# Shared Module

## Purpose

The `shared` module serves as the foundational library for the entire ATS-V3 system. It encapsulates common functionalities, core data structures, and cross-cutting concerns that are utilized by multiple other modules. This promotes code reusability, consistency, and simplifies the development of other components.

## Key Components

-   **Core Data Types (`types/common_types.hpp`)**:
    Defines all fundamental data structures and enumerations used throughout the application, such as `Ticker`, `OrderBook`, `Order`, `Trade`, `Balance`, `Position`, `ArbitrageOpportunity`, and various configuration structs (`ExchangeConfig`, `TradingConfig`, `RiskConfig`). This ensures a single source of truth for data models.

-   **Configuration Management (`config/config_manager.hpp`)**:
    A robust configuration manager capable of loading settings from JSON files. It supports encrypted configuration, hot-reloading of settings, environment variable overrides, and configuration validation. It allows other modules to access their specific configuration sections.

-   **Logging (`utils/logger.hpp`)**:
    Provides a flexible logging framework, typically wrapping `spdlog`, with support for different log levels (TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL), console and file output, and structured logging for trading-specific events. Includes performance measurement utilities like `ScopedTimer`.

-   **Cryptography Utilities (`utils/crypto_utils.hpp`)**:
    Offers a suite of cryptographic functions based on OpenSSL, including AES-256-GCM encryption/decryption, HMAC-SHA256/SHA512 for message authentication, and secure string handling (`SecureString`). It also provides helpers for generating exchange-specific API signatures.

-   **Resilient Exchange Integration (`exchange/` directory)**:
    This is a highly sophisticated sub-module providing a resilient and extensible framework for interacting with cryptocurrency exchanges. It includes:
    -   **`IExchangePlugin`**: The core interface for all exchange adapters.
    -   **`BaseExchangePlugin`**: Provides common implementation for exchange adapters, handling state, callbacks, rate limiting, and basic statistics.
    -   **`ExchangePluginManager`**: Manages the dynamic loading, unloading, and lifecycle of exchange plugins (DLLs/shared objects), supporting hot-reloading and built-in plugins.
    -   **`FailoverManager`**: Monitors the health of multiple exchange connections and automatically switches to a healthy alternative if the primary fails.
    -   **`ResilientExchangeAdapter`**: Wraps exchange connections with advanced resilience patterns, including circuit breakers (to prevent cascading failures) and automatic retry logic.
    -   **`ExchangeNotificationSystem`**: A system for sending notifications about events within the exchange layer (e.g., connection issues, rate limit breaches) to various channels.

-   **Database/Cache Client Interfaces (`utils/influxdb_client.hpp`, `utils/redis_client.hpp`)**:
    Defines abstract interfaces for interacting with InfluxDB (time-series database) and Redis (in-memory data store/message broker). Concrete implementations are expected to be provided elsewhere or linked as external libraries.

-   **Prometheus Exporter (`utils/prometheus_exporter.hpp`)**:
    Provides an interface for exposing application metrics in a Prometheus-compatible format, enabling comprehensive monitoring of service health and performance.

## Integration with Other Modules

The `shared` module is a dependency for almost all other modules in ATS-V3. It provides the common language, tools, and infrastructure for inter-module communication and data handling. For instance, the `price_collector` heavily relies on the `ResilientExchangeAdapter` and `IExchangePlugin` for market data collection, while the `trading_engine` uses the core data types and configuration management.

## Design Philosophy

The `shared` module emphasizes:
-   **Abstraction**: Providing clear interfaces to hide complex underlying implementations (e.g., `IExchangePlugin`, `InfluxDBClient`).
-   **Reusability**: Centralizing common code to avoid duplication.
-   **Robustness**: Implementing resilience patterns to ensure stability in a volatile environment.
-   **Security**: Integrating cryptographic primitives and secure practices at a fundamental level.

