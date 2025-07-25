#!/bin/bash

set -euo pipefail

ENVIRONMENT=${1:-staging}
VERSION=${2:-latest}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/common.sh"

COMPOSE_FILE="${PROJECT_ROOT}/deployment/docker-compose.${ENVIRONMENT}.yml"

log_info "Starting deployment for environment: ${ENVIRONMENT}, version: ${VERSION}"

prepare_environment() {
    log_info "Preparing environment variables"
    
    export GITHUB_REPOSITORY="ats-trading-system"
    export IMAGE_TAG="${VERSION}"
    
    # Create environment-specific compose file if it doesn't exist
    if [[ ! -f "${COMPOSE_FILE}" ]]; then
        cp "${PROJECT_ROOT}/deployment/docker-compose.production.yml" "${COMPOSE_FILE}"
        
        # Adjust ports for staging environment
        if [[ "${ENVIRONMENT}" == "staging" ]]; then
            sed -i 's/8080:8080/9080:8080/g' "${COMPOSE_FILE}"
            sed -i 's/50051:50051/60051:50051/g' "${COMPOSE_FILE}"
            sed -i 's/50052:50052/60052:50052/g' "${COMPOSE_FILE}"
            sed -i 's/50053:50053/60053:50053/g' "${COMPOSE_FILE}"
            sed -i 's/50054:50054/60054:50054/g' "${COMPOSE_FILE}"
            sed -i 's/50055:50055/60055:50055/g' "${COMPOSE_FILE}"
        fi
    fi
    
    # Substitute environment variables
    envsubst < "${COMPOSE_FILE}" > "${COMPOSE_FILE}.tmp"
    mv "${COMPOSE_FILE}.tmp" "${COMPOSE_FILE}"
}

deploy_services() {
    log_info "Deploying services to ${ENVIRONMENT}"
    
    # Stop existing services
    docker-compose -f "${COMPOSE_FILE}" -p "ats-${ENVIRONMENT}" down --remove-orphans || true
    
    # Pull latest images
    docker-compose -f "${COMPOSE_FILE}" -p "ats-${ENVIRONMENT}" pull
    
    # Start services
    docker-compose -f "${COMPOSE_FILE}" -p "ats-${ENVIRONMENT}" up -d
    
    log_info "Services deployed successfully"
}

wait_for_services() {
    log_info "Waiting for services to be ready"
    
    local health_port
    if [[ "${ENVIRONMENT}" == "staging" ]]; then
        health_port="9080"
    else
        health_port="8080"
    fi
    
    local health_url="http://localhost:${health_port}/health"
    
    for i in {1..30}; do
        if curl -f "${health_url}" >/dev/null 2>&1; then
            log_info "All services are ready"
            return 0
        fi
        log_info "Health check attempt ${i}/30 failed, retrying in 10 seconds..."
        sleep 10
    done
    
    log_error "Services failed to become ready within timeout"
    return 1
}

main() {
    log_info "Standard deployment started"
    
    prepare_environment
    deploy_services
    
    if wait_for_services; then
        log_info "Deployment completed successfully"
    else
        log_error "Deployment failed during health check"
        exit 1
    fi
}

main "$@"