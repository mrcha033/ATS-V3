# Monitoring and Observability

ATS-V3 is built with observability as a core principle, providing comprehensive insights into its operation, performance, and health. This is achieved through integrated logging, metrics, and distributed tracing (implied).

## Key Tools

-   **Prometheus**: An open-source monitoring system with a time-series database. ATS-V3 services expose metrics in a Prometheus-compatible format, which Prometheus scrapes and stores.
-   **Grafana**: An open-source platform for data visualization and analytics. It integrates with Prometheus (and InfluxDB) to create rich, interactive dashboards for real-time monitoring.
-   **InfluxDB**: A high-performance time-series database used for storing detailed historical market data, trade logs, risk metrics, and notification history. It complements Prometheus for long-term data retention and complex analytical queries.
-   **`spdlog`**: A fast, header-only C++ logging library used across ATS-V3 services for structured and efficient logging.

## Monitoring Architecture

```
+-------------------+     +-------------------+
|   ATS-V3 Services |     |   ATS-V3 Services |
| (Price Collector) |     | (Trading Engine)  |
| (Risk Manager)    |     | (Notification)    |
+-------------------+     +-------------------+
         | Exposes Metrics (HTTP)    |
         v                           v
+-------------------+
|     Prometheus    |<----------------------+
| (Metrics Storage) |                       |
+-------------------+                       |
         |                                   |
         | Scrapes Metrics                   |
         v                                   |
+-------------------+
|     Grafana       |<----------------------+
| (Dashboards)      |                       |
+-------------------+                       |
         ^                                   |
         | Queries                           |
         +-----------------------------------+
                                             |
+-------------------+
|     InfluxDB      |<----------------------+
| (Historical Data) |     (Logs, Trades, Risk Metrics)
+-------------------+
```

## Metrics

ATS-V3 services expose a variety of metrics to Prometheus, providing insights into their internal state and performance. Key metrics include:

-   **Service Health**: Uptime, connection status to external dependencies (exchanges, Redis, InfluxDB).
-   **Market Data**: Number of messages received, parsing latency, message rates per second, average latency to exchanges.
-   **Trading Engine**: Opportunities detected, trades executed (successful/failed), average execution time, total P&L, fees paid, rollback counts.
-   **Risk Manager**: Current exposure, P&L, VaR, number of risk checks, alerts generated.
-   **Notification Service**: Total notifications sent (by channel and type), delivery success rates, batch processing statistics.
-   **System Resources**: CPU usage, memory consumption, thread counts.

Metrics are exposed via an HTTP endpoint (typically `/metrics`) on a configurable port for each service (e.g., `trading_engine` exposes on `8082`).

## Logging

All ATS-V3 services utilize `spdlog` for structured and efficient logging. Log levels can be configured (`TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `CRITICAL`) to control the verbosity of output.

-   **Console Output**: Logs are streamed to the console for immediate feedback during development and debugging.
-   **File Logging**: Logs are also written to rotating log files, ensuring historical records are maintained without consuming excessive disk space.
-   **Structured Logging**: Important events (e.g., trade executions, risk alerts) are logged in a structured format, making them easier to parse and analyze by external tools or for ingestion into log management systems.

## Dashboards (Grafana)

Grafana dashboards provide a visual representation of the system's health and performance. After deploying ATS-V3 with Docker Compose, you can access Grafana at `http://localhost:3000`.

To set up dashboards:

1.  **Add Data Sources**: Configure Prometheus and InfluxDB as data sources in Grafana.
2.  **Import Dashboards**: Import pre-built dashboards (if available in `deployment/grafana/dashboards/`) or create custom ones using the collected metrics and logs.

Recommended dashboards would include:
-   **Overall System Health**: CPU, Memory, Network, Service Uptime.
-   **Trading Performance**: Real-time P&L, Trade Count, Success Rate, Execution Latency.
-   **Market Data Quality**: Message Rates, Latency per Exchange.
-   **Risk Overview**: Current Exposure, VaR, Drawdown, Alert Counts.
-   **Notification Delivery**: Email/Push notification success rates, pending batches.

## Alerting

Prometheus can be configured to send alerts based on predefined rules (e.g., high error rates, service downtime, risk limit breaches). These alerts can be routed to various notification channels (e.g., email, Slack, PagerDuty) via the `notification_service` module.

## Data Retention

-   **Prometheus**: Typically configured for short-to-medium term metrics storage (e.g., 15-30 days).
-   **InfluxDB**: Used for long-term storage of detailed historical data (market data, trade logs, risk events) that requires more granular querying and analysis over extended periods.

