# Trading Engine Module

## Purpose

The `trading_engine` module is the core of the ATS-V3, responsible for executing arbitrage strategies, managing orders across multiple exchanges, and ensuring the reliable and efficient execution of trades. It acts as the central brain for all trading activities.

## Key Components

-   **`TradingEngineService` (`include/trading_engine_service.hpp`, `src/trading_engine_service.cpp`)**:
    The central orchestrator of the trading engine. Its responsibilities include:
    -   **Lifecycle Management**: Initializes, starts, and stops the entire trading engine, coordinating its internal components.
    -   **Configuration**: Loads and updates its operational parameters (e.g., minimum spread, maximum position size, concurrent trades) from the `ConfigManager`.
    -   **Arbitrage Execution**: The `execute_arbitrage` method is the primary entry point for processing and executing detected arbitrage opportunities. It validates opportunities, checks risk limits, and dispatches orders to the `OrderRouter`.
    -   **Manual Trading**: Provides a `submit_manual_trade` method for testing or manual intervention.
    -   **Position & Balance Management**: Aggregates and provides access to portfolio and balance information across all integrated exchanges via the `OrderRouter`.
    -   **Risk Management Integration**: Interacts with the `RiskManager` to ensure trades adhere to predefined risk limits. It supports an `emergency_stop` function to halt all trading activities in critical situations.
    -   **Statistics & Monitoring**: Tracks comprehensive trading statistics (e.g., total opportunities, successful/failed trades, profit/loss, volume, execution times) and exposes them for monitoring. It integrates with `PrometheusExporter` for metrics.
    -   **Threading**: Utilizes a multi-threaded architecture with worker threads to process arbitrage opportunities from a queue, and dedicated threads for price monitoring and statistics updates, ensuring high concurrency and responsiveness.
    -   **Paper Trading**: Supports a paper trading mode for simulating trade executions without real capital, enabling safe testing and development.

-   **`OrderRouter` (`include/order_router.hpp`, `src/order_router.cpp`)**:
    Responsible for intelligent routing and management of orders across different exchanges. Key features include:
    -   **Exchange Integration**: Manages a collection of `ExchangeTradingInterface` instances, each representing a connection to a specific exchange for trading operations.
    -   **Asynchronous Order Placement**: Supports non-blocking order submission (`place_order_async`) for low-latency execution.
    -   **Simultaneous Execution**: The `execute_arbitrage_orders_sync` method is crucial for arbitrage, as it attempts to place buy and sell orders on different exchanges concurrently. It handles execution timeouts and partial successes.
    -   **Order Monitoring**: Continuously tracks the status of active orders and updates them based on real-time responses from exchanges.
    -   **Pre-trade Validation**: Performs essential checks before order submission, such as verifying sufficient balance, validating order parameters, and assessing current market conditions.
    -   **Performance Metrics**: Collects and exposes order-level performance metrics (e.g., total orders, success/failure rates, average execution times, total fees paid).

-   **`ExchangeTradingAdapter` (`include/exchange_trading_adapter.hpp`, `src/exchange_trading_adapter.cpp`)**:
    An adapter that implements the `ExchangeTradingInterface` (defined in `order_router.hpp`). It wraps the `ats::ExchangeInterface` (from the `shared` module) to provide trading-specific functionalities. It includes concrete implementations for exchanges like Binance and Upbit, utilizing an `ExchangeRestClient` for API interactions.

-   **`ExchangeRestClient` (defined within `exchange_trading_adapter.hpp`, `src/exchange_trading_adapter.cpp`)**:
    A dedicated REST client built on `libcurl` for making authenticated API calls to exchanges. It handles API key authentication, request signing (HMAC-SHA256/512), and basic rate limiting to ensure compliance with exchange API policies.

-   **`SpreadCalculator` (`include/spread_calculator.hpp`, `src/spread_calculator.cpp`)**:
    A critical component for identifying and analyzing arbitrage opportunities. Its functions include:
    -   **Market Data Updates**: Receives real-time `Ticker` and `MarketDepth` updates.
    -   **Spread Analysis**: Calculates raw, effective, and breakeven spreads between exchanges for given symbols and quantities.
    -   **Opportunity Detection**: Identifies profitable arbitrage opportunities based on configurable thresholds.
    -   **Fee & Slippage Calculation**: Accurately accounts for exchange trading fees (maker/taker) and estimates slippage using sophisticated models, crucial for determining true profitability.
    -   **Market Microstructure Analysis**: Can analyze market depth, volatility, and liquidity to refine opportunity assessment.

-   **`RedisSubscriber` (`include/redis_subscriber.hpp`, `src/redis_subscriber.cpp`)**:
    Subscribes to Redis channels to receive real-time market data updates (e.g., price changes) from the `price_collector` module. It processes these messages and notifies the `TradingEngineService` for opportunity detection.

-   **`TradeLogger` (`include/redis_subscriber.hpp`, `src/trade_logger.cpp`)**:
    Logs various trading events, including detailed trade executions, detected arbitrage opportunities, and order execution details. It persists this data to InfluxDB for structured, time-series analysis and can also log to local CSV files for backup.

-   **`EnhancedRollbackManager` (`include/rollback_manager.hpp`, `src/rollback_manager.cpp`)**:
    A sophisticated component designed to handle failed or partially executed arbitrage trades. It supports various rollback strategies (e.g., immediate cancel, market close, gradual liquidation, hedging) based on configurable triggers and severity levels, minimizing potential losses.

-   **gRPC Services (`grpc/trading_engine_grpc_service.hpp`)**:
    Exposes the `TradingEngineService`, `SpreadCalculator`, and `OrderRouter` functionalities via gRPC. This enables seamless communication with external clients, such as the `ui_dashboard` or other microservices, allowing for remote control and monitoring.

## Data Flow

1.  **Market Data Ingestion**: The `RedisSubscriber` receives real-time market data (e.g., ticker updates) from the `price_collector` via Redis Pub/Sub.
2.  **Opportunity Detection**: The `TradingEngineService` passes market data to the `SpreadCalculator`, which continuously analyzes spreads and detects arbitrage opportunities.
3.  **Risk Assessment**: Detected opportunities are sent to the `RiskManager` (via gRPC) for pre-trade risk assessment and approval.
4.  **Trade Execution**: Approved opportunities are passed to the `OrderRouter`, which places simultaneous buy and sell orders on the respective exchanges using `ExchangeTradingAdapter`s.
5.  **Order Monitoring**: The `OrderRouter` monitors order execution status and reports back to the `TradingEngineService`.
6.  **Logging & Monitoring**: All trade executions, opportunities, and key metrics are logged by the `TradeLogger` to InfluxDB and exposed via Prometheus for real-time monitoring.
7.  **Rollback**: In case of partial fills or execution failures, the `EnhancedRollbackManager` is triggered to mitigate losses.

## Integration with Other Modules

-   **`shared`**: Provides fundamental data types, configuration management, logging, and the core resilient exchange integration framework.
-   **`price_collector`**: Supplies the real-time market data necessary for opportunity detection.
-   **`risk_manager`**: Performs pre-trade risk assessment and provides real-time risk monitoring, influencing trade execution decisions.
-   **`notification_service`**: Receives trade execution and error events to send alerts to users.
-   **`ui_dashboard`**: Interacts with the `trading_engine` via gRPC to display real-time trading activity, P&L, and allow user control.
-   **`backtest_analytics`**: Can use the `trading_engine`'s logic for simulating trades during backtesting.

## Design Philosophy

The `trading_engine` module is built with a focus on:
-   **Performance**: Low-latency execution and high-throughput processing of opportunities.
-   **Reliability**: Robust error handling, rollback mechanisms, and resilient exchange interactions.
-   **Accuracy**: Precise calculation of spreads, fees, and slippage to ensure profitable trades.
-   **Extensibility**: Modular design allows for easy integration of new trading strategies or exchange-specific logic.
-   **Observability**: Comprehensive logging and metrics for deep insights into trading operations.

