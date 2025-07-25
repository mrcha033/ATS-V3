#!/bin/bash

set -euo pipefail

ENVIRONMENT=${1:-production}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/common.sh"

ACTIVE_ENV_FILE="${PROJECT_ROOT}/deployment/.active-environment"

log_info "Starting health check for environment: ${ENVIRONMENT}"

get_active_environment() {
    if [[ -f "${ACTIVE_ENV_FILE}" ]]; then
        cat "${ACTIVE_ENV_FILE}"
    else
        echo "blue"
    fi
}

get_health_endpoints() {
    local env_color=$(get_active_environment)
    
    if [[ "${ENVIRONMENT}" == "staging" ]]; then
        echo "http://localhost:9080/health"
    elif [[ "${env_color}" == "green" ]]; then
        echo "http://localhost:8081/health"
    else
        echo "http://localhost:8080/health"
    fi
}

check_service_health() {
    local service_name=$1
    local health_url=$2
    local timeout=${3:-30}
    
    log_info "Checking health of ${service_name} at ${health_url}"
    
    for i in $(seq 1 ${timeout}); do
        if curl -f -s "${health_url}" >/dev/null 2>&1; then
            log_success "${service_name} is healthy"
            return 0
        fi
        
        if [[ $i -eq ${timeout} ]]; then
            log_error "${service_name} health check failed after ${timeout} attempts"
            return 1
        fi
        
        log_info "Health check attempt ${i}/${timeout} failed, retrying in 2 seconds..."
        sleep 2
    done
}

check_container_status() {
    local env_color=$(get_active_environment)
    local compose_file="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}-${env_color}.yml"
    
    if [[ ! -f "${compose_file}" ]]; then
        compose_file="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}.yml"
    fi
    
    log_info "Checking container status for ${env_color} environment"
    
    # Get container status
    local containers
    containers=$(docker-compose -f "${compose_file}" -p "ats-${env_color}" ps -q 2>/dev/null || true)
    
    if [[ -z "${containers}" ]]; then
        log_error "No containers found for environment"
        return 1
    fi
    
    # Check each container
    local unhealthy_containers=0
    for container in ${containers}; do
        local container_name
        container_name=$(docker inspect --format='{{.Name}}' "${container}" | sed 's/^\///')
        
        local container_status
        container_status=$(docker inspect --format='{{.State.Status}}' "${container}")
        
        if [[ "${container_status}" != "running" ]]; then
            log_error "Container ${container_name} is not running (status: ${container_status})"
            ((unhealthy_containers++))
        else
            log_success "Container ${container_name} is running"
        fi
    done
    
    if [[ ${unhealthy_containers} -gt 0 ]]; then
        log_error "${unhealthy_containers} container(s) are not healthy"
        return 1
    fi
    
    log_success "All containers are running"
    return 0
}

check_resource_usage() {
    log_info "Checking resource usage"
    
    # Memory usage
    local memory_usage
    memory_usage=$(free | grep Mem | awk '{printf "%.1f", $3/$2 * 100.0}')
    log_info "Memory usage: ${memory_usage}%"
    
    if (( $(echo "${memory_usage} > 90" | bc -l) )); then
        log_warning "High memory usage detected: ${memory_usage}%"
    fi
    
    # Disk usage
    local disk_usage
    disk_usage=$(df / | tail -1 | awk '{print $5}' | sed 's/%//')
    log_info "Disk usage: ${disk_usage}%"
    
    if [[ ${disk_usage} -gt 85 ]]; then
        log_warning "High disk usage detected: ${disk_usage}%"
    fi
    
    # Load average
    local load_avg
    load_avg=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | sed 's/,//')
    log_info "Load average (1min): ${load_avg}"
    
    return 0
}

check_service_connectivity() {
    log_info "Checking inter-service connectivity"
    
    local base_url
    base_url=$(get_health_endpoints | sed 's|/health||')
    
    # Check gRPC service connectivity (if grpcurl is available)
    if command -v grpcurl >/dev/null 2>&1; then
        local grpc_port
        if [[ "${ENVIRONMENT}" == "staging" ]]; then
            grpc_port="60051"
        else
            grpc_port="50051"
        fi
        
        if grpcurl -plaintext "localhost:${grpc_port}" list >/dev/null 2>&1; then
            log_success "gRPC services are accessible"
        else
            log_warning "gRPC services may not be accessible"
        fi
    fi
    
    return 0
}

run_comprehensive_health_check() {
    log_info "Running comprehensive health check"
    
    local failed_checks=0
    
    # Container status check
    if ! check_container_status; then
        ((failed_checks++))
    fi
    
    # HTTP health endpoint check
    local health_url
    health_url=$(get_health_endpoints)
    if ! check_service_health "Main Application" "${health_url}" 10; then
        ((failed_checks++))
    fi
    
    # Resource usage check
    check_resource_usage
    
    # Service connectivity check
    check_service_connectivity
    
    if [[ ${failed_checks} -eq 0 ]]; then
        log_success "All health checks passed"
        return 0
    else
        log_error "${failed_checks} health check(s) failed"
        return 1
    fi
}

generate_health_report() {
    local timestamp
    timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    
    local report_file="${PROJECT_ROOT}/deployment/health-report-${ENVIRONMENT}-$(date +%s).json"
    
    log_info "Generating health report: ${report_file}"
    
    cat > "${report_file}" << EOF
{
  "timestamp": "${timestamp}",
  "environment": "${ENVIRONMENT}",
  "active_environment": "$(get_active_environment)",
  "health_check_passed": $(run_comprehensive_health_check >/dev/null 2>&1 && echo "true" || echo "false"),
  "endpoints": {
    "health_url": "$(get_health_endpoints)",
    "dashboard_url": "$(get_health_endpoints | sed 's|/health||')"
  },
  "system_info": {
    "memory_usage": "$(free | grep Mem | awk '{printf "%.1f", $3/$2 * 100.0}')%",
    "disk_usage": "$(df / | tail -1 | awk '{print $5}')",
    "load_average": "$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | sed 's/,//')"
  }
}
EOF
    
    log_info "Health report generated"
}

main() {
    log_info "Health check process started"
    
    if run_comprehensive_health_check; then
        log_success "Environment ${ENVIRONMENT} is healthy"
        generate_health_report
        exit 0
    else
        log_error "Environment ${ENVIRONMENT} has health issues"
        generate_health_report
        exit 1
    fi
}

main "$@"