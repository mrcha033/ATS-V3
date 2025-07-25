# ATS Deployment Automation

This directory contains the complete deployment automation infrastructure for the ATS (Automated Trading System) project, including CI/CD pipelines, blue-green deployments, automated rollbacks, and comprehensive monitoring.

## 📁 Directory Structure

```
deployment/
├── docker/                     # Docker configurations
│   ├── Dockerfile.ats-core     # Core application container
│   ├── Dockerfile.price-collector
│   ├── Dockerfile.trading-engine
│   ├── Dockerfile.risk-manager
│   ├── Dockerfile.backtest-analytics
│   └── Dockerfile.ui-dashboard
├── scripts/                    # Deployment automation scripts
│   ├── blue-green-deploy.sh    # Blue-green deployment
│   ├── deploy.sh              # Standard deployment
│   ├── rollback.sh            # Automatic rollback
│   ├── health-check.sh        # Health monitoring
│   ├── monitor-deployment.sh  # Deployment monitoring
│   ├── setup-monitoring.sh    # Monitoring infrastructure setup
│   └── common.sh              # Shared utilities
├── prometheus/                 # Prometheus configuration
├── grafana/                   # Grafana dashboards and config
├── alertmanager/              # Alert management
├── nginx/                     # Load balancer configuration
└── docker-compose.*.yml       # Environment-specific compose files
```

## 🚀 Quick Start

### 1. Initial Setup

```bash
# Set up monitoring infrastructure
./deployment/scripts/setup-monitoring.sh

# Start monitoring stack
docker-compose -f deployment/docker-compose.monitoring.yml up -d
```

### 2. Deploy to Staging

```bash
# Deploy specific version to staging
./deployment/scripts/deploy.sh staging v1.0.0

# Monitor deployment
./deployment/scripts/monitor-deployment.sh staging
```

### 3. Deploy to Production (Blue-Green)

```bash
# Blue-green deployment with automatic health checks
./deployment/scripts/blue-green-deploy.sh production v1.0.0

# Manual health check
./deployment/scripts/health-check.sh production
```

### 4. Rollback (if needed)

```bash
# Automatic rollback to previous version
./deployment/scripts/rollback.sh production
```

## 🔄 CI/CD Pipeline

The GitHub Actions workflow (`.github/workflows/ci-cd.yml`) provides:

- **Automated Testing**: Runs on every PR and push
- **Docker Image Building**: Multi-service container builds
- **Staging Deployment**: Automatic deployment on `develop` branch
- **Production Deployment**: Blue-green deployment on version tags
- **Automatic Rollback**: On deployment failure

### Triggering Deployments

```bash
# Create a production release
git tag v1.0.0
git push origin v1.0.0  # Triggers production deployment

# Deploy to staging
git push origin develop  # Triggers staging deployment
```

## 🔵🟢 Blue-Green Deployment

The blue-green deployment strategy ensures zero-downtime deployments:

1. **Deploy to Inactive Environment**: New version deployed to unused environment
2. **Health Checks**: Comprehensive validation before traffic switch
3. **Traffic Switch**: Load balancer redirects traffic to new environment
4. **Cleanup**: Old environment is decommissioned after successful switch

### Configuration

- **Blue Environment**: Port 8080 (default)
- **Green Environment**: Port 8081
- **Health Check Timeout**: 5 minutes
- **Traffic Switch**: Nginx configuration update

## 📊 Monitoring & Alerting

### Prometheus Metrics

- **Service Health**: Uptime and response times
- **Resource Usage**: CPU, memory, disk utilization
- **Application Metrics**: Trade execution, error rates
- **Infrastructure**: Redis, InfluxDB, container health

### Grafana Dashboards

- **ATS Overview**: System-wide health and performance
- **Trading Metrics**: Live trading statistics
- **Infrastructure**: Resource utilization and alerts

### Alert Rules

- **Critical**: Service down, high error rates
- **Warning**: High resource usage, slow response times
- **Info**: Deployment events, recovery notifications

### Access URLs

- **Grafana**: http://localhost:3000 (admin/ats-admin)
- **Prometheus**: http://localhost:9090
- **Alertmanager**: http://localhost:9093

## 🔧 Configuration

### Monitoring Thresholds

Edit `deployment/monitoring-config.json`:

```json
{
  "thresholds": {
    "response_time_ms": 1000,
    "error_rate_percent": 5,
    "memory_usage_percent": 80,
    "cpu_usage_percent": 75,
    "disk_usage_percent": 85
  },
  "auto_rollback": {
    "enabled": true,
    "consecutive_failures_threshold": 3
  }
}
```

### Notification Channels

Configure alerts in `deployment/monitoring-config.json`:

```json
{
  "notification": {
    "channels": {
      "slack": {
        "enabled": true,
        "webhook_url": "YOUR_SLACK_WEBHOOK"
      },
      "webhook": {
        "enabled": true,
        "url": "YOUR_WEBHOOK_URL"
      }
    }
  }
}
```

## 🛡️ Security Features

- **Container Security**: Multi-stage builds, minimal base images
- **Secrets Management**: Environment variables, no hardcoded credentials
- **Network Isolation**: Docker networks with service segmentation
- **Health Checks**: Built-in container health monitoring
- **Access Control**: Service-to-service authentication

## 📋 Health Checks

### Automated Checks

- **HTTP Endpoints**: `/health` endpoint monitoring
- **Container Status**: Docker container health
- **Resource Usage**: System resource monitoring
- **Service Connectivity**: Inter-service communication

### Manual Health Check

```bash
# Check specific environment
./deployment/scripts/health-check.sh production

# Generate health report
./deployment/scripts/health-check.sh staging
```

## 🔄 Rollback Process

### Automatic Rollback Triggers

- **Consecutive Health Check Failures**: 3+ failures
- **Critical Error Threshold**: Immediate rollback
- **High Error Rate**: >5% error rate sustained
- **Resource Exhaustion**: Memory/CPU limits exceeded

### Manual Rollback

```bash
# Rollback to previous version
./deployment/scripts/rollback.sh production

# Check rollback status
./deployment/scripts/health-check.sh production
```

## 🐳 Docker Services

### Service Architecture

- **ats-core**: Main orchestrator (Port 50051)
- **price-collector**: Market data collection (Port 50052)
- **trading-engine**: Trade execution (Port 50053)
- **risk-manager**: Risk management (Port 50054)
- **backtest-analytics**: Strategy backtesting (Port 50055)
- **ui-dashboard**: Web interface (Port 8080)

### Resource Limits

```yaml
deploy:
  resources:
    limits:
      memory: 2G
      cpus: '1.0'
    reservations:
      memory: 1G
      cpus: '0.5'
```

## 🚨 Troubleshooting

### Common Issues

1. **Health Check Failures**
   ```bash
   # Check service logs
   docker-compose logs service-name
   
   # Manual service test
   curl -f http://localhost:8080/health
   ```

2. **Deployment Stuck**
   ```bash
   # Check container status
   docker ps -a
   
   # Check resource usage
   docker stats
   ```

3. **Rollback Issues**
   ```bash
   # Check deployment history
   cat deployment/.deployment-history
   
   # Manual environment switch
   ./deployment/scripts/blue-green-deploy.sh production previous-version
   ```

### Log Locations

- **Deployment Logs**: `deployment/logs/`
- **Alert History**: `deployment/.alert-history`
- **Health Reports**: `deployment/health-report-*.json`

## 📈 Performance Optimization

### Container Optimization

- **Multi-stage Builds**: Reduced image sizes
- **Layer Caching**: Optimized Docker build times
- **Resource Limits**: Prevented resource exhaustion

### Deployment Optimization

- **Parallel Builds**: Multiple services built simultaneously
- **Health Check Optimization**: Faster failure detection
- **Monitoring Efficiency**: Minimal performance impact

## 🔄 Maintenance

### Regular Tasks

```bash
# Clean up old Docker images
docker system prune -af

# Update monitoring configuration
./deployment/scripts/setup-monitoring.sh

# Review deployment history
cat deployment/.deployment-history
```

### Monthly Maintenance

- Review and update monitoring thresholds
- Analyze deployment success rates
- Update container base images
- Review security configurations

## 📚 Additional Resources

- **Docker Documentation**: Service configuration details
- **Prometheus Docs**: Metrics and alerting setup
- **Grafana Guides**: Dashboard configuration
- **GitHub Actions**: CI/CD pipeline customization

For support or questions, please refer to the project documentation or create an issue in the repository.