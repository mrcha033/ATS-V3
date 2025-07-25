#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/common.sh"

log_info "Setting up deployment monitoring infrastructure"

create_monitoring_directories() {
    log_info "Creating monitoring directories"
    
    mkdir -p "${PROJECT_ROOT}/deployment/logs"
    mkdir -p "${PROJECT_ROOT}/deployment/alerts"
    mkdir -p "${PROJECT_ROOT}/deployment/nginx"
    mkdir -p "${PROJECT_ROOT}/deployment/prometheus"
    mkdir -p "${PROJECT_ROOT}/deployment/grafana"
    
    log_success "Monitoring directories created"
}

setup_prometheus_config() {
    log_info "Setting up Prometheus configuration"
    
    cat > "${PROJECT_ROOT}/deployment/prometheus/prometheus.yml" << 'EOF'
global:
  scrape_interval: 15s
  evaluation_interval: 15s

rule_files:
  - "alerts.yml"

alerting:
  alertmanagers:
    - static_configs:
        - targets:
          - alertmanager:9093

scrape_configs:
  - job_name: 'ats-services'
    static_configs:
      - targets: 
        - 'ats-core:8080'
        - 'price-collector:8081'
        - 'trading-engine:8082'
        - 'risk-manager:8083'
        - 'backtest-analytics:8084'
    metrics_path: '/metrics'
    scrape_interval: 5s

  - job_name: 'node-exporter'
    static_configs:
      - targets:
        - 'node-exporter:9100'

  - job_name: 'redis'
    static_configs:
      - targets:
        - 'redis:6379'

  - job_name: 'influxdb'
    static_configs:
      - targets:
        - 'influxdb:8086'
EOF
    
    log_success "Prometheus configuration created"
}

setup_prometheus_alerts() {
    log_info "Setting up Prometheus alert rules"
    
    cat > "${PROJECT_ROOT}/deployment/prometheus/alerts.yml" << 'EOF'
groups:
  - name: ats-alerts
    rules:
      - alert: ServiceDown
        expr: up == 0
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "Service {{ $labels.instance }} is down"
          description: "{{ $labels.job }} on {{ $labels.instance }} has been down for more than 1 minute."

      - alert: HighResponseTime
        expr: histogram_quantile(0.95, rate(http_request_duration_seconds_bucket[5m])) > 1
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "High response time on {{ $labels.instance }}"
          description: "95th percentile response time is {{ $value }}s for {{ $labels.instance }}"

      - alert: HighErrorRate
        expr: rate(http_requests_total{status=~"5.."}[5m]) / rate(http_requests_total[5m]) > 0.05
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "High error rate on {{ $labels.instance }}"
          description: "Error rate is {{ $value | humanizePercentage }} for {{ $labels.instance }}"

      - alert: HighMemoryUsage
        expr: (node_memory_MemTotal_bytes - node_memory_MemAvailable_bytes) / node_memory_MemTotal_bytes > 0.8
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "High memory usage on {{ $labels.instance }}"
          description: "Memory usage is {{ $value | humanizePercentage }} for {{ $labels.instance }}"

      - alert: HighCPUUsage
        expr: 100 - (avg by(instance) (irate(node_cpu_seconds_total{mode="idle"}[5m])) * 100) > 75
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "High CPU usage on {{ $labels.instance }}"
          description: "CPU usage is {{ $value }}% for {{ $labels.instance }}"

      - alert: LowDiskSpace
        expr: (node_filesystem_free_bytes / node_filesystem_size_bytes) < 0.15
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "Low disk space on {{ $labels.instance }}"
          description: "Disk space is {{ $value | humanizePercentage }} full for {{ $labels.instance }}"

      - alert: TradingEngineDown
        expr: up{job="ats-services", instance="trading-engine:8082"} == 0
        for: 30s
        labels:
          severity: critical
        annotations:
          summary: "Trading Engine is down"
          description: "Critical trading engine service is not responding"

      - alert: PriceCollectorDown
        expr: up{job="ats-services", instance="price-collector:8081"} == 0
        for: 30s
        labels:
          severity: critical
        annotations:
          summary: "Price Collector is down"
          description: "Critical price collector service is not responding"
EOF
    
    log_success "Prometheus alert rules created"
}

setup_grafana_config() {
    log_info "Setting up Grafana configuration"
    
    cat > "${PROJECT_ROOT}/deployment/grafana/grafana.ini" << 'EOF'
[server]
http_port = 3000

[auth.anonymous]
enabled = true
org_role = Viewer

[security]
admin_user = admin
admin_password = ats-admin

[dashboards]
default_home_dashboard_path = /var/lib/grafana/dashboards/ats-overview.json
EOF
    
    # Create datasource configuration
    mkdir -p "${PROJECT_ROOT}/deployment/grafana/provisioning/datasources"
    cat > "${PROJECT_ROOT}/deployment/grafana/provisioning/datasources/prometheus.yml" << 'EOF'
apiVersion: 1

datasources:
  - name: Prometheus
    type: prometheus
    access: proxy
    url: http://prometheus:9090
    isDefault: true
    editable: true
EOF
    
    # Create dashboard configuration
    mkdir -p "${PROJECT_ROOT}/deployment/grafana/provisioning/dashboards"
    cat > "${PROJECT_ROOT}/deployment/grafana/provisioning/dashboards/dashboards.yml" << 'EOF'
apiVersion: 1

providers:
  - name: 'default'
    orgId: 1
    folder: ''
    type: file
    disableDeletion: false
    updateIntervalSeconds: 10
    allowUiUpdates: true
    options:
      path: /var/lib/grafana/dashboards
EOF
    
    log_success "Grafana configuration created"
}

setup_docker_monitoring_stack() {
    log_info "Setting up Docker monitoring stack"
    
    cat > "${PROJECT_ROOT}/deployment/docker-compose.monitoring.yml" << 'EOF'
version: '3.8'

services:
  prometheus:
    image: prom/prometheus:latest
    ports:
      - "9090:9090"
    volumes:
      - ./prometheus/prometheus.yml:/etc/prometheus/prometheus.yml
      - ./prometheus/alerts.yml:/etc/prometheus/alerts.yml
      - prometheus_data:/prometheus
    command:
      - '--config.file=/etc/prometheus/prometheus.yml'
      - '--storage.tsdb.path=/prometheus'
      - '--web.console.libraries=/etc/prometheus/console_libraries'
      - '--web.console.templates=/etc/prometheus/consoles'
      - '--storage.tsdb.retention.time=200h'
      - '--web.enable-lifecycle'
      - '--web.enable-admin-api'
    networks:
      - monitoring
    restart: unless-stopped

  alertmanager:
    image: prom/alertmanager:latest
    ports:
      - "9093:9093"
    volumes:
      - ./alertmanager/alertmanager.yml:/etc/alertmanager/alertmanager.yml
      - alertmanager_data:/alertmanager
    command:
      - '--config.file=/etc/alertmanager/alertmanager.yml'
      - '--storage.path=/alertmanager'
      - '--web.external-url=http://localhost:9093'
    networks:
      - monitoring
    restart: unless-stopped

  grafana:
    image: grafana/grafana:latest
    ports:
      - "3000:3000"
    volumes:
      - grafana_data:/var/lib/grafana
      - ./grafana/grafana.ini:/etc/grafana/grafana.ini
      - ./grafana/provisioning:/etc/grafana/provisioning
      - ./grafana/dashboards:/var/lib/grafana/dashboards
    environment:
      - GF_SECURITY_ADMIN_PASSWORD=ats-admin
    networks:
      - monitoring
    restart: unless-stopped

  node-exporter:
    image: prom/node-exporter:latest
    ports:
      - "9100:9100"
    volumes:
      - /proc:/host/proc:ro
      - /sys:/host/sys:ro
      - /:/rootfs:ro
    command:
      - '--path.procfs=/host/proc'
      - '--path.rootfs=/rootfs'
      - '--path.sysfs=/host/sys'
      - '--collector.filesystem.mount-points-exclude=^/(sys|proc|dev|host|etc)($$|/)'
    networks:
      - monitoring
    restart: unless-stopped

networks:
  monitoring:
    driver: bridge

volumes:
  prometheus_data:
  alertmanager_data:
  grafana_data:
EOF
    
    log_success "Docker monitoring stack configuration created"
}

setup_alertmanager_config() {
    log_info "Setting up Alertmanager configuration"
    
    mkdir -p "${PROJECT_ROOT}/deployment/alertmanager"
    cat > "${PROJECT_ROOT}/deployment/alertmanager/alertmanager.yml" << 'EOF'
global:
  smtp_smarthost: 'localhost:587'
  smtp_from: 'alerts@ats-trading.com'

templates:
  - '/etc/alertmanager/template/*.tmpl'

route:
  group_by: ['alertname']
  group_wait: 10s
  group_interval: 10s
  repeat_interval: 1h
  receiver: 'default'

receivers:
  - name: 'default'
    webhook_configs:
      - url: 'http://webhook-receiver:8080/webhook'
        send_resolved: true
        http_config:
          bearer_token: 'your-webhook-token'

inhibit_rules:
  - source_match:
      severity: 'critical'
    target_match:
      severity: 'warning'
    equal: ['alertname', 'dev', 'instance']
EOF
    
    log_success "Alertmanager configuration created"
}

make_scripts_executable() {
    log_info "Making deployment scripts executable"
    
    chmod +x "${PROJECT_ROOT}/deployment/scripts/"*.sh
    
    log_success "Deployment scripts are now executable"
}

main() {
    log_info "Monitoring setup started"
    
    create_monitoring_directories
    setup_prometheus_config
    setup_prometheus_alerts
    setup_grafana_config
    setup_docker_monitoring_stack
    setup_alertmanager_config
    make_scripts_executable
    
    log_success "Monitoring infrastructure setup completed"
    log_info "Next steps:"
    log_info "1. Configure notification channels in deployment/monitoring-config.json"
    log_info "2. Start monitoring stack: docker-compose -f deployment/docker-compose.monitoring.yml up -d"
    log_info "3. Access Grafana at http://localhost:3000 (admin/ats-admin)"
    log_info "4. Access Prometheus at http://localhost:9090"
}

main "$@"