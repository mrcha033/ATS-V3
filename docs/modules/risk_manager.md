# Risk Manager Module

## Purpose

The `risk_manager` module is a critical component of the ATS-V3, responsible for real-time monitoring, assessment, and control of trading risks. Its primary goal is to protect the portfolio from excessive losses and ensure compliance with predefined risk limits.

## Key Components

-   **`EnhancedRiskManager` (`include/enhanced_risk_manager.hpp`, `src/enhanced_risk_manager.cpp`)**:
    The central orchestrator of the risk management module. It extends a base `RiskManager` (implied) and provides comprehensive real-time risk functionalities:
    -   **Real-time P&L and Position Tracking**: Integrates with `RealTimePnLCalculator` to maintain an up-to-date view of the portfolio's profit & loss (P&L) and individual positions across all exchanges.
    -   **Risk Assessment**: The `assess_opportunity_realtime` method performs a comprehensive pre-trade risk assessment for arbitrage opportunities. It evaluates potential trades against various limits, considering exposure, concentration, volatility, and correlation.
    -   **Limit Monitoring**: Continuously monitors and enforces a wide range of risk limits:
        -   **P&L Limits**: Real-time, daily, weekly, and monthly P&L thresholds.
        -   **Exposure Limits**: Maximum total portfolio exposure and individual position exposure limits.
        -   **Concentration Limits**: Limits on the concentration of capital in specific symbols or exchanges.
        -   **VaR (Value at Risk) Limits**: Monitors the portfolio's VaR, a statistical measure of potential loss.
    -   **Automatic Trading Halt**: The `check_and_trigger_halt` function can automatically halt all trading activities if critical risk limits are breached. It also supports `manual_halt` and `resume_after_halt` for manual intervention.
    -   **Risk Alerts**: Generates `RiskAlert` notifications when risk limits are approached or breached. These alerts are rate-limited to prevent spamming and are sent to external systems (e.g., Redis, InfluxDB) for real-time consumption and historical logging.
    -   **Integration with Trading Engine**: Connects to the `trading_engine` via gRPC to stream real-time trade executions, order updates, and balance updates. This data is crucial for continuously updating the risk manager's view of the portfolio and P&L.
    -   **Persistence**: Persists key risk metrics and alerts to InfluxDB for historical analysis, reporting, and auditing.

-   **`RealTimePnLCalculator` (nested within `enhanced_risk_manager.hpp`, `src/enhanced_risk_manager.cpp`)**:
    A sub-component responsible for managing real-time tracking of positions and calculating unrealized and realized P&L. Its functions include:
    -   **Position Updates**: Dynamically updates positions based on incoming trade executions (changes in quantity and price), calculating weighted average prices.
    -   **Market Price Updates**: Receives current market prices to continuously update the unrealized P&L of open positions.
    -   **Persistence**: Persists position data to Redis, enabling the risk manager to recover its state after restarts.
    -   **Risk Metrics Calculation**: Calculates advanced risk metrics such as VaR and portfolio volatility based on historical P&L data.

-   **`RiskAlert` (nested within `enhanced_risk_manager.hpp`)**:
    A data structure defining a risk alert, including its severity (INFO, WARNING, CRITICAL, EMERGENCY), type, message, and associated metadata. These alerts are the primary output of the risk monitoring process.

-   **gRPC Service (`src/risk_manager_grpc_service.cpp`)**:
    Implements the gRPC service for the `RiskManager`. This allows external clients (e.g., the `ui_dashboard` or other microservices) to interact with the risk management functionalities. It exposes methods to:
    -   `GetRiskStatus`: Retrieve the current overall risk status.
    -   `GetPositions`: Get a list of current real-time positions.
    -   `GetPnL`: Retrieve various P&L metrics (total, daily, weekly, monthly, unrealized, realized).
    -   `GetRiskAlerts`: Get a list of recent risk alerts.
    -   `AcknowledgeAlert`: Acknowledge a specific risk alert.
    -   `EmergencyHalt`/`ResumeTrading`: Manually trigger or resume trading halts.
    -   `UpdateRiskLimits`: Update the risk limits (though the implementation is a placeholder).
    -   `StreamRiskAlerts`/`StreamPositionUpdates`: Provide real-time streaming of risk alerts and position updates to subscribed clients.

## Data Flow

1.  **Configuration**: The `EnhancedRiskManager` loads its risk limits and policies from the `ConfigManager`.
2.  **Real-time Data Ingestion**: The `RiskManager` connects to the `trading_engine` via gRPC to stream trade executions, order updates, and balance updates. This data is used to update the `RealTimePnLCalculator`'s internal state.
3.  **Market Price Updates**: The `RealTimePnLCalculator` also receives market price updates (likely from Redis, populated by the `price_collector`) to calculate unrealized P&L.
4.  **Continuous Monitoring**: The `EnhancedRiskManager` runs a dedicated monitoring loop that periodically checks all predefined risk limits (P&L, exposure, concentration, VaR).
5.  **Pre-trade Assessment**: When the `trading_engine` proposes an arbitrage opportunity, the `RiskManager` performs a real-time risk assessment (`assess_opportunity_realtime`) before approving the trade.
6.  **Alerting**: If any risk limits are breached or approached, `RiskAlert`s are generated and sent to Redis (for real-time consumption by `notification_service`) and InfluxDB (for historical logging).
7.  **Automatic Halting**: In severe cases of limit breaches, the `RiskManager` can automatically trigger a trading halt in the `trading_engine`.
8.  **Persistence**: Key risk metrics and alerts are continuously persisted to InfluxDB for long-term storage and analysis.

## Integration with Other Modules

-   **`shared`**: Provides core data types, configuration management, logging, and client interfaces for Redis and InfluxDB.
-   **`trading_engine`**: The primary consumer of the `RiskManager`'s services, sending trade proposals for assessment and receiving commands for trading halts. It also streams execution data to the `RiskManager`.
-   **`price_collector`**: Indirectly provides market price data (via Redis) that the `RealTimePnLCalculator` uses to update unrealized P&L.
-   **`notification_service`**: Subscribes to risk alerts published by the `RiskManager` (via Redis) to deliver them to users.
-   **`ui_dashboard`**: Connects to the `RiskManager`'s gRPC service to display real-time risk status, positions, P&L, and alerts.

## Design Philosophy

The `risk_manager` module is designed with a focus on:
-   **Proactive Control**: Emphasizes pre-trade assessment and automatic halting to prevent losses rather than just reacting to them.
-   **Real-time Visibility**: Provides an up-to-the-minute view of portfolio risk and P&L.
-   **Granularity**: Monitors risk at various levels, from individual positions to overall portfolio exposure.
-   **Configurability**: Allows for flexible definition and adjustment of risk limits and policies.
-   **Robustness**: Designed to operate continuously and reliably, even under high market volatility.
-   **Auditability**: Logs all critical risk events and metrics for compliance and post-mortem analysis.

