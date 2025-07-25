#!/bin/bash

# Common functions for deployment scripts

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" >&2
}

check_dependencies() {
    local deps=("docker" "docker-compose" "curl")
    
    for dep in "${deps[@]}"; do
        if ! command -v "${dep}" >/dev/null 2>&1; then
            log_error "Required dependency '${dep}' is not installed"
            exit 1
        fi
    done
    
    log_info "All dependencies are available"
}

cleanup_on_exit() {
    local exit_code=$?
    if [[ ${exit_code} -ne 0 ]]; then
        log_error "Script exited with error code ${exit_code}"
    fi
}

# Set up cleanup trap
trap cleanup_on_exit EXIT

# Check dependencies on source
check_dependencies