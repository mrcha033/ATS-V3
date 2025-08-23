# Price Collector Module

## Purpose

The `price_collector` module is responsible for establishing and maintaining connections with various cryptocurrency exchanges, collecting real-time market data (tickers, order books, trades), and persisting this data for use by other modules and for historical analysis.

## Key Components

-   **`BaseExchangeAdapter` (`include/exchange_interface.hpp`)**:
    This is the base class for all exchange-specific adapters within the `price_collector`. It inherits from `ats::exchange::BaseExchangePlugin` (from the `shared` module), ensuring that all price collector adapters conform to the `IExchangePlugin` interface. This allows them to be managed by the `ExchangePluginManager` and benefit from the `ResilientExchangeAdapter`'s features (failover, circuit breaker, retries).

-   **Exchange Adapters (`adapters/` directory)**:
    Concrete implementations of `BaseExchangeAdapter` for supported exchanges (e.g., `BinanceAdapter`, `BitfinexAdapter`, `CoinbaseAdapter`, `KrakenAdapter`, `UpbitAdapter`). Each adapter is responsible for:
    -   Connecting to exchange-specific WebSocket and REST APIs.
    -   Implementing subscription logic for market data streams.
    -   Parsing raw JSON messages from exchanges into standardized `ats::types` data structures.
    -   Handling exchange-specific symbol formatting and API nuances.
    -   (Optional) Implementing trading operations if the public API supports it (e.g., Binance).

-   **HTTP Client (`include/http_client.hpp`, `src/http_client.cpp`)**:
    A custom HTTP client built on `libcurl` for making REST API calls to exchanges. It handles GET, POST, and DELETE requests, along with setting headers and timeouts.

-   **WebSocket Client (`include/websocket_client.hpp`, `src/websocket_client.cpp`)**:
    A custom WebSocket client built on `websocketpp` for establishing and managing real-time data streams from exchanges. It supports both secure (wss://) and insecure (ws://) connections and provides callbacks for message reception, connection status, and errors.

-   **Market Data Storage (`include/market_data_storage.hpp`, `src/market_data_storage.cpp`)**:
    Manages the persistence of collected market data. It utilizes:
    -   `ats::utils::InfluxDBClient` (from `shared`) for storing historical time-series data (tickers, order books, trades).
    -   `ats::utils::RedisClient` (from `shared`) for caching the latest market data snapshots.
    -   It operates on a separate thread to process data asynchronously, preventing blocking of the data collection process.

-   **Performance Monitor (`include/performance_monitor.hpp`)**:
    Integrates with `ats::utils::PrometheusExporter` (from `shared`) to expose metrics specific to the price collection process. This includes message rates, parsing latencies, storage latencies, and API error counts, providing crucial operational visibility.

-   **Price Collector Service (`include/price_collector_service.hpp`, `src/price_collector_service.cpp`)**:
    The main orchestration service for the `price_collector` module. Its responsibilities include:
    -   Loading exchange configurations and instantiating `BaseExchangeAdapter`s.
    -   Wrapping each adapter with a `ats::exchange::ResilientExchangeAdapter` to ensure high resilience.
    -   Setting up callbacks from adapters to process incoming market data.
    -   Passing processed data to `MarketDataStorage` for persistence and caching.
    -   Managing subscriptions to market data streams.
    -   Running a health check loop to monitor exchange connections and trigger reconnections.

## Data Flow

1.  **Configuration**: The `PriceCollectorService` loads exchange configurations from the `ConfigManager`.
2.  **Connection**: It initializes `BaseExchangeAdapter` instances for each configured exchange, which then use `HttpClient` and `WebsocketClient` to connect to exchange APIs.
3.  **Data Collection**: Exchange adapters subscribe to market data streams (tickers, order books, trades) and make REST API calls for snapshot data.
4.  **Parsing & Normalization**: Raw data from exchanges is parsed and converted into standardized `ats::types` data structures.
5.  **Resilience**: The `ResilientExchangeAdapter` handles retries, failovers, and circuit breaking for all exchange interactions.
6.  **Data Processing**: The `PriceCollectorService` receives the normalized data via callbacks.
7.  **Storage & Caching**: Data is sent to `MarketDataStorage` for: 
    -   Long-term historical storage in InfluxDB.
    -   Caching of latest values in Redis for quick access by other modules.
8.  **Monitoring**: `PerformanceMonitor` collects and exposes metrics about the data collection process.

## Integration with Other Modules

-   **`shared`**: Heavily relies on `shared` for core data types, configuration, logging, crypto utilities, and especially the resilient exchange integration framework.
-   **`trading_engine`**: Consumes market data (latest prices, order books) from Redis, which is populated by the `price_collector`.
-   **`backtest_analytics`**: Uses historical market data stored in InfluxDB by the `price_collector` for strategy backtesting.
-   **`ui_dashboard`**: Displays real-time market data and connection statuses provided by the `price_collector` (indirectly via Redis or direct queries).

## Design Philosophy

The `price_collector` module is designed for:
-   **High Throughput**: Efficiently handles large volumes of real-time market data.
-   **Reliability**: Employs robust error handling, reconnection logic, and resilience patterns to ensure continuous data flow.
-   **Extensibility**: The adapter pattern allows for easy integration of new exchanges.
-   **Accuracy**: Ensures data is correctly parsed and normalized across different exchange formats.

