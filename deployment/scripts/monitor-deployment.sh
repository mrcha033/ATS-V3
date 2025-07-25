#!/bin/bash

set -euo pipefail

ENVIRONMENT=${1:-production}
DEPLOYMENT_ID=${2:-$(date +%s)}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/common.sh"

MONITORING_CONFIG="${PROJECT_ROOT}/deployment/monitoring-config.json"
ALERT_HISTORY_FILE="${PROJECT_ROOT}/deployment/.alert-history"

log_info "Starting deployment monitoring for environment: ${ENVIRONMENT}, deployment ID: ${DEPLOYMENT_ID}"

load_monitoring_config() {
    if [[ ! -f "${MONITORING_CONFIG}" ]]; then
        create_default_monitoring_config
    fi
    
    log_info "Loading monitoring configuration"
}

create_default_monitoring_config() {
    log_info "Creating default monitoring configuration"
    
    cat > "${MONITORING_CONFIG}" << 'EOF'
{
  "thresholds": {
    "response_time_ms": 1000,
    "error_rate_percent": 5,
    "memory_usage_percent": 80,
    "cpu_usage_percent": 75,
    "disk_usage_percent": 85
  },
  "monitoring_duration_minutes": 30,
  "check_interval_seconds": 60,
  "notification": {
    "enabled": true,
    "channels": {
      "slack": {
        "enabled": false,
        "webhook_url": ""
      },
      "email": {
        "enabled": false,
        "smtp_server": "",
        "recipients": []
      },
      "webhook": {
        "enabled": false,
        "url": ""
      }
    }
  },
  "auto_rollback": {
    "enabled": true,
    "consecutive_failures_threshold": 3,
    "critical_error_threshold": 1
  }
}
EOF
}

get_config_value() {
    local key=$1
    local default_value=${2:-""}
    
    if command -v jq >/dev/null 2>&1 && [[ -f "${MONITORING_CONFIG}" ]]; then
        jq -r "${key} // \"${default_value}\"" "${MONITORING_CONFIG}" 2>/dev/null || echo "${default_value}"
    else
        echo "${default_value}"
    fi
}

check_response_time() {
    local health_url=$1
    local threshold_ms=$2
    
    local start_time
    start_time=$(date +%s%3N)
    
    if curl -f -s "${health_url}" >/dev/null 2>&1; then
        local end_time
        end_time=$(date +%s%3N)
        local response_time=$((end_time - start_time))
        
        log_info "Response time: ${response_time}ms (threshold: ${threshold_ms}ms)"
        
        if [[ ${response_time} -gt ${threshold_ms} ]]; then
            log_warning "Response time exceeded threshold"
            return 1
        fi
        return 0
    else
        log_error "Health check failed - service not responding"
        return 2
    fi
}

check_error_rate() {
    local base_url=$1
    local threshold_percent=$2
    
    # Simulate error rate check (in real implementation, you'd check logs or metrics)
    # For now, we'll use a simple HTTP status check
    local total_requests=10
    local failed_requests=0
    
    for i in $(seq 1 ${total_requests}); do
        if ! curl -f -s "${base_url}/health" >/dev/null 2>&1; then
            ((failed_requests++))
        fi
        sleep 1
    done
    
    local error_rate=$((failed_requests * 100 / total_requests))
    log_info "Error rate: ${error_rate}% (threshold: ${threshold_percent}%)"
    
    if [[ ${error_rate} -gt ${threshold_percent} ]]; then
        log_warning "Error rate exceeded threshold"
        return 1
    fi
    return 0
}

check_system_resources() {
    local memory_threshold=$1
    local cpu_threshold=$2
    local disk_threshold=$3
    
    # Memory check
    local memory_usage
    memory_usage=$(free | grep Mem | awk '{printf "%.0f", $3/$2 * 100.0}')
    log_info "Memory usage: ${memory_usage}% (threshold: ${memory_threshold}%)"
    
    if [[ ${memory_usage} -gt ${memory_threshold} ]]; then
        log_warning "Memory usage exceeded threshold"
        return 1
    fi
    
    # CPU check (if available)
    if command -v top >/dev/null 2>&1; then
        local cpu_usage
        cpu_usage=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | awk -F'%' '{print $1}' | sed 's/us,//')
        log_info "CPU usage: ${cpu_usage}% (threshold: ${cpu_threshold}%)"
        
        if (( $(echo "${cpu_usage} > ${cpu_threshold}" | bc -l) )); then
            log_warning "CPU usage exceeded threshold"
            return 1
        fi
    fi
    
    # Disk check
    local disk_usage
    disk_usage=$(df / | tail -1 | awk '{print $5}' | sed 's/%//')
    log_info "Disk usage: ${disk_usage}% (threshold: ${disk_threshold}%)"
    
    if [[ ${disk_usage} -gt ${disk_threshold} ]]; then
        log_warning "Disk usage exceeded threshold"
        return 1
    fi
    
    return 0
}

send_notification() {
    local alert_type=$1
    local message=$2
    
    log_info "Sending ${alert_type} notification: ${message}"
    
    # Record alert
    local timestamp
    timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "${timestamp}|${ENVIRONMENT}|${DEPLOYMENT_ID}|${alert_type}|${message}" >> "${ALERT_HISTORY_FILE}"
    
    # Check if notifications are enabled
    local notifications_enabled
    notifications_enabled=$(get_config_value ".notification.enabled" "false")
    
    if [[ "${notifications_enabled}" != "true" ]]; then
        log_info "Notifications are disabled"
        return 0
    fi
    
    # Slack notification
    local slack_enabled
    slack_enabled=$(get_config_value ".notification.channels.slack.enabled" "false")
    if [[ "${slack_enabled}" == "true" ]]; then
        local webhook_url
        webhook_url=$(get_config_value ".notification.channels.slack.webhook_url" "")
        if [[ -n "${webhook_url}" ]]; then
            send_slack_notification "${webhook_url}" "${alert_type}" "${message}"
        fi
    fi
    
    # Webhook notification
    local webhook_enabled
    webhook_enabled=$(get_config_value ".notification.channels.webhook.enabled" "false")
    if [[ "${webhook_enabled}" == "true" ]]; then
        local webhook_url
        webhook_url=$(get_config_value ".notification.channels.webhook.url" "")
        if [[ -n "${webhook_url}" ]]; then
            send_webhook_notification "${webhook_url}" "${alert_type}" "${message}"
        fi
    fi
}

send_slack_notification() {
    local webhook_url=$1
    local alert_type=$2
    local message=$3
    
    local emoji
    case "${alert_type}" in
        "CRITICAL") emoji="🚨" ;;
        "WARNING") emoji="⚠️" ;;
        "INFO") emoji="ℹ️" ;;
        "SUCCESS") emoji="✅" ;;
        *) emoji="📢" ;;
    esac
    
    local payload
    payload=$(cat << EOF
{
  "text": "${emoji} ${alert_type}: ATS Deployment Alert",
  "attachments": [
    {
      "color": "$(case "${alert_type}" in CRITICAL) echo "danger" ;; WARNING) echo "warning" ;; SUCCESS) echo "good" ;; *) echo "#439FE0" ;; esac)",
      "fields": [
        {
          "title": "Environment",
          "value": "${ENVIRONMENT}",
          "short": true
        },
        {
          "title": "Deployment ID",
          "value": "${DEPLOYMENT_ID}",
          "short": true
        },
        {
          "title": "Message",
          "value": "${message}",
          "short": false
        }
      ],
      "footer": "ATS Monitoring",
      "ts": $(date +%s)
    }
  ]
}
EOF
)
    
    if ! curl -X POST -H 'Content-type: application/json' --data "${payload}" "${webhook_url}" 2>/dev/null; then
        log_warning "Failed to send Slack notification"
    fi
}

send_webhook_notification() {
    local webhook_url=$1
    local alert_type=$2
    local message=$3
    
    local payload
    payload=$(cat << EOF
{
  "alert_type": "${alert_type}",
  "environment": "${ENVIRONMENT}",
  "deployment_id": "${DEPLOYMENT_ID}",
  "message": "${message}",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
)
    
    if ! curl -X POST -H 'Content-type: application/json' --data "${payload}" "${webhook_url}" 2>/dev/null; then
        log_warning "Failed to send webhook notification"
    fi
}

trigger_auto_rollback() {
    local reason=$1
    
    log_error "Triggering automatic rollback due to: ${reason}"
    
    send_notification "CRITICAL" "Auto-rollback triggered: ${reason}"
    
    # Execute rollback
    if "${SCRIPT_DIR}/rollback.sh" "${ENVIRONMENT}"; then
        send_notification "SUCCESS" "Auto-rollback completed successfully"
        log_success "Auto-rollback completed"
        return 0
    else
        send_notification "CRITICAL" "Auto-rollback failed - manual intervention required"
        log_error "Auto-rollback failed"
        return 1
    fi
}

monitor_deployment() {
    local duration_minutes
    duration_minutes=$(get_config_value ".monitoring_duration_minutes" "30")
    local check_interval
    check_interval=$(get_config_value ".check_interval_seconds" "60")
    
    local end_time
    end_time=$(($(date +%s) + duration_minutes * 60))
    
    local consecutive_failures=0
    local failure_threshold
    failure_threshold=$(get_config_value ".auto_rollback.consecutive_failures_threshold" "3")
    
    local auto_rollback_enabled
    auto_rollback_enabled=$(get_config_value ".auto_rollback.enabled" "true")
    
    log_info "Monitoring deployment for ${duration_minutes} minutes (check interval: ${check_interval}s)"
    
    # Get health URL based on environment
    local health_url
    if [[ "${ENVIRONMENT}" == "staging" ]]; then
        health_url="http://localhost:9080/health"
    else
        health_url="http://localhost:8080/health"
    fi
    
    local base_url
    base_url=$(echo "${health_url}" | sed 's|/health||')
    
    while [[ $(date +%s) -lt ${end_time} ]]; do
        log_info "Running monitoring check (failures: ${consecutive_failures}/${failure_threshold})"
        
        local check_failed=false
        
        # Response time check
        local response_threshold
        response_threshold=$(get_config_value ".thresholds.response_time_ms" "1000")
        if ! check_response_time "${health_url}" "${response_threshold}"; then
            check_failed=true
        fi
        
        # Error rate check
        local error_threshold
        error_threshold=$(get_config_value ".thresholds.error_rate_percent" "5")
        if ! check_error_rate "${base_url}" "${error_threshold}"; then
            check_failed=true
        fi
        
        # System resources check
        local memory_threshold
        memory_threshold=$(get_config_value ".thresholds.memory_usage_percent" "80")
        local cpu_threshold
        cpu_threshold=$(get_config_value ".thresholds.cpu_usage_percent" "75")
        local disk_threshold
        disk_threshold=$(get_config_value ".thresholds.disk_usage_percent" "85")
        
        if ! check_system_resources "${memory_threshold}" "${cpu_threshold}" "${disk_threshold}"; then
            check_failed=true
        fi
        
        if [[ "${check_failed}" == "true" ]]; then
            ((consecutive_failures++))
            send_notification "WARNING" "Monitoring check failed (${consecutive_failures}/${failure_threshold})"
            
            if [[ "${auto_rollback_enabled}" == "true" ]] && [[ ${consecutive_failures} -ge ${failure_threshold} ]]; then
                trigger_auto_rollback "${consecutive_failures} consecutive monitoring failures"
                return $?
            fi
        else
            if [[ ${consecutive_failures} -gt 0 ]]; then
                send_notification "INFO" "Service recovered after ${consecutive_failures} failures"
            fi
            consecutive_failures=0
        fi
        
        sleep "${check_interval}"
    done
    
    log_success "Monitoring completed successfully"
    send_notification "SUCCESS" "Deployment monitoring completed - no critical issues detected"
    return 0
}

main() {
    log_info "Deployment monitoring started"
    
    load_monitoring_config
    
    # Send initial notification
    send_notification "INFO" "Starting deployment monitoring"
    
    if monitor_deployment; then
        log_success "Deployment monitoring completed successfully"
        exit 0
    else
        log_error "Deployment monitoring failed"
        exit 1
    fi
}

main "$@"