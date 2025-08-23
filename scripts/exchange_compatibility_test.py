#!/usr/bin/env python3
"""
Exchange API Compatibility Test Script

This script performs daily compatibility tests for exchange APIs to detect
version changes, deprecated endpoints, and breaking changes before they
impact the live trading system.
"""

import asyncio
import json
import logging
import os
import sys
import time
from datetime import datetime, timedelta
from typing import Dict, List, Optional, Any
import aiohttp
import asyncio
import subprocess

# Configuration
EXCHANGES = {
    'binance': {
        'base_url': 'https://api.binance.com',
        'testnet_url': 'https://testnet.binance.vision',
        'endpoints': [
            '/api/v3/ping',
            '/api/v3/time',
            '/api/v3/exchangeInfo',
            '/api/v3/ticker/24hr',
            '/api/v3/depth',
            '/api/v3/klines'
        ],
        'ws_url': 'wss://stream.binance.com:9443/ws/btcusdt@ticker',
        'auth_required': ['/api/v3/account', '/api/v3/order']
    },
    'upbit': {
        'base_url': 'https://api.upbit.com',
        'endpoints': [
            '/v1/market/all',
            '/v1/ticker',
            '/v1/orderbook',
            '/v1/candles/minutes/1'
        ],
        'ws_url': 'wss://api.upbit.com/websocket/v1',
        'auth_required': ['/v1/accounts', '/v1/orders']
    }
}

TIMEOUT_SECONDS = 30
MAX_RETRIES = 3
REPORT_FILE = 'exchange_compatibility_report.json'

class ExchangeCompatibilityTester:
    def __init__(self):
        self.session = None
        self.results = {}
        self.logger = self._setup_logging()
        
    def _setup_logging(self):
        logging.basicConfig(
            level=logging.INFO,
            format='%(asctime)s - %(levelname)s - %(message)s',
            handlers=[
                logging.FileHandler('exchange_compatibility_test.log'),
                logging.StreamHandler(sys.stdout)
            ]
        )
        return logging.getLogger(__name__)
    
    async def run_all_tests(self) -> Dict[str, Any]:
        """Run compatibility tests for all configured exchanges."""
        self.logger.info("Starting exchange compatibility tests")
        
        async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=TIMEOUT_SECONDS)) as session:
            self.session = session
            
            for exchange_name, config in EXCHANGES.items():
                self.logger.info(f"Testing {exchange_name}")
                self.results[exchange_name] = await self.test_exchange(exchange_name, config)
        
        # Generate report
        report = self.generate_report()
        self.save_report(report)
        
        return report
    
    async def test_exchange(self, exchange_name: str, config: Dict[str, Any]) -> Dict[str, Any]:
        """Test a specific exchange for compatibility issues."""
        test_results = {
            'exchange': exchange_name,
            'timestamp': datetime.utcnow().isoformat(),
            'rest_api_tests': [],
            'websocket_tests': [],
            'latency_tests': [],
            'rate_limit_tests': [],
            'overall_status': 'UNKNOWN',
            'issues_found': [],
            'warnings': []
        }
        
        # Test REST API endpoints
        rest_results = await self.test_rest_endpoints(exchange_name, config)
        test_results['rest_api_tests'] = rest_results
        
        # Test WebSocket connectivity
        ws_results = await self.test_websocket(exchange_name, config)
        test_results['websocket_tests'] = ws_results
        
        # Test latency
        latency_results = await self.test_latency(exchange_name, config)
        test_results['latency_tests'] = latency_results
        
        # Test rate limiting behavior
        rate_limit_results = await self.test_rate_limits(exchange_name, config)
        test_results['rate_limit_tests'] = rate_limit_results
        
        # Determine overall status
        test_results['overall_status'] = self.determine_overall_status(test_results)
        
        return test_results
    
    async def test_rest_endpoints(self, exchange_name: str, config: Dict[str, Any]) -> List[Dict[str, Any]]:
        """Test REST API endpoints for availability and response format."""
        results = []
        base_url = config['base_url']
        
        for endpoint in config['endpoints']:
            endpoint_result = {
                'endpoint': endpoint,
                'status': 'UNKNOWN',
                'response_time_ms': 0,
                'status_code': 0,
                'content_type': '',
                'response_size': 0,
                'schema_validation': 'NOT_TESTED',
                'issues': [],
                'warnings': []
            }
            
            url = base_url + endpoint
            
            # Add required parameters for some endpoints
            params = self.get_default_params(exchange_name, endpoint)
            
            start_time = time.time()
            
            try:
                async with self.session.get(url, params=params) as response:
                    end_time = time.time()
                    
                    endpoint_result['response_time_ms'] = int((end_time - start_time) * 1000)
                    endpoint_result['status_code'] = response.status
                    endpoint_result['content_type'] = response.headers.get('content-type', '')
                    
                    if response.status == 200:
                        content = await response.text()
                        endpoint_result['response_size'] = len(content)
                        
                        # Validate JSON response
                        try:
                            data = json.loads(content)
                            endpoint_result['schema_validation'] = self.validate_response_schema(
                                exchange_name, endpoint, data
                            )
                            endpoint_result['status'] = 'PASS'
                        except json.JSONDecodeError:
                            endpoint_result['issues'].append('Invalid JSON response')
                            endpoint_result['status'] = 'FAIL'
                    
                    elif response.status == 429:
                        endpoint_result['status'] = 'RATE_LIMITED'
                        endpoint_result['warnings'].append('Rate limit encountered')
                    
                    else:
                        endpoint_result['status'] = 'FAIL'
                        endpoint_result['issues'].append(f'HTTP {response.status}')
                        
            except asyncio.TimeoutError:
                endpoint_result['status'] = 'TIMEOUT'
                endpoint_result['issues'].append('Request timeout')
                
            except Exception as e:
                endpoint_result['status'] = 'ERROR'
                endpoint_result['issues'].append(str(e))
            
            # Check for performance issues
            if endpoint_result['response_time_ms'] > 5000:
                endpoint_result['warnings'].append('High latency detected')
            
            results.append(endpoint_result)
            self.logger.info(f"{exchange_name} {endpoint}: {endpoint_result['status']}")
            
            # Small delay to avoid hitting rate limits
            await asyncio.sleep(0.1)
        
        return results
    
    async def test_websocket(self, exchange_name: str, config: Dict[str, Any]) -> Dict[str, Any]:
        """Test WebSocket connectivity and message format."""
        if 'ws_url' not in config:
            return {'status': 'SKIPPED', 'reason': 'No WebSocket URL configured'}
        
        result = {
            'url': config['ws_url'],
            'status': 'UNKNOWN',
            'connection_time_ms': 0,
            'messages_received': 0,
            'message_format_valid': False,
            'issues': [],
            'warnings': []
        }
        
        start_time = time.time()
        
        try:
            async with self.session.ws_connect(config['ws_url']) as ws:
                connection_time = time.time() - start_time
                result['connection_time_ms'] = int(connection_time * 1000)
                
                # Wait for messages for up to 10 seconds
                timeout = 10
                end_time = time.time() + timeout
                
                while time.time() < end_time:
                    try:
                        msg = await asyncio.wait_for(ws.receive(), timeout=1.0)
                        
                        if msg.type == aiohttp.WSMsgType.TEXT:
                            result['messages_received'] += 1
                            
                            # Validate message format
                            try:
                                json.loads(msg.data)
                                result['message_format_valid'] = True
                            except json.JSONDecodeError:
                                result['warnings'].append('Invalid JSON message format')
                        
                        elif msg.type == aiohttp.WSMsgType.ERROR:
                            result['issues'].append(f'WebSocket error: {msg.data}')
                            break
                            
                    except asyncio.TimeoutError:
                        continue
                
                if result['messages_received'] > 0:
                    result['status'] = 'PASS'
                else:
                    result['status'] = 'FAIL'
                    result['issues'].append('No messages received')
                    
        except Exception as e:
            result['status'] = 'ERROR'
            result['issues'].append(str(e))
        
        return result
    
    async def test_latency(self, exchange_name: str, config: Dict[str, Any]) -> Dict[str, Any]:
        """Test API latency and consistency."""
        result = {
            'measurements': [],
            'average_latency_ms': 0,
            'min_latency_ms': float('inf'),
            'max_latency_ms': 0,
            'latency_variance': 0,
            'status': 'UNKNOWN',
            'issues': [],
            'warnings': []
        }
        
        # Use ping endpoint or first available endpoint
        test_endpoint = config['endpoints'][0]
        url = config['base_url'] + test_endpoint
        params = self.get_default_params(exchange_name, test_endpoint)
        
        measurements = []
        
        # Take 10 measurements
        for i in range(10):
            start_time = time.time()
            
            try:
                async with self.session.get(url, params=params) as response:
                    if response.status == 200:
                        end_time = time.time()
                        latency = (end_time - start_time) * 1000
                        measurements.append(latency)
                        
            except Exception:
                pass  # Skip failed measurements
            
            await asyncio.sleep(0.2)  # Small delay between measurements
        
        if measurements:
            result['measurements'] = measurements
            result['average_latency_ms'] = sum(measurements) / len(measurements)
            result['min_latency_ms'] = min(measurements)
            result['max_latency_ms'] = max(measurements)
            
            # Calculate variance
            avg = result['average_latency_ms']
            variance = sum((x - avg) ** 2 for x in measurements) / len(measurements)
            result['latency_variance'] = variance
            
            # Determine status
            if result['average_latency_ms'] < 200:
                result['status'] = 'EXCELLENT'
            elif result['average_latency_ms'] < 500:
                result['status'] = 'GOOD'
            elif result['average_latency_ms'] < 1000:
                result['status'] = 'ACCEPTABLE'
                result['warnings'].append('Higher than expected latency')
            else:
                result['status'] = 'POOR'
                result['issues'].append('High latency detected')
                
            # Check for high variance
            if variance > 100000:  # High variance in latency
                result['warnings'].append('High latency variance detected')
        
        else:
            result['status'] = 'FAIL'
            result['issues'].append('Could not measure latency')
        
        return result
    
    async def test_rate_limits(self, exchange_name: str, config: Dict[str, Any]) -> Dict[str, Any]:
        """Test rate limiting behavior."""
        result = {
            'rate_limit_detected': False,
            'requests_before_limit': 0,
            'rate_limit_headers': {},
            'recovery_time_seconds': 0,
            'status': 'UNKNOWN',
            'issues': [],
            'warnings': []
        }
        
        test_endpoint = config['endpoints'][0]
        url = config['base_url'] + test_endpoint
        params = self.get_default_params(exchange_name, test_endpoint)
        
        # Make rapid requests to trigger rate limiting
        request_count = 0
        rate_limit_hit = False
        
        for i in range(50):  # Try up to 50 requests
            try:
                async with self.session.get(url, params=params) as response:
                    request_count += 1
                    
                    # Check for rate limit headers
                    if 'x-mbx-used-weight' in response.headers:  # Binance
                        result['rate_limit_headers']['used_weight'] = response.headers['x-mbx-used-weight']
                    if 'x-ratelimit-remaining' in response.headers:  # Generic
                        result['rate_limit_headers']['remaining'] = response.headers['x-ratelimit-remaining']
                    
                    if response.status == 429:
                        result['rate_limit_detected'] = True
                        result['requests_before_limit'] = request_count
                        rate_limit_hit = True
                        
                        # Test recovery time
                        recovery_start = time.time()
                        while time.time() - recovery_start < 60:  # Wait up to 60 seconds
                            await asyncio.sleep(1)
                            async with self.session.get(url, params=params) as recovery_response:
                                if recovery_response.status != 429:
                                    result['recovery_time_seconds'] = int(time.time() - recovery_start)
                                    break
                        
                        break
                        
            except Exception as e:
                result['issues'].append(f'Error during rate limit test: {str(e)}')
                break
            
            await asyncio.sleep(0.1)  # Small delay
        
        if rate_limit_hit:
            result['status'] = 'DETECTED'
        else:
            result['status'] = 'NOT_TRIGGERED'
            result['warnings'].append('Rate limit not triggered - may need adjustment')
        
        return result
    
    def get_default_params(self, exchange_name: str, endpoint: str) -> Dict[str, str]:
        """Get default parameters for specific endpoints."""
        params = {}
        
        if exchange_name == 'binance':
            if 'ticker' in endpoint:
                params['symbol'] = 'BTCUSDT'
            elif 'depth' in endpoint:
                params['symbol'] = 'BTCUSDT'
                params['limit'] = '10'
            elif 'klines' in endpoint:
                params['symbol'] = 'BTCUSDT'
                params['interval'] = '1h'
                params['limit'] = '10'
                
        elif exchange_name == 'upbit':
            if 'ticker' in endpoint:
                params['markets'] = 'KRW-BTC'
            elif 'orderbook' in endpoint:
                params['markets'] = 'KRW-BTC'
            elif 'candles' in endpoint:
                params['market'] = 'KRW-BTC'
                params['count'] = '10'
        
        return params
    
    def validate_response_schema(self, exchange_name: str, endpoint: str, data: Any) -> str:
        """Validate response schema against expected format."""
        try:
            if exchange_name == 'binance':
                if endpoint == '/api/v3/ping':
                    return 'PASS' if data == {} else 'FAIL'
                elif endpoint == '/api/v3/time':
                    return 'PASS' if 'serverTime' in data else 'FAIL'
                elif endpoint == '/api/v3/exchangeInfo':
                    return 'PASS' if 'symbols' in data and isinstance(data['symbols'], list) else 'FAIL'
                elif 'ticker' in endpoint:
                    expected_fields = ['symbol', 'price', 'volume']
                    if isinstance(data, list):
                        return 'PASS' if all(field in data[0] for field in expected_fields if data) else 'FAIL'
                    else:
                        return 'PASS' if all(field in data for field in expected_fields) else 'FAIL'
                        
            elif exchange_name == 'upbit':
                if endpoint == '/v1/market/all':
                    return 'PASS' if isinstance(data, list) and all('market' in item for item in data[:5]) else 'FAIL'
                elif 'ticker' in endpoint:
                    expected_fields = ['market', 'trade_price', 'acc_trade_volume_24h']
                    if isinstance(data, list):
                        return 'PASS' if all(field in data[0] for field in expected_fields if data) else 'FAIL'
                    else:
                        return 'PASS' if all(field in data for field in expected_fields) else 'FAIL'
            
            return 'PASS'  # Default pass for unknown endpoints
            
        except Exception:
            return 'FAIL'
    
    def determine_overall_status(self, test_results: Dict[str, Any]) -> str:
        """Determine overall status based on all test results."""
        critical_failures = 0
        warnings = 0
        
        # Check REST API results
        for rest_test in test_results['rest_api_tests']:
            if rest_test['status'] in ['FAIL', 'ERROR', 'TIMEOUT']:
                critical_failures += 1
            elif rest_test['status'] in ['RATE_LIMITED'] or rest_test['warnings']:
                warnings += 1
        
        # Check WebSocket results
        ws_test = test_results['websocket_tests']
        if isinstance(ws_test, dict) and ws_test.get('status') in ['FAIL', 'ERROR']:
            critical_failures += 1
        elif isinstance(ws_test, dict) and ws_test.get('warnings'):
            warnings += 1
        
        # Check latency results
        latency_test = test_results['latency_tests']
        if latency_test.get('status') in ['FAIL', 'POOR']:
            if latency_test.get('status') == 'FAIL':
                critical_failures += 1
            else:
                warnings += 1
        
        # Determine overall status
        if critical_failures > 2:
            return 'CRITICAL'
        elif critical_failures > 0:
            return 'DEGRADED'
        elif warnings > 3:
            return 'WARNING'
        else:
            return 'HEALTHY'
    
    def generate_report(self) -> Dict[str, Any]:
        """Generate comprehensive compatibility report."""
        report = {
            'test_run_id': datetime.utcnow().strftime('%Y%m%d_%H%M%S'),
            'timestamp': datetime.utcnow().isoformat(),
            'summary': {
                'total_exchanges_tested': len(self.results),
                'exchanges_healthy': 0,
                'exchanges_degraded': 0,
                'exchanges_critical': 0,
                'total_issues_found': 0,
                'total_warnings': 0
            },
            'exchange_results': self.results,
            'recommendations': [],
            'action_items': []
        }
        
        # Calculate summary statistics
        for exchange_name, result in self.results.items():
            status = result.get('overall_status', 'UNKNOWN')
            
            if status == 'HEALTHY':
                report['summary']['exchanges_healthy'] += 1
            elif status in ['DEGRADED', 'WARNING']:
                report['summary']['exchanges_degraded'] += 1
            elif status == 'CRITICAL':
                report['summary']['exchanges_critical'] += 1
            
            # Count issues and warnings
            for rest_test in result.get('rest_api_tests', []):
                report['summary']['total_issues_found'] += len(rest_test.get('issues', []))
                report['summary']['total_warnings'] += len(rest_test.get('warnings', []))
        
        # Generate recommendations
        if report['summary']['exchanges_critical'] > 0:
            report['recommendations'].append(
                'URGENT: Critical issues detected. Immediate investigation required.'
            )
            report['action_items'].append('Investigate critical exchange failures immediately')
        
        if report['summary']['exchanges_degraded'] > 0:
            report['recommendations'].append(
                'Performance degradation detected. Monitor closely and prepare contingency plans.'
            )
            report['action_items'].append('Review degraded exchange performance and implement mitigations')
        
        if report['summary']['total_warnings'] > 10:
            report['recommendations'].append(
                'High number of warnings detected. Review and optimize exchange configurations.'
            )
        
        return report
    
    def save_report(self, report: Dict[str, Any]) -> None:
        """Save report to file."""
        try:
            with open(REPORT_FILE, 'w') as f:
                json.dump(report, f, indent=2, default=str)
            self.logger.info(f"Report saved to {REPORT_FILE}")
        except Exception as e:
            self.logger.error(f"Failed to save report: {e}")


async def main():
    """Main entry point."""
    tester = ExchangeCompatibilityTester()
    
    try:
        report = await tester.run_all_tests()
        
        # Print summary
        print("\n" + "="*50)
        print("EXCHANGE COMPATIBILITY TEST SUMMARY")
        print("="*50)
        print(f"Exchanges tested: {report['summary']['total_exchanges_tested']}")
        print(f"Healthy: {report['summary']['exchanges_healthy']}")
        print(f"Degraded: {report['summary']['exchanges_degraded']}")
        print(f"Critical: {report['summary']['exchanges_critical']}")
        print(f"Total issues: {report['summary']['total_issues_found']}")
        print(f"Total warnings: {report['summary']['total_warnings']}")
        
        if report['recommendations']:
            print("\nRECOMMENDATIONS:")
            for rec in report['recommendations']:
                print(f"- {rec}")
        
        # Exit with appropriate code
        if report['summary']['exchanges_critical'] > 0:
            sys.exit(1)  # Critical issues found
        elif report['summary']['exchanges_degraded'] > 0:
            sys.exit(2)  # Performance issues found
        else:
            sys.exit(0)  # All tests passed
            
    except Exception as e:
        logging.error(f"Test execution failed: {e}")
        sys.exit(3)


if __name__ == "__main__":
    asyncio.run(main())