#!/bin/bash

set -euo pipefail

ENVIRONMENT=${1:-production}
VERSION=${2:-latest}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/common.sh"

BLUE_COMPOSE_FILE="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}-blue.yml"
GREEN_COMPOSE_FILE="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}-green.yml"
NGINX_CONFIG_DIR="${PROJECT_ROOT}/deployment/nginx"
ACTIVE_ENV_FILE="${PROJECT_ROOT}/deployment/.active-environment"

log_info "Starting blue-green deployment for environment: ${ENVIRONMENT}, version: ${VERSION}"

get_active_environment() {
    if [[ -f "${ACTIVE_ENV_FILE}" ]]; then
        cat "${ACTIVE_ENV_FILE}"
    else
        echo "blue"
    fi
}

get_inactive_environment() {
    local active_env=$(get_active_environment)
    if [[ "${active_env}" == "blue" ]]; then
        echo "green"
    else
        echo "blue"
    fi
}

prepare_compose_file() {
    local env_color=$1
    local compose_file="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}-${env_color}.yml"
    
    log_info "Preparing compose file for ${env_color} environment"
    
    cp "${PROJECT_ROOT}/deployment/docker-compose.production.yml" "${compose_file}"
    
    # Update port mappings for the inactive environment
    if [[ "${env_color}" == "green" ]]; then
        sed -i 's/8080:8080/8081:8080/g' "${compose_file}"
        sed -i 's/50051:50051/50061:50051/g' "${compose_file}"
        sed -i 's/50052:50052/50062:50052/g' "${compose_file}"
        sed -i 's/50053:50053/50063:50053/g' "${compose_file}"
        sed -i 's/50054:50054/50064:50054/g' "${compose_file}"
        sed -i 's/50055:50055/50065:50055/g' "${compose_file}"
    fi
    
    # Set environment variables
    export GITHUB_REPOSITORY="ats-trading-system"
    export IMAGE_TAG="${VERSION}"
    
    envsubst < "${compose_file}" > "${compose_file}.tmp"
    mv "${compose_file}.tmp" "${compose_file}"
}

deploy_to_inactive() {
    local inactive_env=$(get_inactive_environment)
    local compose_file="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}-${inactive_env}.yml"
    
    log_info "Deploying to ${inactive_env} environment"
    
    prepare_compose_file "${inactive_env}"
    
    # Deploy to inactive environment
    docker-compose -f "${compose_file}" -p "ats-${inactive_env}" down --remove-orphans || true
    docker-compose -f "${compose_file}" -p "ats-${inactive_env}" pull
    docker-compose -f "${compose_file}" -p "ats-${inactive_env}" up -d
    
    log_info "Waiting for services to be ready in ${inactive_env} environment"
    sleep 30
    
    # Health check
    if [[ "${inactive_env}" == "green" ]]; then
        local health_url="http://localhost:8081/health"
    else
        local health_url="http://localhost:8080/health"
    fi
    
    for i in {1..30}; do
        if curl -f "${health_url}" >/dev/null 2>&1; then
            log_info "Health check passed for ${inactive_env} environment"
            return 0
        fi
        log_info "Health check attempt ${i}/30 failed, retrying in 10 seconds..."
        sleep 10
    done
    
    log_error "Health check failed for ${inactive_env} environment"
    return 1
}

switch_traffic() {
    local inactive_env=$(get_inactive_environment)
    local active_env=$(get_active_environment)
    
    log_info "Switching traffic from ${active_env} to ${inactive_env}"
    
    # Update nginx configuration
    if [[ "${inactive_env}" == "green" ]]; then
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
    echo "${inactive_env}" > "${ACTIVE_ENV_FILE}"
    
    log_info "Traffic switched to ${inactive_env} environment"
}

cleanup_old_environment() {
    local old_env=$(get_inactive_environment)
    local compose_file="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}-${old_env}.yml"
    
    log_info "Cleaning up old ${old_env} environment"
    
    # Wait before cleanup to ensure traffic has switched
    sleep 60
    
    docker-compose -f "${compose_file}" -p "ats-${old_env}" down --remove-orphans || true
    
    log_info "Cleanup completed for ${old_env} environment"
}

main() {
    log_info "Blue-Green deployment started"
    
    # Create nginx config directory if it doesn't exist
    mkdir -p "${NGINX_CONFIG_DIR}"
    
    # Create nginx template if it doesn't exist
    if [[ ! -f "${NGINX_CONFIG_DIR}/nginx.conf.template" ]]; then
        cat > "${NGINX_CONFIG_DIR}/nginx.conf.template" << 'EOF'
upstream ats-backend {
    server localhost:8080;
}

server {
    listen 80;
    server_name _;
    
    location / {
        proxy_pass http://ats-backend;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
    
    location /health {
        proxy_pass http://ats-backend/health;
        access_log off;
    }
}
EOF
    fi
    
    if deploy_to_inactive; then
        switch_traffic
        cleanup_old_environment
        log_info "Blue-Green deployment completed successfully"
    else
        log_error "Blue-Green deployment failed during health check"
        exit 1
    fi
}

main "$@"