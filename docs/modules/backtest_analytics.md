# Backtest Analytics Module

## Purpose

The `backtest_analytics` module provides a comprehensive framework for evaluating trading strategies against historical market data. It allows users to simulate how a strategy would have performed in the past, analyze its profitability and risk characteristics, and optimize its parameters before deployment in live trading.

## Key Components

-   **`BacktestEngine` (`include/backtest_engine.hpp`, `src/backtest_engine.cpp`)**:
    The core component of the backtesting module. Its responsibilities include:
    -   **Strategy Simulation**: Simulates the execution of a given trading strategy over a specified historical period.
    -   **Event-Driven Processing**: Processes historical market data events (e.g., ticker updates, trade events) chronologically, mimicking real-time market conditions. This ensures accurate simulation of strategy reactions.
    -   **Order Execution Simulation**: Simulates order placement, execution, and fills, accounting for realistic factors such as slippage, trading fees, and order types (market, limit).
    -   **P&L Calculation**: Tracks the simulated profit and loss of the strategy throughout the backtest, including unrealized and realized P&L.
    -   **Integration with `DataLoader`**: Utilizes the `DataLoader` to efficiently fetch and manage historical market data required for the simulation.
    -   **Integration with `PerformanceMetrics`**: After the backtest, it feeds the simulated trade results and portfolio history to the `PerformanceMetrics` component for comprehensive performance evaluation.
    -   **Configurable Parameters**: Allows users to configure various backtesting parameters, such as the time range, initial capital, trading fees, slippage models, and strategy-specific settings.
    -   **Callbacks**: Provides an event-driven interface with callbacks for strategy events (e.g., `on_tick`, `on_order_fill`, `on_trade`), enabling flexible strategy implementation.

-   **`DataLoader` (`include/data_loader.hpp`, `src/data_loader.cpp`)**:
    Responsible for loading and preparing historical market data for backtesting. Its features include:
    -   **Multiple Data Sources**: Can load data from various sources:
        -   **InfluxDB**: Integrates with `InfluxDBStorage` to retrieve historical market data (tickers, order books, trades) that was previously collected and stored by the `price_collector` module. This is the primary source for comprehensive historical data.
        -   **CSV Files**: Supports loading data from local CSV files, providing flexibility for offline analysis or when specific datasets are not in InfluxDB.
    -   **Data Filtering**: Allows filtering historical data by symbol, exchange, and specific time ranges to focus the backtest.
    -   **Data Resampling/Aggregation**: Can resample or aggregate raw tick data into different timeframes (e.g., 1-minute bars, 1-hour bars, daily bars), enabling backtesting on various granularities.

-   **`PerformanceMetrics` (`include/performance_metrics.hpp`, `src/performance_metrics.cpp`)**:
    Calculates and reports a wide array of performance metrics to thoroughly evaluate a trading strategy. Key metrics include:
    -   **Return Metrics**: Total Return, Annualized Return, Compound Annual Growth Rate (CAGR).
    -   **Risk-Adjusted Returns**: Sharpe Ratio, Sortino Ratio, Calmar Ratio.
    -   **Drawdown Metrics**: Maximum Drawdown, Average Drawdown, Drawdown Duration.
    -   **Trade Statistics**: Win Rate, Loss Rate, Profit Factor (Gross Profit / Gross Loss), Average Win, Average Loss, Largest Win, Largest Loss, Total Trade Count.
    -   **Efficiency Metrics**: Average Holding Period, Slippage Analysis, Commission Impact.
    -   **Reporting**: Can generate detailed performance reports, which can be output to the console, saved to CSV files, or stored in InfluxDB for historical tracking.

-   **`InfluxDBStorage` (`include/influxdb_storage.hpp`, `src/influxdb_storage.cpp`)**:
    Provides the interface for the `backtest_analytics` module to interact with InfluxDB. It uses `ats::utils::InfluxDBClient` (from the `shared` module) for database operations. Its primary role here is to retrieve historical market data for the `DataLoader`.

-   **`AIPredictionModule` (`include/ai_prediction_module.hpp`, `src/ai_prediction_module.cpp`)**:
    This module signifies the integration of Artificial Intelligence and Machine Learning models into the backtesting and strategy development process. Its functionalities include:
    -   **Prediction Generation**: Generates simulated trading signals or price predictions based on historical data, which can then be fed into the `BacktestEngine`.
    -   **Model Integration**: Designed to integrate with external AI/ML models (e.g., those developed using TensorFlow, PyTorch, or scikit-learn). The current implementation serves as a placeholder, outlining the interface for such integration.
    -   **Feature Engineering**: Can perform feature engineering on raw market data to create suitable inputs for AI models.
    -   **Evaluation**: Enables the evaluation of AI model performance within the realistic context of a backtesting framework.

## Data Flow

1.  **Data Loading**: The `BacktestEngine` requests historical data from the `DataLoader`.
2.  **Data Retrieval**: The `DataLoader` retrieves market data from `InfluxDBStorage` (which queries InfluxDB) or local CSV files.
3.  **Strategy Simulation**: The `BacktestEngine` iterates through the historical data, feeding events to the simulated trading strategy.
4.  **Order Execution**: The strategy generates orders, which the `BacktestEngine` simulates, accounting for fees and slippage.
5.  **P&L Tracking**: The engine continuously calculates the simulated portfolio P&L.
6.  **Performance Evaluation**: After the backtest, the `PerformanceMetrics` component analyzes the simulated trades and portfolio history to generate a comprehensive performance report.
7.  **AI Integration**: The `AIPredictionModule` can generate signals or predictions that influence the strategy's decisions within the `BacktestEngine`.

## Integration with Other Modules

-   **`shared`**: Provides core data types, logging, and the `InfluxDBClient` interface for data storage and retrieval.
-   **`price_collector`**: Indirectly provides the historical market data stored in InfluxDB, which is then consumed by the `DataLoader`.
-   **`trading_engine`**: The backtesting process simulates the logic of the `trading_engine` to evaluate its performance under historical conditions.
-   **`ui_dashboard`**: Expected to provide a user interface for configuring and running backtests, and for visualizing the backtest results and performance reports.

## Design Philosophy

The `backtest_analytics` module is designed for:
-   **Accuracy**: Simulates market conditions and order execution as realistically as possible.
-   **Flexibility**: Supports various data sources, timeframes, and strategy types.
-   **Comprehensiveness**: Provides a wide range of performance metrics for thorough evaluation.
-   **Extensibility**: Allows for easy integration of new data sources, strategy logic, and AI models.
-   **Research & Development**: Serves as a vital tool for developing, testing, and optimizing new trading strategies.

