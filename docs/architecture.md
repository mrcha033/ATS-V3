# Architecture Overview

ATS-V3 is designed as a high-performance, modular, and resilient automated trading system. It follows a microservices-oriented architecture, where core functionalities are encapsulated within independent modules that communicate with each other using efficient and robust protocols.

## High-Level Design

The system's architecture can be visualized as a set of interconnected services, each responsible for a specific domain:

```
+-------------------+     +-------------------+     +-------------------+
|   Price Collector |---->|   Trading Engine  |<----|   Risk Manager    |
| (Market Data)     |     | (Arbitrage Logic) |     | (Risk Control)    |
+-------------------+     +-------------------+     +-------------------+
         ^                         ^                         ^
         |                         |                         |
         | (Redis Pub/Sub)         | (gRPC)                  | (gRPC)
         v                         v                         v
+-------------------+     +-------------------+     +-------------------+
|   UI Dashboard    |<----|   Notification    |<----|   Backtest        |
| (Visualization)   |     | (Alerts)          |     | (Strategy Eval)   |
+-------------------+     +-------------------+     +-------------------+
         ^                         ^
         |                         |
         +-------------------------+
              (Shared Libraries, Security, Monitoring)
```

## Key Architectural Principles

-   **Modularity**: Each module is a self-contained unit with a clear responsibility, promoting independent development, deployment, and scaling.
-   **Resilience**: The system incorporates various patterns like circuit breakers, retries, and failover mechanisms to gracefully handle failures in external dependencies (e.g., exchange APIs) or internal services.
-   **Scalability**: Modules can be scaled independently based on workload. Asynchronous communication and efficient data handling contribute to high throughput.
-   **Observability**: Comprehensive logging, metrics exposition (Prometheus), and distributed tracing (implied) provide deep insights into the system's behavior and performance.
-   **Security-by-Design**: Security features are integrated at every layer, from secure communication (TLS) and data encryption to fine-grained access control (RBAC).
-   **High Performance**: Written in C++17, leveraging modern language features and efficient libraries for low-latency data processing and trade execution.

## Communication Protocols

Different communication protocols are used based on the interaction patterns:

-   **gRPC**: Used for synchronous, high-performance communication between core services (e.g., Trading Engine, Risk Manager, UI Dashboard). It provides strong typing, efficient serialization, and built-in support for streaming.
-   **WebSocket**: Primarily used for real-time, low-latency market data streaming from exchanges to the Price Collector, and potentially for real-time updates to the UI Dashboard.
-   **Redis Pub/Sub**: Utilized for asynchronous, one-to-many messaging, such as distributing market data updates from the Price Collector to other interested modules (e.g., Trading Engine, Backtest Analytics).
-   **HTTP/REST**: Used for less latency-sensitive interactions, such as fetching historical data, configuration management, or interacting with external APIs that do not offer WebSocket streams.

## Data Persistence

-   **InfluxDB**: A time-series database used for storing high-volume, time-stamped data like market data (tickers, order books, trades), historical trading performance, and system metrics.
-   **Redis**: Used as an in-memory data store for caching frequently accessed data (e.g., latest market prices, user session data) and for Pub/Sub messaging.
-   **Filesystem**: Used for persistent storage of sensitive configuration (e.g., encrypted API keys), user preferences, and RBAC data.

For detailed information on each module, refer to their respective documentation pages.