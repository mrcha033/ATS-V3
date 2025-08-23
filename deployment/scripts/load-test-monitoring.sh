#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/common.sh"

log_info "Starting load testing for monitoring system validation"

# Configuration
PROMETHEUS_URL="${PROMETHEUS_URL:-http://localhost:9090}"
GRAFANA_URL="${GRAFANA_URL:-http://localhost:3000}"
TEST_DURATION="${TEST_DURATION:-300}"  # 5 minutes default
LOAD_LEVEL="${LOAD_LEVEL:-medium}"  # low, medium, high

# Test results directory
TEST_RESULTS_DIR="${PROJECT_ROOT}/deployment/logs/load-test-$(date +%Y%m%d-%H%M%S)"
mkdir -p "${TEST_RESULTS_DIR}"

# Function to simulate trading load
simulate_trading_load() {
    local load_level=$1
    local duration=$2
    
    log_info "Simulating trading load: ${load_level} for ${duration} seconds"
    
    case ${load_level} in
        "low")
            OPPORTUNITIES_PER_SEC=1
            SUCCESS_RATE=0.95
            ;;
        "medium")
            OPPORTUNITIES_PER_SEC=5
            SUCCESS_RATE=0.90
            ;;
        "high")
            OPPORTUNITIES_PER_SEC=20
            SUCCESS_RATE=0.80
            ;;
        *)
            log_error "Unknown load level: ${load_level}"
            return 1
            ;;
    esac
    
    # Create load testing script
    cat > "${TEST_RESULTS_DIR}/simulate_load.py" << EOF
#!/usr/bin/env python3

import requests
import time
import random
import json
import threading
from concurrent.futures import ThreadPoolExecutor
import sys

class TradingLoadSimulator:
    def __init__(self, base_url, opportunities_per_sec, success_rate, duration):
        self.base_url = base_url
        self.opportunities_per_sec = opportunities_per_sec
        self.success_rate = success_rate
        self.duration = duration
        self.results = {
            'opportunities_sent': 0,
            'trades_executed': 0,
            'successful_trades': 0,
            'failed_trades': 0,
            'errors': 0
        }
        
    def generate_opportunity(self):
        """Generate a simulated arbitrage opportunity"""
        symbols = ['BTCUSDT', 'ETHUSDT', 'ADAUSDT', 'DOTUSDT', 'LINKUSDT']
        exchanges = ['binance', 'coinbase', 'kraken', 'upbit']
        
        symbol = random.choice(symbols)
        buy_exchange = random.choice(exchanges)
        sell_exchange = random.choice([e for e in exchanges if e != buy_exchange])
        
        base_price = random.uniform(100, 50000)
        spread = random.uniform(0.005, 0.02)  # 0.5% to 2% spread
        
        return {
            'symbol': symbol,
            'buy_exchange': buy_exchange,
            'sell_exchange': sell_exchange,
            'buy_price': base_price,
            'sell_price': base_price * (1 + spread),
            'quantity': random.uniform(0.1, 10.0),
            'expected_profit': base_price * spread * random.uniform(0.1, 10.0)
        }
    
    def send_opportunity(self, opportunity):
        """Simulate sending an opportunity to the trading engine"""
        try:
            # In a real system, this would POST to the trading engine API
            # For simulation, we'll just track metrics
            
            self.results['opportunities_sent'] += 1
            
            # Simulate processing time
            processing_time = random.uniform(0.05, 0.5)  # 50-500ms
            time.sleep(processing_time)
            
            # Simulate trade execution
            if random.random() <= self.success_rate:
                self.results['successful_trades'] += 1
                self.simulate_successful_trade(opportunity, processing_time)
            else:
                self.results['failed_trades'] += 1
                self.simulate_failed_trade(opportunity)
                
            self.results['trades_executed'] += 1
            
        except Exception as e:
            print(f"Error processing opportunity: {e}")
            self.results['errors'] += 1
    
    def simulate_successful_trade(self, opportunity, latency):
        """Simulate metrics for a successful trade"""
        # In a real system, these would be sent to Prometheus via the exporter
        # For testing, we log the metrics that should appear
        
        profit = opportunity['expected_profit'] * random.uniform(0.8, 1.1)
        slippage = random.uniform(0, 0.01)  # 0-1% slippage
        
        metrics_log = {
            'timestamp': time.time(),
            'type': 'successful_trade',
            'symbol': opportunity['symbol'],
            'buy_exchange': opportunity['buy_exchange'],
            'sell_exchange': opportunity['sell_exchange'],
            'profit_usd': profit,
            'latency_ms': latency * 1000,
            'slippage_percent': slippage * 100
        }
        
        with open('${TEST_RESULTS_DIR}/simulated_metrics.jsonl', 'a') as f:
            f.write(json.dumps(metrics_log) + '\n')
    
    def simulate_failed_trade(self, opportunity):
        """Simulate metrics for a failed trade"""
        failure_reasons = [
            'insufficient_balance',
            'market_moved',
            'api_timeout',
            'risk_limit_exceeded',
            'exchange_error'
        ]
        
        metrics_log = {
            'timestamp': time.time(),
            'type': 'failed_trade',
            'symbol': opportunity['symbol'],
            'reason': random.choice(failure_reasons)
        }
        
        with open('${TEST_RESULTS_DIR}/simulated_metrics.jsonl', 'a') as f:
            f.write(json.dumps(metrics_log) + '\n')
    
    def worker_thread(self):
        """Worker thread to generate and process opportunities"""
        opportunities_sent = 0
        start_time = time.time()
        
        while time.time() - start_time < self.duration:
            if opportunities_sent < (time.time() - start_time) * self.opportunities_per_sec:
                opportunity = self.generate_opportunity()
                self.send_opportunity(opportunity)
                opportunities_sent += 1
            else:
                time.sleep(0.1)  # Small delay to prevent busy waiting
    
    def run_load_test(self):
        """Run the load test"""
        print(f"Starting load test: {self.opportunities_per_sec} opp/sec for {self.duration}s")
        print(f"Expected success rate: {self.success_rate}")
        
        start_time = time.time()
        
        # Use thread pool to simulate concurrent load
        with ThreadPoolExecutor(max_workers=min(10, self.opportunities_per_sec)) as executor:
            # Submit worker threads
            futures = []
            for _ in range(min(5, max(1, self.opportunities_per_sec // 2))):
                future = executor.submit(self.worker_thread)
                futures.append(future)
            
            # Wait for completion
            for future in futures:
                future.result()
        
        end_time = time.time()
        actual_duration = end_time - start_time
        
        # Print results
        print(f"\nLoad test completed in {actual_duration:.2f} seconds")
        print(f"Opportunities sent: {self.results['opportunities_sent']}")
        print(f"Trades executed: {self.results['trades_executed']}")
        print(f"Successful trades: {self.results['successful_trades']}")
        print(f"Failed trades: {self.results['failed_trades']}")
        print(f"Errors: {self.results['errors']}")
        print(f"Actual success rate: {self.results['successful_trades'] / max(1, self.results['trades_executed']):.2%}")
        
        # Save results
        with open('${TEST_RESULTS_DIR}/load_test_results.json', 'w') as f:
            self.results['duration'] = actual_duration
            self.results['opportunities_per_sec'] = self.results['opportunities_sent'] / actual_duration
            json.dump(self.results, f, indent=2)

if __name__ == '__main__':
    # Parse command line arguments
    base_url = sys.argv[1] if len(sys.argv) > 1 else 'http://localhost:8080'
    opportunities_per_sec = int(sys.argv[2]) if len(sys.argv) > 2 else ${OPPORTUNITIES_PER_SEC}
    success_rate = float(sys.argv[3]) if len(sys.argv) > 3 else ${SUCCESS_RATE}
    duration = int(sys.argv[4]) if len(sys.argv) > 4 else ${duration}
    
    simulator = TradingLoadSimulator(base_url, opportunities_per_sec, success_rate, duration)
    simulator.run_load_test()
EOF
    
    # Make the script executable
    chmod +x "${TEST_RESULTS_DIR}/simulate_load.py"
    
    # Run the load test
    cd "${TEST_RESULTS_DIR}"
    python3 simulate_load.py "http://localhost:8082" ${OPPORTUNITIES_PER_SEC} ${SUCCESS_RATE} ${duration}
    
    log_success "Trading load simulation completed"
}

# Function to monitor system resources during load test
monitor_system_resources() {
    local duration=$1
    
    log_info "Monitoring system resources for ${duration} seconds"
    
    cat > "${TEST_RESULTS_DIR}/monitor_resources.sh" << 'EOF'
#!/bin/bash

DURATION=$1
OUTPUT_FILE=$2

echo "timestamp,cpu_percent,memory_mb,docker_containers" > ${OUTPUT_FILE}

END_TIME=$(($(date +%s) + ${DURATION}))

while [ $(date +%s) -lt ${END_TIME} ]; do
    TIMESTAMP=$(date +%s)
    
    # Get CPU usage
    CPU_USAGE=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | sed 's/%us,//')
    
    # Get memory usage
    MEMORY_USAGE=$(free -m | awk 'NR==2{printf "%.0f", $3}')
    
    # Count Docker containers
    CONTAINER_COUNT=$(docker ps -q | wc -l)
    
    echo "${TIMESTAMP},${CPU_USAGE:-0},${MEMORY_USAGE:-0},${CONTAINER_COUNT}" >> ${OUTPUT_FILE}
    
    sleep 5
done
EOF
    
    chmod +x "${TEST_RESULTS_DIR}/monitor_resources.sh"
    
    # Start resource monitoring in background
    "${TEST_RESULTS_DIR}/monitor_resources.sh" ${duration} "${TEST_RESULTS_DIR}/system_resources.csv" &
    MONITOR_PID=$!
    
    # Return the PID so we can wait for it
    echo ${MONITOR_PID}
}

# Function to test Prometheus metrics collection
test_prometheus_metrics() {
    log_info "Testing Prometheus metrics collection"
    
    local metrics_to_test=(
        "ats_service_health"
        "ats_successful_trades_total" 
        "ats_failed_trades_total"
        "ats_arbitrage_opportunities_total"
        "ats_order_latency_milliseconds"
        "ats_cpu_usage_percent"
        "ats_memory_usage_mb"
        "ats_total_pnl_usd"
    )
    
    echo "metric_name,available,sample_count,latest_value" > "${TEST_RESULTS_DIR}/prometheus_metrics.csv"
    
    for metric in "${metrics_to_test[@]}"; do
        log_info "Testing metric: ${metric}"
        
        # Query Prometheus for the metric
        response=$(curl -s "${PROMETHEUS_URL}/api/v1/query?query=${metric}" || echo "")
        
        if echo "${response}" | jq -e '.data.result | length > 0' > /dev/null 2>&1; then
            sample_count=$(echo "${response}" | jq -r '.data.result | length')
            latest_value=$(echo "${response}" | jq -r '.data.result[0].value[1] // "N/A"')
            echo "${metric},true,${sample_count},${latest_value}" >> "${TEST_RESULTS_DIR}/prometheus_metrics.csv"
            log_success "✓ ${metric}: ${sample_count} samples, latest value: ${latest_value}"
        else
            echo "${metric},false,0,N/A" >> "${TEST_RESULTS_DIR}/prometheus_metrics.csv"
            log_warning "✗ ${metric}: No data available"
        fi
    done
    
    log_success "Prometheus metrics test completed"
}

# Function to test Grafana dashboards
test_grafana_dashboards() {
    log_info "Testing Grafana dashboard accessibility"
    
    local dashboards=(
        "ats-overview"
        "ats-trading-performance"
    )
    
    echo "dashboard_uid,accessible,status_code" > "${TEST_RESULTS_DIR}/grafana_dashboards.csv"
    
    for dashboard in "${dashboards[@]}"; do
        log_info "Testing dashboard: ${dashboard}"
        
        # Test dashboard accessibility
        status_code=$(curl -s -o /dev/null -w "%{http_code}" "${GRAFANA_URL}/api/dashboards/uid/${dashboard}" || echo "000")
        
        if [ "${status_code}" = "200" ]; then
            echo "${dashboard},true,${status_code}" >> "${TEST_RESULTS_DIR}/grafana_dashboards.csv"
            log_success "✓ ${dashboard}: Accessible (HTTP ${status_code})"
        else
            echo "${dashboard},false,${status_code}" >> "${TEST_RESULTS_DIR}/grafana_dashboards.csv"
            log_warning "✗ ${dashboard}: Not accessible (HTTP ${status_code})"
        fi
    done
    
    log_success "Grafana dashboards test completed"
}

# Function to generate load test report
generate_load_test_report() {
    log_info "Generating load test report"
    
    cat > "${TEST_RESULTS_DIR}/load_test_report.md" << EOF
# ATS Monitoring System Load Test Report

**Test Date:** $(date)
**Test Duration:** ${TEST_DURATION} seconds
**Load Level:** ${LOAD_LEVEL}

## Test Summary

### Trading Load Simulation
$(cat "${TEST_RESULTS_DIR}/load_test_results.json" 2>/dev/null || echo "Load test results not available")

### System Resources
- Resource monitoring data: \`system_resources.csv\`
- Peak CPU usage observed during test
- Peak memory usage observed during test
- Docker container stability

### Prometheus Metrics
- Metrics availability: \`prometheus_metrics.csv\`
- All critical trading metrics should be collecting data
- System metrics should show load test impact

### Grafana Dashboards
- Dashboard accessibility: \`grafana_dashboards.csv\`
- Visual verification required for dashboard functionality

## Key Findings

### Successful Metrics Collection
- [ ] Trading metrics (opportunities, executions, P&L)
- [ ] System metrics (CPU, memory, uptime)
- [ ] Performance metrics (latency, error rates)

### Dashboard Functionality
- [ ] ATS Overview dashboard loads correctly
- [ ] Trading Performance dashboard shows real-time data
- [ ] Alerts are configured and functional

### Auto-scaling Triggers
- [ ] CPU-based scaling rules active
- [ ] Memory-based scaling rules active  
- [ ] Performance-based scaling rules active

## Recommendations

1. **Metrics Collection**: Review any missing metrics and verify exporter integration
2. **Dashboard Performance**: Optimize slow-loading panels if detected
3. **Alert Thresholds**: Adjust alert thresholds based on observed normal operating ranges
4. **Scaling Policies**: Fine-tune scaling triggers based on load test behavior

## Next Steps

1. Run extended load test (1+ hour) to validate stability
2. Test alert notification delivery
3. Validate auto-scaling behavior under sustained load
4. Performance tune based on observed bottlenecks

---
*Generated by ATS Monitoring Load Test Script*
EOF
    
    log_success "Load test report generated: ${TEST_RESULTS_DIR}/load_test_report.md"
}

# Main load testing function
run_load_test() {
    log_info "Starting comprehensive load test"
    
    # Pre-test checks
    log_info "Checking service availability..."
    
    if ! curl -s "${PROMETHEUS_URL}/api/v1/targets" > /dev/null; then
        log_error "Prometheus is not accessible at ${PROMETHEUS_URL}"
        return 1
    fi
    
    if ! curl -s "${GRAFANA_URL}/api/health" > /dev/null; then
        log_warning "Grafana is not accessible at ${GRAFANA_URL} (continuing anyway)"
    fi
    
    # Start system resource monitoring
    monitor_pid=$(monitor_system_resources ${TEST_DURATION})
    
    # Run trading load simulation
    simulate_trading_load ${LOAD_LEVEL} ${TEST_DURATION}
    
    # Wait a moment for metrics to propagate
    sleep 10
    
    # Test Prometheus metrics collection
    test_prometheus_metrics
    
    # Test Grafana dashboards
    test_grafana_dashboards
    
    # Wait for resource monitoring to complete
    wait ${monitor_pid}
    
    # Generate comprehensive report
    generate_load_test_report
    
    log_success "Load test completed successfully"
    log_info "Results saved to: ${TEST_RESULTS_DIR}"
    log_info "View report: cat ${TEST_RESULTS_DIR}/load_test_report.md"
}

# Command line interface
print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  -d, --duration SECONDS    Test duration in seconds (default: 300)"
    echo "  -l, --load LEVEL         Load level: low, medium, high (default: medium)"
    echo "  -p, --prometheus URL     Prometheus URL (default: http://localhost:9090)"
    echo "  -g, --grafana URL        Grafana URL (default: http://localhost:3000)"
    echo "  -h, --help              Show this help message"
    echo ""
    echo "Example:"
    echo "  $0 --duration 600 --load high --prometheus http://localhost:9090"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--duration)
            TEST_DURATION="$2"
            shift 2
            ;;
        -l|--load)
            LOAD_LEVEL="$2"
            shift 2
            ;;
        -p|--prometheus)
            PROMETHEUS_URL="$2"
            shift 2
            ;;
        -g|--grafana)
            GRAFANA_URL="$2"
            shift 2
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Main execution
main() {
    log_info "ATS Monitoring System Load Test"
    log_info "Duration: ${TEST_DURATION}s, Load: ${LOAD_LEVEL}"
    log_info "Prometheus: ${PROMETHEUS_URL}"
    log_info "Grafana: ${GRAFANA_URL}"
    log_info "Results: ${TEST_RESULTS_DIR}"
    
    run_load_test
    
    log_success "Load testing completed successfully!"
}

main "$@"