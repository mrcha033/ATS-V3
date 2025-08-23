#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/common.sh"

log_info "Verifying ATS monitoring system setup"

# Configuration
PROMETHEUS_URL="${PROMETHEUS_URL:-http://localhost:9090}"
GRAFANA_URL="${GRAFANA_URL:-http://localhost:3000}"
GRAFANA_USER="${GRAFANA_USER:-admin}"
GRAFANA_PASS="${GRAFANA_PASS:-ats-admin}"

# Verification results
VERIFICATION_RESULTS=()

# Function to add verification result
add_result() {
    local status=$1
    local component=$2
    local message=$3
    
    VERIFICATION_RESULTS+=("${status}|${component}|${message}")
    
    if [ "${status}" = "PASS" ]; then
        log_success "✓ ${component}: ${message}"
    elif [ "${status}" = "WARN" ]; then
        log_warning "⚠ ${component}: ${message}"
    else
        log_error "✗ ${component}: ${message}"
    fi
}

# Verify Prometheus is running and accessible
verify_prometheus() {
    log_info "Verifying Prometheus setup..."
    
    # Check if Prometheus is accessible
    if curl -s "${PROMETHEUS_URL}/api/v1/status/config" > /dev/null 2>&1; then
        add_result "PASS" "Prometheus" "Service is accessible at ${PROMETHEUS_URL}"
    else
        add_result "FAIL" "Prometheus" "Service not accessible at ${PROMETHEUS_URL}"
        return 1
    fi
    
    # Check if targets are being scraped
    local targets_response
    targets_response=$(curl -s "${PROMETHEUS_URL}/api/v1/targets" | jq -r '.data.activeTargets | length' 2>/dev/null || echo "0")
    
    if [ "${targets_response}" -gt "0" ]; then
        add_result "PASS" "Prometheus Targets" "${targets_response} active targets found"
        
        # List target health
        curl -s "${PROMETHEUS_URL}/api/v1/targets" | jq -r '.data.activeTargets[] | "\(.labels.job):\(.labels.instance) - \(.health)"' | while IFS= read -r target; do
            log_info "  Target: ${target}"
        done
    else
        add_result "WARN" "Prometheus Targets" "No active targets found"
    fi
    
    # Check if ATS metrics are available
    local ats_metrics
    ats_metrics=$(curl -s "${PROMETHEUS_URL}/api/v1/label/__name__/values" | jq -r '.data[] | select(startswith("ats_"))' | wc -l 2>/dev/null || echo "0")
    
    if [ "${ats_metrics}" -gt "0" ]; then
        add_result "PASS" "ATS Metrics" "${ats_metrics} ATS metrics available"
    else
        add_result "WARN" "ATS Metrics" "No ATS-specific metrics found"
    fi
}

# Verify Grafana is running and dashboards are available
verify_grafana() {
    log_info "Verifying Grafana setup..."
    
    # Check if Grafana is accessible
    if curl -s "${GRAFANA_URL}/api/health" > /dev/null 2>&1; then
        add_result "PASS" "Grafana" "Service is accessible at ${GRAFANA_URL}"
    else
        add_result "FAIL" "Grafana" "Service not accessible at ${GRAFANA_URL}"
        return 1
    fi
    
    # Check if Prometheus datasource is configured
    local datasource_response
    datasource_response=$(curl -s -u "${GRAFANA_USER}:${GRAFANA_PASS}" "${GRAFANA_URL}/api/datasources" | jq -r '.[] | select(.type == "prometheus") | .name' 2>/dev/null || echo "")
    
    if [ -n "${datasource_response}" ]; then
        add_result "PASS" "Grafana Datasource" "Prometheus datasource configured: ${datasource_response}"
    else
        add_result "WARN" "Grafana Datasource" "No Prometheus datasource found"
    fi
    
    # Check if ATS dashboards exist
    local dashboards=("ats-overview" "ats-trading-performance")
    local dashboard_count=0
    
    for dashboard_uid in "${dashboards[@]}"; do
        if curl -s -u "${GRAFANA_USER}:${GRAFANA_PASS}" "${GRAFANA_URL}/api/dashboards/uid/${dashboard_uid}" > /dev/null 2>&1; then
            add_result "PASS" "Dashboard" "${dashboard_uid} is available"
            ((dashboard_count++))
        else
            add_result "WARN" "Dashboard" "${dashboard_uid} not found"
        fi
    done
    
    if [ "${dashboard_count}" -eq "${#dashboards[@]}" ]; then
        add_result "PASS" "Grafana Dashboards" "All ${dashboard_count} ATS dashboards available"
    else
        add_result "WARN" "Grafana Dashboards" "Only ${dashboard_count}/${#dashboards[@]} dashboards available"
    fi
}

# Verify Alertmanager is running and configured
verify_alertmanager() {
    log_info "Verifying Alertmanager setup..."
    
    local alertmanager_url="http://localhost:9093"
    
    # Check if Alertmanager is accessible
    if curl -s "${alertmanager_url}/api/v1/status" > /dev/null 2>&1; then
        add_result "PASS" "Alertmanager" "Service is accessible at ${alertmanager_url}"
        
        # Check alert rules
        local rules_response
        rules_response=$(curl -s "${PROMETHEUS_URL}/api/v1/rules" | jq -r '.data.groups | length' 2>/dev/null || echo "0")
        
        if [ "${rules_response}" -gt "0" ]; then
            add_result "PASS" "Alert Rules" "${rules_response} rule groups configured"
        else
            add_result "WARN" "Alert Rules" "No alert rules found"
        fi
        
    else
        add_result "WARN" "Alertmanager" "Service not accessible at ${alertmanager_url}"
    fi
}

# Verify Docker containers are running
verify_docker_containers() {
    log_info "Verifying Docker containers..."
    
    local expected_containers=("prometheus" "grafana" "ats-v3")
    local running_count=0
    
    for container in "${expected_containers[@]}"; do
        if docker ps --format "table {{.Names}}" | grep -q "${container}"; then
            add_result "PASS" "Container" "${container} is running"
            ((running_count++))
        else
            add_result "WARN" "Container" "${container} not found or not running"
        fi
    done
    
    # Check total container count
    local total_containers
    total_containers=$(docker ps -q | wc -l)
    add_result "PASS" "Docker Status" "${total_containers} total containers running (${running_count} expected containers found)"
}

# Verify configuration files exist
verify_configuration_files() {
    log_info "Verifying configuration files..."
    
    local config_files=(
        "monitoring/prometheus.yml"
        "deployment/prometheus/prometheus.yml"
        "deployment/prometheus/alerts.yml"
        "deployment/grafana/grafana.ini"
        "deployment/grafana/dashboards/ats-overview.json"
        "deployment/grafana/dashboards/ats-trading-performance.json"
    )
    
    local found_count=0
    
    for config_file in "${config_files[@]}"; do
        local full_path="${PROJECT_ROOT}/${config_file}"
        if [ -f "${full_path}" ]; then
            add_result "PASS" "Config File" "${config_file} exists"
            ((found_count++))
        else
            add_result "WARN" "Config File" "${config_file} not found"
        fi
    done
    
    add_result "PASS" "Configuration" "${found_count}/${#config_files[@]} configuration files found"
}

# Verify code integration (Prometheus exporters)
verify_code_integration() {
    log_info "Verifying code integration..."
    
    # Check if Prometheus exporter code exists
    if [ -f "${PROJECT_ROOT}/shared/include/utils/prometheus_exporter.hpp" ]; then
        add_result "PASS" "Code Integration" "Prometheus exporter header found"
    else
        add_result "FAIL" "Code Integration" "Prometheus exporter header not found"
    fi
    
    if [ -f "${PROJECT_ROOT}/shared/src/utils/prometheus_exporter.cpp" ]; then
        add_result "PASS" "Code Integration" "Prometheus exporter implementation found"
    else
        add_result "FAIL" "Code Integration" "Prometheus exporter implementation not found"
    fi
    
    # Check if trading engine has Prometheus integration
    if grep -q "prometheus_exporter" "${PROJECT_ROOT}/trading_engine/include/trading_engine_service.hpp" 2>/dev/null; then
        add_result "PASS" "Code Integration" "Trading engine has Prometheus integration"
    else
        add_result "WARN" "Code Integration" "Trading engine Prometheus integration not found"
    fi
    
    # Check CMakeLists for prometheus-cpp dependency
    if grep -q "prometheus-cpp" "${PROJECT_ROOT}/conanfile.txt" 2>/dev/null; then
        add_result "PASS" "Dependencies" "prometheus-cpp dependency found in conanfile.txt"
    else
        add_result "FAIL" "Dependencies" "prometheus-cpp dependency not found"
    fi
}

# Test basic metric queries
test_metric_queries() {
    log_info "Testing basic metric queries..."
    
    local test_queries=(
        "up"
        "ats_service_health"
        "ats_successful_trades_total"
        "ats_cpu_usage_percent"
    )
    
    local successful_queries=0
    
    for query in "${test_queries[@]}"; do
        if curl -s "${PROMETHEUS_URL}/api/v1/query?query=${query}" | jq -e '.data.result | length > 0' > /dev/null 2>&1; then
            add_result "PASS" "Query Test" "${query} returns data"
            ((successful_queries++))
        else
            add_result "WARN" "Query Test" "${query} returns no data"
        fi
    done
    
    add_result "PASS" "Query Tests" "${successful_queries}/${#test_queries[@]} queries successful"
}

# Generate verification report
generate_verification_report() {
    log_info "Generating verification report..."
    
    local timestamp
    timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    
    local total_checks=0
    local passed_checks=0
    local warning_checks=0
    local failed_checks=0
    
    cat > "${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md" << EOF
# ATS Monitoring System Verification Report

**Generated:** ${timestamp}
**System:** $(uname -a)
**Script:** $0

## Verification Summary

EOF
    
    # Process results
    for result in "${VERIFICATION_RESULTS[@]}"; do
        IFS='|' read -r status component message <<< "${result}"
        ((total_checks++))
        
        case "${status}" in
            "PASS") ((passed_checks++));;
            "WARN") ((warning_checks++));;
            "FAIL") ((failed_checks++));;
        esac
        
        echo "- **${status}** ${component}: ${message}" >> "${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md"
    done
    
    cat >> "${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md" << EOF

## Results Overview

| Status | Count | Percentage |
|--------|-------|------------|
| ✓ PASS | ${passed_checks} | $(( passed_checks * 100 / total_checks ))% |
| ⚠ WARN | ${warning_checks} | $(( warning_checks * 100 / total_checks ))% |
| ✗ FAIL | ${failed_checks} | $(( failed_checks * 100 / total_checks ))% |
| **Total** | **${total_checks}** | **100%** |

## Recommendations

EOF
    
    if [ "${failed_checks}" -gt 0 ]; then
        echo "### Critical Issues (Must Fix)" >> "${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md"
        echo "- Address all FAIL status items before proceeding to production" >> "${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md"
        echo "" >> "${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md"
    fi
    
    if [ "${warning_checks}" -gt 0 ]; then
        echo "### Warnings (Should Fix)" >> "${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md"
        echo "- Review WARN status items for optimal monitoring coverage" >> "${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md"
        echo "" >> "${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md"
    fi
    
    cat >> "${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md" << EOF
### Next Steps

1. **If all critical checks pass**: System is ready for monitoring
2. **Run load test**: Execute \`./load-test-monitoring.sh\` to validate under load
3. **Test alerts**: Trigger test alerts to verify notification delivery
4. **Monitor real usage**: Observe system behavior with actual trading activity

## Service URLs

- **Prometheus**: ${PROMETHEUS_URL}
- **Grafana**: ${GRAFANA_URL} (admin/ats-admin)
- **Alertmanager**: http://localhost:9093

---
*Generated by ATS Monitoring Verification Script*
EOF
    
    log_success "Verification report generated: ${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md"
}

# Print summary and recommendations
print_summary() {
    local total_checks=${#VERIFICATION_RESULTS[@]}
    local passed_checks=0
    local warning_checks=0
    local failed_checks=0
    
    # Count results
    for result in "${VERIFICATION_RESULTS[@]}"; do
        IFS='|' read -r status component message <<< "${result}"
        case "${status}" in
            "PASS") ((passed_checks++));;
            "WARN") ((warning_checks++));;
            "FAIL") ((failed_checks++));;
        esac
    done
    
    echo ""
    log_info "=== VERIFICATION SUMMARY ==="
    log_info "Total checks: ${total_checks}"
    log_success "Passed: ${passed_checks} ($(( passed_checks * 100 / total_checks ))%)"
    
    if [ "${warning_checks}" -gt 0 ]; then
        log_warning "Warnings: ${warning_checks} ($(( warning_checks * 100 / total_checks ))%)"
    fi
    
    if [ "${failed_checks}" -gt 0 ]; then
        log_error "Failed: ${failed_checks} ($(( failed_checks * 100 / total_checks ))%)"
        echo ""
        log_error "❌ VERIFICATION FAILED: ${failed_checks} critical issues found"
        log_info "Review the report and fix all FAIL items before proceeding"
        return 1
    else
        echo ""
        log_success "✅ MONITORING SYSTEM VERIFICATION PASSED!"
        
        if [ "${warning_checks}" -gt 0 ]; then
            log_info "📋 ${warning_checks} warnings detected - review for optimal setup"
        fi
        
        log_info "🚀 System is ready for monitoring operations"
    fi
    
    echo ""
    log_info "📊 View full report: cat ${PROJECT_ROOT}/deployment/logs/monitoring_verification_report.md"
    log_info "🔧 Next: Run load test with ./load-test-monitoring.sh"
    
    return 0
}

# Main verification function
main() {
    log_info "Starting ATS monitoring system verification"
    log_info "Prometheus: ${PROMETHEUS_URL}"
    log_info "Grafana: ${GRAFANA_URL}"
    
    # Ensure logs directory exists
    mkdir -p "${PROJECT_ROOT}/deployment/logs"
    
    # Run all verification checks
    verify_docker_containers
    verify_configuration_files
    verify_code_integration
    verify_prometheus
    verify_grafana
    verify_alertmanager
    test_metric_queries
    
    # Generate report and show summary
    generate_verification_report
    print_summary
}

main "$@"