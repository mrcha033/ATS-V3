#!/bin/bash

# Exchange API Compatibility Test Runner
# This script runs the exchange compatibility tests and integrates with CI/CD

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
PYTHON_SCRIPT="$SCRIPT_DIR/exchange_compatibility_test.py"
REPORT_DIR="$PROJECT_ROOT/reports/exchange_compatibility"
ALERT_WEBHOOK_URL="${EXCHANGE_ALERT_WEBHOOK_URL:-}"
SLACK_WEBHOOK_URL="${SLACK_WEBHOOK_URL:-}"
EMAIL_RECIPIENTS="${EMAIL_RECIPIENTS:-}"
FAIL_ON_WARNINGS="${FAIL_ON_WARNINGS:-false}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# Create report directory
create_report_dir() {
    mkdir -p "$REPORT_DIR"
    log_info "Report directory created: $REPORT_DIR"
}

# Install Python dependencies if needed
install_dependencies() {
    log_info "Installing Python dependencies..."
    
    if ! command -v python3 &> /dev/null; then
        log_error "Python 3 is not installed"
        exit 1
    fi
    
    # Check if pip is available
    if ! python3 -m pip --version &> /dev/null; then
        log_error "pip is not available"
        exit 1
    fi
    
    # Install required packages
    python3 -m pip install --quiet aiohttp asyncio requests || {
        log_error "Failed to install Python dependencies"
        exit 1
    }
    
    log_success "Python dependencies installed"
}

# Run the compatibility tests
run_compatibility_tests() {
    log_info "Starting exchange compatibility tests..."
    
    cd "$SCRIPT_DIR"
    
    # Set timeout for the test execution
    timeout 300 python3 "$PYTHON_SCRIPT" || {
        local exit_code=$?
        case $exit_code in
            1)
                log_error "Critical issues detected in exchange APIs"
                return 1
                ;;
            2)
                log_warning "Performance issues detected in exchange APIs"
                if [ "$FAIL_ON_WARNINGS" = "true" ]; then
                    return 2
                else
                    return 0
                fi
                ;;
            3)
                log_error "Test execution failed"
                return 3
                ;;
            124)
                log_error "Test execution timed out"
                return 124
                ;;
            *)
                log_error "Unknown error occurred (exit code: $exit_code)"
                return $exit_code
                ;;
        esac
    }
    
    log_success "Exchange compatibility tests completed successfully"
    return 0
}

# Move report to report directory
archive_report() {
    if [ -f "$SCRIPT_DIR/exchange_compatibility_report.json" ]; then
        local timestamp=$(date +"%Y%m%d_%H%M%S")
        local report_file="$REPORT_DIR/exchange_compatibility_report_$timestamp.json"
        
        cp "$SCRIPT_DIR/exchange_compatibility_report.json" "$report_file"
        log_info "Report archived to: $report_file"
        
        # Keep only the last 30 reports
        find "$REPORT_DIR" -name "exchange_compatibility_report_*.json" -type f | \
            sort -r | tail -n +31 | xargs -r rm -f
        
        log_info "Old reports cleaned up (keeping last 30)"
    else
        log_warning "No report file found to archive"
    fi
}

# Parse the report and extract key information
parse_report() {
    local report_file="$SCRIPT_DIR/exchange_compatibility_report.json"
    
    if [ ! -f "$report_file" ]; then
        log_error "Report file not found: $report_file"
        return 1
    fi
    
    # Use jq to parse the JSON report if available
    if command -v jq &> /dev/null; then
        local total_exchanges=$(jq -r '.summary.total_exchanges_tested' "$report_file")
        local healthy_exchanges=$(jq -r '.summary.exchanges_healthy' "$report_file")
        local degraded_exchanges=$(jq -r '.summary.exchanges_degraded' "$report_file")
        local critical_exchanges=$(jq -r '.summary.exchanges_critical' "$report_file")
        local total_issues=$(jq -r '.summary.total_issues_found' "$report_file")
        local total_warnings=$(jq -r '.summary.total_warnings' "$report_file")
        
        echo "EXCHANGE_TOTAL=$total_exchanges"
        echo "EXCHANGE_HEALTHY=$healthy_exchanges"
        echo "EXCHANGE_DEGRADED=$degraded_exchanges"
        echo "EXCHANGE_CRITICAL=$critical_exchanges"
        echo "ISSUES_FOUND=$total_issues"
        echo "WARNINGS_FOUND=$total_warnings"
        
        return 0
    else
        log_warning "jq not available - cannot parse report details"
        return 1
    fi
}

# Send alert notifications
send_alerts() {
    local exit_code=$1
    local report_summary="$2"
    
    if [ $exit_code -eq 0 ]; then
        return 0  # No alerts needed for successful tests
    fi
    
    local message=""
    local severity=""
    
    case $exit_code in
        1)
            severity="CRITICAL"
            message="🚨 CRITICAL: Exchange API compatibility test failures detected!"
            ;;
        2)
            severity="WARNING"
            message="⚠️ WARNING: Exchange API performance issues detected"
            ;;
        *)
            severity="ERROR"
            message="❌ ERROR: Exchange compatibility tests failed to execute"
            ;;
    esac
    
    # Add report summary if available
    if [ -n "$report_summary" ]; then
        message="$message\n\nSummary:\n$report_summary"
    fi
    
    # Add timestamp and environment info
    message="$message\n\nTimestamp: $(date)\nEnvironment: ${CI_ENVIRONMENT:-local}\nBranch: ${CI_COMMIT_REF_NAME:-$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo 'unknown')}"
    
    # Send Slack notification
    if [ -n "$SLACK_WEBHOOK_URL" ]; then
        send_slack_notification "$message" "$severity"
    fi
    
    # Send webhook notification
    if [ -n "$ALERT_WEBHOOK_URL" ]; then
        send_webhook_notification "$message" "$severity"
    fi
    
    # Send email notification
    if [ -n "$EMAIL_RECIPIENTS" ]; then
        send_email_notification "$message" "$severity"
    fi
}

# Send Slack notification
send_slack_notification() {
    local message="$1"
    local severity="$2"
    
    local color="danger"
    if [ "$severity" = "WARNING" ]; then
        color="warning"
    elif [ "$severity" = "ERROR" ]; then
        color="danger"
    fi
    
    local payload=$(cat <<EOF
{
    "attachments": [
        {
            "color": "$color",
            "title": "Exchange API Compatibility Test Alert",
            "text": "$message",
            "ts": $(date +%s)
        }
    ]
}
EOF
)
    
    curl -X POST -H 'Content-type: application/json' \
        --data "$payload" \
        "$SLACK_WEBHOOK_URL" &> /dev/null || {
        log_warning "Failed to send Slack notification"
    }
    
    log_info "Slack notification sent"
}

# Send webhook notification
send_webhook_notification() {
    local message="$1"
    local severity="$2"
    
    local payload=$(cat <<EOF
{
    "alert_type": "exchange_compatibility",
    "severity": "$severity",
    "message": "$message",
    "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
    "source": "ci_compatibility_test"
}
EOF
)
    
    curl -X POST -H 'Content-type: application/json' \
        --data "$payload" \
        "$ALERT_WEBHOOK_URL" &> /dev/null || {
        log_warning "Failed to send webhook notification"
    }
    
    log_info "Webhook notification sent"
}

# Send email notification (requires mail command)
send_email_notification() {
    local message="$1"
    local severity="$2"
    
    if ! command -v mail &> /dev/null; then
        log_warning "mail command not available - skipping email notification"
        return
    fi
    
    local subject="[$severity] Exchange API Compatibility Test Alert"
    
    echo -e "$message" | mail -s "$subject" "$EMAIL_RECIPIENTS" || {
        log_warning "Failed to send email notification"
    }
    
    log_info "Email notification sent to: $EMAIL_RECIPIENTS"
}

# Generate CI artifacts
generate_ci_artifacts() {
    local report_file="$SCRIPT_DIR/exchange_compatibility_report.json"
    
    if [ ! -f "$report_file" ]; then
        return
    fi
    
    # Create artifacts directory if it doesn't exist
    local artifacts_dir="$PROJECT_ROOT/artifacts"
    mkdir -p "$artifacts_dir"
    
    # Copy report to artifacts
    cp "$report_file" "$artifacts_dir/exchange_compatibility_report.json"
    
    # Generate a simple HTML report if jq is available
    if command -v jq &> /dev/null; then
        generate_html_report "$report_file" "$artifacts_dir/exchange_compatibility_report.html"
    fi
    
    log_info "CI artifacts generated in: $artifacts_dir"
}

# Generate HTML report
generate_html_report() {
    local json_file="$1"
    local html_file="$2"
    
    cat > "$html_file" <<'EOF'
<!DOCTYPE html>
<html>
<head>
    <title>Exchange API Compatibility Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .header { background-color: #f0f0f0; padding: 20px; border-radius: 5px; }
        .summary { display: flex; justify-content: space-between; margin: 20px 0; }
        .metric { text-align: center; padding: 10px; border-radius: 5px; }
        .healthy { background-color: #d4edda; color: #155724; }
        .warning { background-color: #fff3cd; color: #856404; }
        .critical { background-color: #f8d7da; color: #721c24; }
        .exchange { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }
        .status-healthy { border-left: 5px solid #28a745; }
        .status-degraded { border-left: 5px solid #ffc107; }
        .status-critical { border-left: 5px solid #dc3545; }
        pre { background-color: #f8f9fa; padding: 10px; border-radius: 3px; overflow-x: auto; }
    </style>
</head>
<body>
    <div class="header">
        <h1>Exchange API Compatibility Report</h1>
        <p>Generated: <span id="timestamp"></span></p>
    </div>
    
    <div class="summary" id="summary">
        <!-- Summary will be populated by JavaScript -->
    </div>
    
    <div id="exchanges">
        <!-- Exchange details will be populated by JavaScript -->
    </div>
    
    <script>
        // Load and display the report data
        const reportData = 
EOF
    
    cat "$json_file" >> "$html_file"
    
    cat >> "$html_file" <<'EOF'
        ;
        
        // Populate timestamp
        document.getElementById('timestamp').textContent = reportData.timestamp;
        
        // Populate summary
        const summary = reportData.summary;
        document.getElementById('summary').innerHTML = `
            <div class="metric healthy">
                <h3>${summary.exchanges_healthy}</h3>
                <p>Healthy</p>
            </div>
            <div class="metric warning">
                <h3>${summary.exchanges_degraded}</h3>
                <p>Degraded</p>
            </div>
            <div class="metric critical">
                <h3>${summary.exchanges_critical}</h3>
                <p>Critical</p>
            </div>
            <div class="metric">
                <h3>${summary.total_issues_found}</h3>
                <p>Issues</p>
            </div>
            <div class="metric">
                <h3>${summary.total_warnings}</h3>
                <p>Warnings</p>
            </div>
        `;
        
        // Populate exchange details
        const exchangesDiv = document.getElementById('exchanges');
        for (const [exchangeName, exchangeData] of Object.entries(reportData.exchange_results)) {
            const statusClass = `status-${exchangeData.overall_status.toLowerCase()}`;
            exchangesDiv.innerHTML += `
                <div class="exchange ${statusClass}">
                    <h3>${exchangeName.toUpperCase()}</h3>
                    <p><strong>Status:</strong> ${exchangeData.overall_status}</p>
                    <p><strong>REST API Tests:</strong> ${exchangeData.rest_api_tests.length} endpoints tested</p>
                    <p><strong>WebSocket:</strong> ${exchangeData.websocket_tests.status || 'N/A'}</p>
                    <p><strong>Average Latency:</strong> ${exchangeData.latency_tests.average_latency_ms || 'N/A'}ms</p>
                </div>
            `;
        }
    </script>
</body>
</html>
EOF
    
    log_info "HTML report generated: $html_file"
}

# Main execution
main() {
    log_info "Starting Exchange API Compatibility Test Runner"
    
    # Create report directory
    create_report_dir
    
    # Install dependencies
    install_dependencies
    
    # Run the tests
    local test_exit_code=0
    run_compatibility_tests || test_exit_code=$?
    
    # Archive the report
    archive_report
    
    # Parse report for summary
    local report_summary=""
    if parse_report > /tmp/report_summary.txt 2>/dev/null; then
        report_summary=$(cat /tmp/report_summary.txt)
    fi
    
    # Generate CI artifacts
    generate_ci_artifacts
    
    # Send alerts if needed
    send_alerts $test_exit_code "$report_summary"
    
    # Final status
    case $test_exit_code in
        0)
            log_success "All exchange compatibility tests passed!"
            ;;
        1)
            log_error "Critical issues detected - immediate action required"
            ;;
        2)
            log_warning "Performance issues detected - monitoring recommended"
            if [ "$FAIL_ON_WARNINGS" = "true" ]; then
                log_error "Treating warnings as failures (FAIL_ON_WARNINGS=true)"
            fi
            ;;
        *)
            log_error "Test execution failed"
            ;;
    esac
    
    # Cleanup
    rm -f /tmp/report_summary.txt
    
    exit $test_exit_code
}

# Show help
show_help() {
    cat <<EOF
Exchange API Compatibility Test Runner

Usage: $0 [OPTIONS]

Options:
    -h, --help              Show this help message
    -w, --fail-on-warnings  Treat warnings as failures
    --report-dir DIR        Custom report directory (default: $REPORT_DIR)
    
Environment Variables:
    SLACK_WEBHOOK_URL       Slack webhook URL for notifications
    ALERT_WEBHOOK_URL       Generic webhook URL for alerts
    EMAIL_RECIPIENTS        Email addresses for notifications (comma-separated)
    FAIL_ON_WARNINGS        Set to 'true' to fail on warnings (default: false)

Examples:
    # Run tests normally
    $0
    
    # Run tests and fail on warnings
    $0 --fail-on-warnings
    
    # Run with Slack notifications
    SLACK_WEBHOOK_URL="https://hooks.slack.com/..." $0
    
Exit Codes:
    0   All tests passed
    1   Critical issues detected
    2   Performance/warning issues detected
    3   Test execution failed
    124 Test execution timed out

EOF
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -w|--fail-on-warnings)
            FAIL_ON_WARNINGS="true"
            shift
            ;;
        --report-dir)
            REPORT_DIR="$2"
            shift 2
            ;;
        *)
            log_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Execute main function
main "$@"