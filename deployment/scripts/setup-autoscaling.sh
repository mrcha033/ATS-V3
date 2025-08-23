#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/common.sh"

log_info "Setting up auto-scaling configuration based on Prometheus metrics"

# Create auto-scaling directory
mkdir -p "${PROJECT_ROOT}/deployment/autoscaling"

setup_alertmanager_autoscaling_rules() {
    log_info "Setting up Alertmanager rules for auto-scaling triggers"
    
    cat > "${PROJECT_ROOT}/deployment/prometheus/autoscaling-alerts.yml" << 'EOF'
groups:
  - name: autoscaling-alerts
    rules:
      # CPU-based scaling triggers
      - alert: HighCPUUsageScaleUp
        expr: avg(ats_cpu_usage_percent) by (service) > 70
        for: 2m
        labels:
          severity: warning
          action: scale_up
          resource: cpu
        annotations:
          summary: "High CPU usage detected on {{ $labels.service }}"
          description: "CPU usage is {{ $value }}% on {{ $labels.service }}, consider scaling up"
          scale_trigger: "cpu_high"
          
      - alert: LowCPUUsageScaleDown
        expr: avg(ats_cpu_usage_percent) by (service) < 20
        for: 10m
        labels:
          severity: info
          action: scale_down
          resource: cpu
        annotations:
          summary: "Low CPU usage detected on {{ $labels.service }}"
          description: "CPU usage is {{ $value }}% on {{ $labels.service }}, consider scaling down"
          scale_trigger: "cpu_low"
          
      # Memory-based scaling triggers
      - alert: HighMemoryUsageScaleUp
        expr: ats_memory_usage_mb > 6144  # 6GB
        for: 5m
        labels:
          severity: warning
          action: scale_up
          resource: memory
        annotations:
          summary: "High memory usage detected on {{ $labels.service }}"
          description: "Memory usage is {{ $value }}MB on {{ $labels.service }}, consider scaling up"
          scale_trigger: "memory_high"
          
      # Trading performance-based scaling
      - alert: HighOrderLatencyScaleUp
        expr: histogram_quantile(0.95, rate(ats_order_latency_milliseconds_bucket[5m])) > 500
        for: 3m
        labels:
          severity: critical
          action: scale_up
          resource: performance
        annotations:
          summary: "High order latency detected"
          description: "95th percentile latency is {{ $value }}ms, scaling up for better performance"
          scale_trigger: "latency_high"
          
      - alert: HighTradeVolumeScaleUp
        expr: rate(ats_successful_trades_total[5m]) > 10
        for: 2m
        labels:
          severity: info
          action: scale_up
          resource: throughput
        annotations:
          summary: "High trading volume detected"
          description: "Trading rate is {{ $value }} trades/sec, consider scaling up"
          scale_trigger: "volume_high"
          
      - alert: LowTradeVolumeScaleDown
        expr: rate(ats_successful_trades_total[30m]) < 0.1
        for: 20m
        labels:
          severity: info
          action: scale_down
          resource: throughput
        annotations:
          summary: "Low trading volume detected"
          description: "Trading rate is {{ $value }} trades/sec over 30min, consider scaling down"
          scale_trigger: "volume_low"
          
      # Error rate-based scaling
      - alert: HighErrorRateScaleUp
        expr: rate(ats_failed_trades_total[5m]) / rate(ats_successful_trades_total[5m]) > 0.05
        for: 2m
        labels:
          severity: critical
          action: scale_up
          resource: reliability
        annotations:
          summary: "High trade error rate detected"
          description: "Trade error rate is {{ $value | humanizePercentage }}, scaling up for reliability"
          scale_trigger: "error_rate_high"
          
      - alert: APIErrorsScaleUp
        expr: sum(rate(ats_api_errors_total[5m])) > 5
        for: 1m
        labels:
          severity: warning
          action: scale_up
          resource: connectivity
        annotations:
          summary: "High API error rate detected"
          description: "API error rate is {{ $value }} errors/sec, scaling up for better connectivity"
          scale_trigger: "api_errors_high"
          
      # Queue depth-based scaling (if queue metrics are available)
      - alert: HighQueueDepthScaleUp
        expr: ats_queue_depth > 100
        for: 1m
        labels:
          severity: warning
          action: scale_up
          resource: queue
        annotations:
          summary: "High queue depth detected"
          description: "Queue depth is {{ $value }}, scaling up to handle backlog"
          scale_trigger: "queue_depth_high"
EOF
    
    log_success "Auto-scaling alert rules created"
}

setup_scaling_webhook() {
    log_info "Setting up webhook handler for auto-scaling actions"
    
    cat > "${PROJECT_ROOT}/deployment/autoscaling/webhook-handler.py" << 'EOF'
#!/usr/bin/env python3

import json
import logging
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class ScalingWebhookHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        """Handle POST requests from Alertmanager"""
        try:
            # Get content length and read body
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            
            # Parse JSON payload
            payload = json.loads(post_data.decode('utf-8'))
            
            # Process alerts
            for alert in payload.get('alerts', []):
                self.process_alert(alert)
                
            # Send response
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok"}).encode())
            
        except Exception as e:
            logger.error(f"Error processing webhook: {e}")
            self.send_response(500)
            self.end_headers()
    
    def process_alert(self, alert):
        """Process individual alert and trigger scaling action"""
        try:
            labels = alert.get('labels', {})
            annotations = alert.get('annotations', {})
            status = alert.get('status', 'firing')
            
            service = labels.get('service', 'unknown')
            action = labels.get('action', 'none')
            resource = labels.get('resource', 'unknown')
            scale_trigger = annotations.get('scale_trigger', 'manual')
            
            logger.info(f"Processing alert: {alert.get('alertname', 'unknown')}")
            logger.info(f"Service: {service}, Action: {action}, Resource: {resource}")
            
            if status == 'firing' and action in ['scale_up', 'scale_down']:
                self.execute_scaling_action(service, action, resource, scale_trigger)
            else:
                logger.info(f"Ignoring alert with status={status}, action={action}")
                
        except Exception as e:
            logger.error(f"Error processing alert: {e}")
    
    def execute_scaling_action(self, service, action, resource, trigger):
        """Execute the actual scaling action"""
        try:
            scaling_config = self.get_scaling_config(service)
            
            if not scaling_config:
                logger.warning(f"No scaling configuration found for service: {service}")
                return
                
            current_instances = self.get_current_instances(service)
            
            if action == 'scale_up':
                new_instances = min(current_instances + scaling_config['scale_step'], 
                                  scaling_config['max_instances'])
            else:  # scale_down
                new_instances = max(current_instances - scaling_config['scale_step'], 
                                  scaling_config['min_instances'])
            
            if new_instances != current_instances:
                logger.info(f"Scaling {service} from {current_instances} to {new_instances} instances")
                self.scale_service(service, new_instances)
                
                # Log scaling event
                self.log_scaling_event(service, action, current_instances, new_instances, trigger)
            else:
                logger.info(f"No scaling needed for {service} (already at limits)")
                
        except Exception as e:
            logger.error(f"Error executing scaling action: {e}")
    
    def get_scaling_config(self, service):
        """Get scaling configuration for service"""
        # Default scaling configurations
        scaling_configs = {
            'trading-engine': {
                'min_instances': 1,
                'max_instances': 5,
                'scale_step': 1
            },
            'price-collector': {
                'min_instances': 1,
                'max_instances': 3,
                'scale_step': 1
            },
            'risk-manager': {
                'min_instances': 1,
                'max_instances': 3,
                'scale_step': 1
            },
            'backtest-analytics': {
                'min_instances': 0,
                'max_instances': 2,
                'scale_step': 1
            }
        }
        
        return scaling_configs.get(service)
    
    def get_current_instances(self, service):
        """Get current number of instances for service"""
        try:
            # Use docker-compose to get current replica count
            result = subprocess.run([
                'docker-compose', '-f', '/deployment/docker-compose.yml', 
                'ps', '-q', service
            ], capture_output=True, text=True)
            
            if result.returncode == 0:
                return len(result.stdout.strip().split('\n')) if result.stdout.strip() else 0
            else:
                logger.error(f"Failed to get instance count for {service}")
                return 1  # Default assumption
                
        except Exception as e:
            logger.error(f"Error getting current instances: {e}")
            return 1
    
    def scale_service(self, service, instances):
        """Scale service to specified number of instances"""
        try:
            # Use docker-compose scale command
            result = subprocess.run([
                'docker-compose', '-f', '/deployment/docker-compose.production.yml',
                'up', '--scale', f'{service}={instances}', '-d'
            ], capture_output=True, text=True)
            
            if result.returncode == 0:
                logger.info(f"Successfully scaled {service} to {instances} instances")
            else:
                logger.error(f"Failed to scale {service}: {result.stderr}")
                
        except Exception as e:
            logger.error(f"Error scaling service: {e}")
    
    def log_scaling_event(self, service, action, old_count, new_count, trigger):
        """Log scaling event for audit trail"""
        scaling_event = {
            'timestamp': self.date_time_string(),
            'service': service,
            'action': action,
            'old_instances': old_count,
            'new_instances': new_count,
            'trigger': trigger
        }
        
        # Write to scaling log file
        try:
            with open('/deployment/logs/scaling-events.log', 'a') as f:
                f.write(json.dumps(scaling_event) + '\n')
        except Exception as e:
            logger.error(f"Failed to write scaling event log: {e}")

def main():
    """Main function to start the webhook server"""
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    
    server = HTTPServer(('', port), ScalingWebhookHandler)
    logger.info(f"Starting auto-scaling webhook server on port {port}")
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logger.info("Shutting down webhook server")
        server.shutdown()

if __name__ == '__main__':
    main()
EOF
    
    chmod +x "${PROJECT_ROOT}/deployment/autoscaling/webhook-handler.py"
    log_success "Auto-scaling webhook handler created"
}

setup_scaling_policies() {
    log_info "Setting up scaling policies configuration"
    
    cat > "${PROJECT_ROOT}/deployment/autoscaling/scaling-policies.yml" << 'EOF'
# Auto-scaling policies for ATS services
scaling_policies:
  trading-engine:
    enabled: true
    min_instances: 1
    max_instances: 5
    scale_up_threshold:
      cpu_percent: 70
      memory_mb: 6144
      latency_ms: 500
      error_rate_percent: 5
    scale_down_threshold:
      cpu_percent: 20
      memory_mb: 2048
      idle_time_minutes: 15
    cooldown_period_minutes: 5
    
  price-collector:
    enabled: true
    min_instances: 1
    max_instances: 3
    scale_up_threshold:
      cpu_percent: 75
      memory_mb: 4096
      api_errors_per_second: 10
      websocket_reconnections_per_minute: 5
    scale_down_threshold:
      cpu_percent: 25
      memory_mb: 1024
      idle_time_minutes: 10
    cooldown_period_minutes: 3
    
  risk-manager:
    enabled: true
    min_instances: 1
    max_instances: 3
    scale_up_threshold:
      cpu_percent: 80
      memory_mb: 3072
      risk_violations_per_minute: 10
    scale_down_threshold:
      cpu_percent: 30
      memory_mb: 1024
      idle_time_minutes: 20
    cooldown_period_minutes: 10
    
  backtest-analytics:
    enabled: false  # Scale manually for batch jobs
    min_instances: 0
    max_instances: 2
    scale_up_threshold:
      cpu_percent: 60
      memory_mb: 8192
    scale_down_threshold:
      cpu_percent: 10
      idle_time_minutes: 30
    cooldown_period_minutes: 15

# Global settings
global_settings:
  enable_autoscaling: true
  webhook_url: "http://webhook-handler:8080/webhook"
  monitoring_interval_seconds: 30
  scaling_logs_retention_days: 30
  enable_notifications: true
  notification_channels:
    - slack
    - email
EOF
    
    log_success "Auto-scaling policies configuration created"
}

setup_docker_compose_scaling() {
    log_info "Adding auto-scaling services to Docker Compose"
    
    cat > "${PROJECT_ROOT}/deployment/docker-compose.autoscaling.yml" << 'EOF'
version: '3.8'

services:
  webhook-handler:
    build:
      context: ./autoscaling
      dockerfile: Dockerfile.webhook
    ports:
      - "8080:8080"
    volumes:
      - ./logs:/deployment/logs
      - /var/run/docker.sock:/var/run/docker.sock
      - ./docker-compose.production.yml:/deployment/docker-compose.production.yml
    environment:
      - WEBHOOK_PORT=8080
      - LOG_LEVEL=INFO
    networks:
      - monitoring
      - ats-network
    restart: unless-stopped
    depends_on:
      - prometheus
      - alertmanager

  scaling-controller:
    build:
      context: ./autoscaling
      dockerfile: Dockerfile.controller  
    volumes:
      - ./autoscaling/scaling-policies.yml:/config/scaling-policies.yml
      - ./logs:/logs
      - /var/run/docker.sock:/var/run/docker.sock
    environment:
      - PROMETHEUS_URL=http://prometheus:9090
      - WEBHOOK_URL=http://webhook-handler:8080/webhook
      - CONFIG_FILE=/config/scaling-policies.yml
    networks:
      - monitoring
      - ats-network
    restart: unless-stopped
    depends_on:
      - prometheus
      - webhook-handler

networks:
  monitoring:
    external: true
  ats-network:
    external: true
EOF
    
    log_success "Auto-scaling Docker Compose configuration created"
}

create_scaling_dockerfile() {
    log_info "Creating Dockerfile for scaling services"
    
    mkdir -p "${PROJECT_ROOT}/deployment/autoscaling"
    
    cat > "${PROJECT_ROOT}/deployment/autoscaling/Dockerfile.webhook" << 'EOF'
FROM python:3.9-slim

WORKDIR /app

# Install required packages
RUN apt-get update && apt-get install -y \
    docker.io \
    docker-compose \
    && rm -rf /var/lib/apt/lists/*

# Copy webhook handler
COPY webhook-handler.py /app/
RUN chmod +x /app/webhook-handler.py

# Create directories
RUN mkdir -p /deployment/logs

EXPOSE 8080

CMD ["python3", "/app/webhook-handler.py", "8080"]
EOF
    
    log_success "Auto-scaling Dockerfiles created"
}

update_alertmanager_config() {
    log_info "Updating Alertmanager configuration for auto-scaling webhooks"
    
    cat > "${PROJECT_ROOT}/deployment/alertmanager/alertmanager-autoscaling.yml" << 'EOF'
global:
  smtp_smarthost: 'localhost:587'
  smtp_from: 'alerts@ats-trading.com'

templates:
  - '/etc/alertmanager/template/*.tmpl'

route:
  group_by: ['alertname', 'service']
  group_wait: 10s
  group_interval: 10s
  repeat_interval: 5m
  receiver: 'default'
  routes:
    - match:
        severity: critical
      receiver: 'critical-alerts'
      routes:
        - match:
            action: scale_up
          receiver: 'autoscaling-webhook'
        - match:
            action: scale_down
          receiver: 'autoscaling-webhook'
    - match:
        severity: warning
      receiver: 'warning-alerts'
      routes:
        - match:
            action: scale_up
          receiver: 'autoscaling-webhook'
        - match:
            action: scale_down
          receiver: 'autoscaling-webhook'

receivers:
  - name: 'default'
    webhook_configs:
      - url: 'http://webhook-handler:8080/webhook'
        send_resolved: true
        
  - name: 'critical-alerts'
    webhook_configs:
      - url: 'http://webhook-handler:8080/webhook'
        send_resolved: true
        http_config:
          bearer_token: 'critical-scaling-token'
          
  - name: 'warning-alerts'
    webhook_configs:
      - url: 'http://webhook-handler:8080/webhook'
        send_resolved: true
        
  - name: 'autoscaling-webhook'
    webhook_configs:
      - url: 'http://webhook-handler:8080/webhook'
        send_resolved: false
        http_config:
          bearer_token: 'autoscaling-token'

inhibit_rules:
  - source_match:
      severity: 'critical'
    target_match:
      severity: 'warning'
    equal: ['alertname', 'service', 'instance']
EOF
    
    log_success "Alertmanager auto-scaling configuration created"
}

create_scaling_documentation() {
    log_info "Creating auto-scaling documentation"
    
    cat > "${PROJECT_ROOT}/deployment/autoscaling/README.md" << 'EOF'
# ATS Auto-Scaling Configuration

This directory contains the auto-scaling configuration for the ATS trading system, which automatically adjusts the number of service instances based on Prometheus metrics.

## Components

### 1. Webhook Handler (`webhook-handler.py`)
- Receives alerts from Alertmanager
- Processes scaling triggers
- Executes Docker Compose scaling commands
- Logs all scaling events

### 2. Scaling Policies (`scaling-policies.yml`)
- Defines scaling thresholds for each service
- Sets minimum and maximum instance limits
- Configures cooldown periods

### 3. Alert Rules (`autoscaling-alerts.yml`)
- Prometheus alert rules that trigger scaling actions
- Based on CPU, memory, latency, and trading performance metrics

## Scaling Triggers

### Trading Engine
- **Scale Up**: CPU > 70%, Memory > 6GB, Latency > 500ms, Error rate > 5%
- **Scale Down**: CPU < 20%, Memory < 2GB, Idle > 15min

### Price Collector
- **Scale Up**: CPU > 75%, Memory > 4GB, API errors > 10/sec, WS reconnections > 5/min
- **Scale Down**: CPU < 25%, Memory < 1GB, Idle > 10min

### Risk Manager
- **Scale Up**: CPU > 80%, Memory > 3GB, Risk violations > 10/min
- **Scale Down**: CPU < 30%, Memory < 1GB, Idle > 20min

## Setup Instructions

1. **Start monitoring stack**:
   ```bash
   docker-compose -f docker-compose.monitoring.yml up -d
   ```

2. **Deploy auto-scaling services**:
   ```bash
   docker-compose -f docker-compose.autoscaling.yml up -d
   ```

3. **Monitor scaling events**:
   ```bash
   tail -f deployment/logs/scaling-events.log
   ```

## Configuration

### Environment Variables
- `WEBHOOK_PORT`: Port for webhook handler (default: 8080)
- `LOG_LEVEL`: Logging level (default: INFO)
- `PROMETHEUS_URL`: Prometheus server URL
- `WEBHOOK_URL`: Webhook handler URL

### Scaling Policies
Edit `scaling-policies.yml` to adjust:
- Minimum/maximum instances
- Scaling thresholds
- Cooldown periods

### Alert Rules
Modify `autoscaling-alerts.yml` to change:
- Metric thresholds
- Alert evaluation periods
- Scaling triggers

## Monitoring

### Grafana Dashboards
- View scaling events in the "ATS Overview" dashboard
- Monitor resource utilization trends
- Track scaling effectiveness

### Logs
- Scaling events: `deployment/logs/scaling-events.log`
- Webhook handler: `docker logs webhook-handler`
- Controller logs: `docker logs scaling-controller`

## Troubleshooting

### Common Issues
1. **Services not scaling**: Check webhook handler logs and Docker socket permissions
2. **Alert not firing**: Verify Prometheus targets and alert rule syntax
3. **Scaling too aggressive**: Increase cooldown periods in policies

### Manual Scaling
To manually scale a service:
```bash
docker-compose -f docker-compose.production.yml up --scale trading-engine=3 -d
```

### Disable Auto-Scaling
Set `enable_autoscaling: false` in `scaling-policies.yml` and restart services.
EOF
    
    log_success "Auto-scaling documentation created"
}

main() {
    log_info "Auto-scaling setup started"
    
    setup_alertmanager_autoscaling_rules
    setup_scaling_webhook
    setup_scaling_policies
    setup_docker_compose_scaling
    create_scaling_dockerfile
    update_alertmanager_config
    create_scaling_documentation
    
    log_success "Auto-scaling infrastructure setup completed"
    log_info "Next steps:"
    log_info "1. Review scaling policies in deployment/autoscaling/scaling-policies.yml"
    log_info "2. Start auto-scaling stack: docker-compose -f deployment/docker-compose.autoscaling.yml up -d"
    log_info "3. Monitor scaling events: tail -f deployment/logs/scaling-events.log"
    log_info "4. View metrics in Grafana dashboards"
}

main "$@"