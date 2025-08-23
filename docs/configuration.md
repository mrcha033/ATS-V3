# Configuration Reference

ATS-V3's behavior is primarily controlled through the `config/settings.json` file. This file allows you to configure various aspects of the system, including exchange API keys, database connections, risk limits, logging preferences, and module-specific parameters.

## `settings.json` Structure

The `settings.json` file is a JSON document organized into logical sections. Below is an overview of the typical structure and key parameters.

```json
{
  "exchanges": [
    {
      "id": "binance",
      "name": "Binance",
      "api_key": "YOUR_BINANCE_API_KEY",
      "secret_key": "YOUR_BINANCE_SECRET_KEY",
      "passphrase": "",
      "enabled": true,
      "sandbox_mode": false,
      "rate_limit": 1200,
      "timeout_ms": 5000,
      "parameters": {
        "rest_api_url": "https://api.binance.com",
        "websocket_url": "wss://stream.binance.com:9443/ws",
        "trading_enabled": "true"
      }
    },
    {
      "id": "upbit",
      "name": "Upbit",
      "api_key": "YOUR_UPBIT_API_KEY",
      "secret_key": "YOUR_UPBIT_SECRET_KEY",
      "passphrase": "",
      "enabled": true,
      "sandbox_mode": false,
      "rate_limit": 100,
      "timeout_ms": 5000,
      "parameters": {
        "rest_api_url": "https://api.upbit.com/v1",
        "websocket_url": "wss://api.upbit.com/websocket"
      }
    }
    // ... other exchanges
  ],
  "database": {
    "redis_host": "redis",
    "redis_port": 6379,
    "redis_password": "",
    "influxdb_host": "influxdb",
    "influxdb_port": 8086,
    "influxdb_username": "admin",
    "influxdb_password": "password",
    "influxdb_database": "ats",
    "rocksdb_path": "./data/rocksdb",
    "enable_ssl": false
  },
  "monitoring": {
    "log_level": "INFO",
    "log_file_path": "logs/ats.log",
    "log_max_file_size": 10485760,
    "log_max_files": 5,
    "metrics_enabled": true,
    "prometheus_port": 9090,
    "dashboard_port": 8080,
    "notification_email": "alerts@example.com",
    "notification_webhook": "",
    "enable_performance_monitoring": true
  },
  "security": {
    "master_key": "", // Leave empty for auto-generation or load from file
    "encrypt_config": true,
    "encrypt_logs": false,
    "session_timeout_minutes": 60,
    "enable_2fa": false,
    "jwt_secret": "YOUR_JWT_SECRET",
    "jwt_expiry_hours": 24
  },
  "trading": {
    "enabled": true,
    "min_spread_threshold": 0.005,
    "max_position_size": 10000.0,
    "max_daily_volume": 100000.0,
    "max_daily_trades": 100,
    "emergency_stop_loss": 0.02,
    "commission_rate": 0.001,
    "allowed_symbols": []
  },
  "risk": {
    "max_portfolio_risk": 0.05,
    "max_single_trade_risk": 0.01,
    "stop_loss_percentage": 0.02,
    "take_profit_percentage": 0.01,
    "max_drawdown": 0.05,
    "max_daily_loss": 1000.0,
    "max_position_concentration": 0.3
  },
  "trading_engine": {
    "enabled": true,
    "min_spread_threshold": 0.005,
    "max_position_size": 10000.0,
    "max_daily_volume": 100000.0,
    "max_concurrent_trades": 5,
    "execution_timeout": 30000,
    "opportunity_timeout": 5000,
    "max_portfolio_exposure": 0.8,
    "max_single_trade_size": 0.1,
    "emergency_stop_loss": 0.05,
    "worker_thread_count": 4,
    "max_queue_size": 1000,
    "enable_paper_trading": false,
    "enable_rollback_on_failure": true,
    "metrics_port": 8082
  },
  "price_collector": {
    "enabled": true,
    "update_interval_ms": 1000,
    "enable_websocket_reconnect": true,
    "max_reconnect_attempts": 10,
    "reconnect_delay_ms": 5000
  },
  "backtest_analytics": {
    "enabled": true,
    "data_source": "influxdb", // "influxdb" or "csv"
    "csv_data_path": "./data/historical_csv/",
    "initial_capital": 100000.0,
    "default_slippage_rate": 0.0005,
    "default_commission_rate": 0.001,
    "enable_ai_prediction": false
  },
  "notification_service": {
    "enabled": true,
    "email": {
      "smtp_server": "smtp.gmail.com",
      "smtp_port": 587,
      "username": "your_email@gmail.com",
      "password": "your_email_password",
      "from_email": "noreply@ats-trading.com",
      "from_name": "ATS Trading System",
      "use_tls": true
    },
    "push": {
      "firebase_server_key": "YOUR_FIREBASE_SERVER_KEY",
      "firebase_sender_id": "YOUR_FIREBASE_SENDER_ID",
      "firebase_project_id": "YOUR_FIREBASE_PROJECT_ID",
      "enabled": true
    },
    "slack": {
      "webhook_url": "",
      "enabled": false
    }
  }
}
```

## Key Sections and Parameters

### `exchanges`

An array of objects, each configuring a specific cryptocurrency exchange integration.

-   `id`: Unique identifier for the exchange (e.g., `binance`, `upbit`).
-   `name`: Human-readable name.
-   `api_key`, `secret_key`, `passphrase`: Your exchange API credentials. **These are highly sensitive and should be protected.**
-   `enabled`: `true` to enable this exchange, `false` to disable.
-   `sandbox_mode`: `true` to connect to the exchange's testnet/sandbox environment.
-   `rate_limit`: API request rate limit (requests per minute).
-   `timeout_ms`: Default timeout for API requests in milliseconds.
-   `parameters`: A map for exchange-specific settings, such as `rest_api_url`, `websocket_url`, and `trading_enabled`.

### `database`

Configures connections to Redis and InfluxDB.

-   `redis_host`, `redis_port`, `redis_password`: Connection details for the Redis server.
-   `influxdb_host`, `influxdb_port`, `influxdb_username`, `influxdb_password`, `influxdb_database`: Connection details for the InfluxDB server.
-   `rocksdb_path`: Path for RocksDB local storage (used by some modules for persistent caching).
-   `enable_ssl`: Enable SSL for database connections.

### `monitoring`

Settings related to logging and metrics.

-   `log_level`: Minimum logging level (`TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `CRITICAL`).
-   `log_file_path`, `log_max_file_size`, `log_max_files`: File logging settings.
-   `metrics_enabled`: Enable/disable Prometheus metrics exposition.
-   `prometheus_port`: Port for Prometheus metrics endpoint.
-   `dashboard_port`: Port for the UI dashboard (if running as a web service).
-   `notification_email`, `notification_webhook`: Default recipients for system notifications.
-   `enable_performance_monitoring`: Enable detailed performance metrics collection.

### `security`

Core security configurations.

-   `master_key`: The master encryption key. Leave empty for auto-generation. **Do not hardcode in production.**
-   `encrypt_config`: `true` to encrypt the `settings.json` file itself.
-   `enable_2fa`: Enable/disable two-factor authentication.
-   `jwt_secret`, `jwt_expiry_hours`: Settings for JSON Web Tokens.

### `trading`

General trading parameters for the `trading_engine`.

-   `enabled`: Enable/disable automated trading.
-   `min_spread_threshold`: Minimum profitable spread percentage to consider an arbitrage opportunity.
-   `max_position_size`, `max_daily_volume`, `max_daily_trades`: Limits for trading activity.
-   `emergency_stop_loss`: Portfolio percentage loss that triggers an emergency halt.
-   `commission_rate`: Default commission rate for trades.
-   `allowed_symbols`: List of symbols allowed for trading.

### `risk`

Global risk management limits.

-   `max_portfolio_risk`, `max_single_trade_risk`: Maximum percentage of portfolio/trade at risk.
-   `stop_loss_percentage`, `take_profit_percentage`: Default stop-loss/take-profit levels.
-   `max_drawdown`: Maximum acceptable portfolio drawdown.
-   `max_daily_loss`: Maximum daily monetary loss.
-   `max_position_concentration`: Maximum percentage of portfolio in a single asset.

### `trading_engine` (Module-specific)

Specific configurations for the `trading_engine` module.

-   `worker_thread_count`: Number of threads processing arbitrage opportunities.
-   `enable_paper_trading`: `true` to simulate trades without real money.
-   `enable_rollback_on_failure`: `true` to attempt to mitigate losses on failed trades.
-   `metrics_port`: Port for the trading engine's Prometheus metrics endpoint.

### `price_collector` (Module-specific)

Specific configurations for the `price_collector` module.

-   `update_interval_ms`: How often to fetch market data via REST (if not using WebSockets).
-   `enable_websocket_reconnect`, `max_reconnect_attempts`, `reconnect_delay_ms`: WebSocket reconnection settings.

### `backtest_analytics` (Module-specific)

Specific configurations for the `backtest_analytics` module.

-   `data_source`: `influxdb` or `csv` for historical data.
-   `csv_data_path`: Directory for CSV historical data files.
-   `initial_capital`: Starting capital for backtests.
-   `enable_ai_prediction`: Enable/disable AI prediction module integration.

### `notification_service` (Module-specific)

Specific configurations for the `notification_service` module.

-   `email`: SMTP server details for sending emails.
-   `push`: Firebase Cloud Messaging (FCM) credentials for push notifications.
-   `slack`: Webhook URL for Slack notifications.

## Managing Configuration

ATS-V3 uses a `ConfigManager` (from the `shared` module) that provides advanced features for managing `settings.json`:

-   **Environment Variable Overrides**: Most parameters can be overridden by environment variables prefixed with `ATS_`. For example, `ATS_REDIS_HOST=my_redis_server` will override `database.redis_host`.
-   **Encrypted Configuration**: The `security` module can encrypt the entire `settings.json` file to protect sensitive data at rest. This is controlled by `security.encrypt_config`.
-   **Hot Reloading**: The `ConfigManager` can monitor `settings.json` for changes and automatically reload the configuration without restarting the application (if enabled).

