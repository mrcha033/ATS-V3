# ATS-V3 API Documentation

## Core Components

### ArbitrageEngine

The main orchestrator for arbitrage trading operations.

```cpp
class ArbitrageEngine {
public:
    ArbitrageEngine(RiskManager* risk_manager, TradeExecutor* trade_executor);
    
    void start();
    void stop();
    void evaluate_opportunity(const ArbitrageOpportunity& opportunity);
};
```

**Usage:**
```cpp
auto risk_manager = container.resolve<RiskManager>();
auto trade_executor = container.resolve<TradeExecutor>();
auto engine = std::make_unique<ArbitrageEngine>(risk_manager.get(), trade_executor.get());

engine->start();
// Engine will now process opportunities automatically
```

### DependencyContainer

Lightweight dependency injection container for managing component lifecycles.

```cpp
class DependencyContainer {
public:
    // Register implementations
    template<typename Interface, typename Implementation, typename... Args>
    void register_singleton();
    
    template<typename Interface, typename Implementation, typename... Args>
    void register_transient();
    
    // Resolve dependencies
    template<typename T>
    std::shared_ptr<T> resolve();
};
```

**Usage:**
```cpp
// Registration (typically in main or initialization)
container.register_singleton<ConfigManager, ConfigManager>();
container.register_transient<ExchangeInterface, BinanceExchange, ConfigManager*>();

// Resolution
auto config = container.resolve<ConfigManager>();
auto exchange = container.resolve<ExchangeInterface>();
```

### StructuredLogger

Enhanced logging system with JSON output and contextual information.

```cpp
class StructuredLogger {
public:
    static void init(const std::string& log_file_path, LogLevel min_level);
    static void info(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    static void trade_executed(const std::string& symbol, const std::string& side, double price, double quantity, const std::string& order_id);
};
```

**Usage:**
```cpp
// Initialize logging
StructuredLogger::init("logs/ats.log", LogLevel::INFO);

// Basic logging
SLOG_INFO("Application started");

// Contextual logging
SLOG_INFO("Processing opportunity", {
    {"symbol", "BTC/USDT"},
    {"profit_percent", "0.25"}
});

// Trading-specific logging
SLOG_TRADE("BTC/USDT", "BUY", 45000.0, 0.001, "order_123");
```

### ConfigValidator

Comprehensive configuration validation with detailed error reporting.

```cpp
class ConfigValidator {
public:
    static ValidationResult validate_config(const nlohmann::json& config);
    static const ValidationErrors& get_errors();
};
```

**Usage:**
```cpp
nlohmann::json config = load_config("config.json");
auto result = ConfigValidator::validate_config(config);

if (result.is_error()) {
    for (const auto& error : ConfigValidator::get_errors()) {
        std::cout << "Error in " << error.field << ": " << error.message << std::endl;
    }
}
```

### PerformanceMonitor

Real-time performance tracking and metrics collection.

```cpp
class PerformanceMonitor {
public:
    static PerformanceMonitor& instance();
    
    void record_order_placed(const std::string& exchange);
    void record_successful_trade(double profit, double slippage_percent);
    bool is_system_healthy() const;
    nlohmann::json get_metrics_json() const;
};
```

**Usage:**
```cpp
// Record metrics
MONITOR_TRADE_PLACED("binance");
MONITOR_SUCCESSFUL_TRADE(15.50, 0.001);

// Performance timing
{
    SCOPED_TIMER("order_processing");
    // ... operation code ...
} // Automatically logs timing

// Health check
if (!PerformanceMonitor::instance().is_system_healthy()) {
    SLOG_WARNING("System health check failed");
}
```

### ThreadPool

High-performance thread pool with priority support.

```cpp
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<...>;
    
    template<typename F, typename... Args>
    auto submit_priority(int priority, F&& f, Args&&... args) -> std::future<...>;
};
```

**Usage:**
```cpp
ThreadPool pool(8);

// Submit regular task
auto future1 = pool.submit([]() {
    return calculate_arbitrage();
});

// Submit high-priority task
auto future2 = pool.submit_priority(10, [](const Order& order) {
    return execute_order(order);
}, urgent_order);

// Wait for results
auto result = future1.get();
```

## Error Handling

### Exception Hierarchy

```cpp
// Base exception
class ATSException : public std::runtime_error;

// Specific exceptions
class ConfigurationError : public ATSException;
class DatabaseError : public ATSException;
class ExchangeError : public ATSException;
class NetworkError : public ATSException;
class RiskManagementError : public ATSException;
class TradingError : public ATSException;
class ValidationError : public ATSException;
```

### Result Type

```cpp
template<typename T>
class Result {
public:
    static Result<T> success(T value);
    static Result<T> error(std::string error);
    
    bool is_success() const;
    bool is_error() const;
    const T& value() const;
    const std::string& error() const;
};
```

**Usage:**
```cpp
Result<Price> get_price(const std::string& symbol) {
    try {
        auto price = fetch_from_exchange(symbol);
        return Result<Price>::success(price);
    } catch (const NetworkError& e) {
        return Result<Price>::error("Network error: " + std::string(e.what()));
    }
}

auto result = get_price("BTC/USDT");
if (result.is_success()) {
    process_price(result.value());
} else {
    SLOG_ERROR("Failed to get price", {{"error", result.error()}});
}
```

## Configuration

### SecureConfig

```cpp
class SecureConfig {
public:
    bool load_secure_config(const std::string& json_file_path);
    std::optional<std::string> get_exchange_api_key(const std::string& exchange_name) const;
    bool validate_exchange_credentials(const std::string& exchange_name) const;
};
```

Environment variable format:
- `BINANCE_API_KEY` / `BINANCE_SECRET_KEY`
- `UPBIT_API_KEY` / `UPBIT_SECRET_KEY`
- `TELEGRAM_BOT_TOKEN`
- `DISCORD_WEBHOOK_URL`

## Threading and Concurrency

### AtomicCounter and RateLimiter

```cpp
// Thread-safe counter
AtomicCounter counter;
auto new_value = counter.increment();

// Rate limiting
RateLimiter limiter(100, std::chrono::seconds(60)); // 100 requests per minute
if (limiter.try_acquire()) {
    // Make API request
}
```

### ThreadSafeQueue

```cpp
ThreadSafeQueue<Event> event_queue;

// Producer thread
event_queue.push(event);

// Consumer thread
Event event;
if (event_queue.try_pop(event)) {
    process_event(event);
}
```

## Best Practices

### 1. Use Dependency Injection

```cpp
// Good: Testable and flexible
class TradingService {
public:
    TradingService(std::shared_ptr<ExchangeInterface> exchange,
                   std::shared_ptr<RiskManager> risk_manager)
        : exchange_(exchange), risk_manager_(risk_manager) {}
};

// Register in container
container.register_singleton<TradingService, TradingService, 
                           std::shared_ptr<ExchangeInterface>,
                           std::shared_ptr<RiskManager>>();
```

### 2. Use Structured Logging

```cpp
// Good: Searchable and structured
SLOG_INFO("Order processing started", {
    {"order_id", order.id},
    {"symbol", order.symbol},
    {"side", order.side == OrderSide::BUY ? "BUY" : "SELL"}
});

// Avoid: Unstructured strings
Logger::info("Processing order " + order.id + " for " + order.symbol);
```

### 3. Handle Errors Gracefully

```cpp
// Use Result type for recoverable errors
auto result = exchange->place_order(order);
if (result.is_error()) {
    SLOG_ERROR("Order placement failed", {
        {"error", result.error()},
        {"order_id", order.id}
    });
    return false;
}

// Use exceptions for programming errors
if (quantity <= 0) {
    throw ValidationError("Order quantity must be positive");
}
```

### 4. Monitor Performance

```cpp
void process_arbitrage_opportunity(const ArbitrageOpportunity& opp) {
    SCOPED_TIMER("arbitrage_processing");
    
    MONITOR_OPPORTUNITY(opp.symbol, opp.profit);
    
    if (execute_trade(opp)) {
        MONITOR_SUCCESSFUL_TRADE(opp.profit, opp.slippage);
    } else {
        MONITOR_FAILED_TRADE("Execution failed");
    }
}
```

### 5. Use Thread Pool for I/O Operations

```cpp
// Submit network operations to thread pool
auto futures = std::vector<std::future<Price>>{};

for (const auto& symbol : symbols) {
    futures.push_back(thread_pool.submit([symbol, this]() {
        return fetch_price(symbol);
    }));
}

// Collect results
for (auto& future : futures) {
    auto price = future.get();
    process_price(price);
}
```