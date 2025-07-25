#!/bin/bash

set -euo pipefail

ENVIRONMENT=${1:-production}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/common.sh"

ACTIVE_ENV_FILE="${PROJECT_ROOT}/deployment/.active-environment"
DEPLOYMENT_HISTORY_FILE="${PROJECT_ROOT}/deployment/.deployment-history"
NGINX_CONFIG_DIR="${PROJECT_ROOT}/deployment/nginx"

log_info "Starting rollback for environment: ${ENVIRONMENT}"

get_current_environment() {
    if [[ -f "${ACTIVE_ENV_FILE}" ]]; then
        cat "${ACTIVE_ENV_FILE}"
    else
        echo "blue"
    fi
}

get_previous_environment() {
    local current_env=$(get_current_environment)
    if [[ "${current_env}" == "blue" ]]; then
        echo "green"
    else
        echo "blue"
    fi
}

get_previous_version() {
    if [[ -f "${DEPLOYMENT_HISTORY_FILE}" ]]; then
        # Get the second most recent deployment (previous version)
        tail -n 2 "${DEPLOYMENT_HISTORY_FILE}" | head -n 1 | cut -d'|' -f3
    else
        log_warning "No deployment history found, using 'latest' as fallback"
        echo "latest"
    fi
}

record_rollback() {
    local env=$1
    local version=$2
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    
    echo "ROLLBACK|${env}|${version}|${timestamp}" >> "${DEPLOYMENT_HISTORY_FILE}"
    log_info "Rollback recorded in deployment history"
}

check_previous_environment() {
    local prev_env=$(get_previous_environment)
    local compose_file="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}-${prev_env}.yml"
    
    log_info "Checking if previous environment (${prev_env}) is available"
    
    if [[ ! -f "${compose_file}" ]]; then
        log_error "Previous environment compose file not found: ${compose_file}"
        return 1
    fi
    
    # Check if containers exist
    local containers=$(docker-compose -f "${compose_file}" -p "ats-${prev_env}" ps -q 2>/dev/null || true)
    if [[ -z "${containers}" ]]; then
        log_warning "No containers found for previous environment, attempting to recreate"
        return 2
    fi
    
    # Check if containers are healthy
    local health_port
    if [[ "${prev_env}" == "green" ]]; then
        health_port="8081"
    else
        health_port="8080"
    fi
    
    local health_url="http://localhost:${health_port}/health"
    if curl -f "${health_url}" >/dev/null 2>&1; then
        log_info "Previous environment is healthy and ready for rollback"
        return 0
    else
        log_warning "Previous environment exists but is not healthy"
        return 2
    fi
}

recreate_previous_environment() {
    local prev_env=$(get_previous_environment)
    local prev_version=$(get_previous_version)
    local compose_file="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}-${prev_env}.yml"
    
    log_info "Recreating previous environment (${prev_env}) with version ${prev_version}"
    
    # Set environment variables
    export GITHUB_REPOSITORY="ats-trading-system"
    export IMAGE_TAG="${prev_version}"
    
    # Substitute environment variables
    envsubst < "${compose_file}" > "${compose_file}.tmp"
    mv "${compose_file}.tmp" "${compose_file}"
    
    # Deploy previous version
    docker-compose -f "${compose_file}" -p "ats-${prev_env}" down --remove-orphans || true
    docker-compose -f "${compose_file}" -p "ats-${prev_env}" pull
    docker-compose -f "${compose_file}" -p "ats-${prev_env}" up -d
    
    log_info "Waiting for previous environment to be ready"
    sleep 30
    
    # Health check
    local health_port
    if [[ "${prev_env}" == "green" ]]; then
        health_port="8081"
    else
        health_port="8080"
    fi
    
    local health_url="http://localhost:${health_port}/health"
    for i in {1..20}; do
        if curl -f "${health_url}" >/dev/null 2>&1; then
            log_info "Previous environment is ready"
            return 0
        fi
        log_info "Health check attempt ${i}/20 failed, retrying in 10 seconds..."
        sleep 10
    done
    
    log_error "Failed to recreate healthy previous environment"
    return 1
}

switch_traffic_back() {
    local prev_env=$(get_previous_environment)
    local current_env=$(get_current_environment)
    
    log_info "Switching traffic back from ${current_env} to ${prev_env}"
    
    # Create backup of current nginx config
    if [[ -f "${NGINX_CONFIG_DIR}/nginx.conf" ]]; then
        cp "${NGINX_CONFIG_DIR}/nginx.conf" "${NGINX_CONFIG_DIR}/nginx.conf.backup.$(date +%s)"
    fi
    
    # Update nginx configuration
    if [[ "${prev_env}" == "green" ]]; then
        sed 's/upstream ats-backend { server localhost:8080; }/upstream ats-backend { server localhost:8081; }/' \
            "${NGINX_CONFIG_DIR}/nginx.conf.template" > "${NGINX_CONFIG_DIR}/nginx.conf"
    else
        sed 's/upstream ats-backend { server localhost:8081; }/upstream ats-backend { server localhost:8080; }/' \
            "${NGINX_CONFIG_DIR}/nginx.conf.template" > "${NGINX_CONFIG_DIR}/nginx.conf"
    fi
    
    # Reload nginx
    if command -v nginx >/dev/null 2>&1; then
        nginx -s reload
    elif command -v systemctl >/dev/null 2>&1; then
        systemctl reload nginx
    fi
    
    # Update active environment file
    echo "${prev_env}" > "${ACTIVE_ENV_FILE}"
    
    log_success "Traffic switched back to ${prev_env} environment"
}

cleanup_failed_environment() {
    local failed_env=$(get_previous_environment)
    local compose_file="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}-${failed_env}.yml"
    
    log_info "Cleaning up failed environment (${failed_env})"
    
    # Wait a bit before cleanup
    sleep 30
    
    docker-compose -f "${compose_file}" -p "ats-${failed_env}" down --remove-orphans || true
    
    log_info "Failed environment cleanup completed"
}

send_rollback_notification() {
    local prev_env=$1
    local prev_version=$2
    
    log_info "Sending rollback notification"
    
    # You can integrate with your notification system here (Slack, email, etc.)
    local message="🔄 ROLLBACK COMPLETED: Environment ${ENVIRONMENT} rolled back to ${prev_env} (version: ${prev_version})"
    
    # Example webhook notification (uncomment and configure as needed)
    # curl -X POST -H 'Content-type: application/json' \
    #     --data "{\"text\":\"${message}\"}" \
    #     "${SLACK_WEBHOOK_URL}" || true
    
    log_success "${message}"
}

main() {
    log_info "Rollback process started"
    
    local prev_env=$(get_previous_environment)
    local prev_version=$(get_previous_version)
    
    log_info "Rolling back from $(get_current_environment) to ${prev_env} (version: ${prev_version})"
    
    # Check previous environment status
    local env_status
    check_previous_environment
    env_status=$?
    
    case ${env_status} in
        0)
            log_info "Previous environment is ready, proceeding with traffic switch"
            ;;
        2)
            log_info "Previous environment needs to be recreated"
            if ! recreate_previous_environment; then
                log_error "Failed to recreate previous environment"
                exit 1
            fi
            ;;
        *)
            log_error "Cannot proceed with rollback - previous environment is not available"
            exit 1
            ;;
    esac
    
    # Switch traffic back
    switch_traffic_back
    
    # Cleanup failed environment
    cleanup_failed_environment
    
    # Record rollback
    record_rollback "${prev_env}" "${prev_version}"
    
    # Send notification
    send_rollback_notification "${prev_env}" "${prev_version}"
    
    log_success "Rollback completed successfully"
}

main "$@"