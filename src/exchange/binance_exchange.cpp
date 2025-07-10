#include "binance_exchange.hpp"
#include "../utils/secure_config.hpp"
#include <stdexcept>

namespace ats {

BinanceExchange::BinanceExchange(const ExchangeConfig& config, AppState* app_state)
    : ExchangeInterface(config, app_state), config_(config), app_state_(app_state), 
      status_(ExchangeStatus::DISCONNECTED) {
    
    SLOG_INFO("Initializing Binance exchange", {
        {"exchange", "binance"},
        {"base_url", config.base_url},
        {"testnet", config.testnet ? "true" : "false"}
    });
    
    initialize_api();
}

BinanceExchange::~BinanceExchange() {
    disconnect();
}

std::string BinanceExchange::get_name() const {
    return "binance";
}

void BinanceExchange::connect() {
    SLOG_INFO("Connecting to Binance exchange");
    
    if (!api_) {
        if (!initialize_api()) {
            handle_connection_error("Failed to initialize Binance API");
            return;
        }
    }
    
    status_ = ExchangeStatus::CONNECTING;
    
    // Test connectivity
    if (!api_->test_connectivity()) {
        handle_connection_error("Connectivity test failed");
        return;
    }
    
    // Test authentication by getting account info
    auto account = api_->get_account_info();
    if (api_->get_last_error().is_error()) {
        handle_connection_error("Authentication failed: " + api_->get_last_error().message);
        return;
    }
    
    status_ = ExchangeStatus::CONNECTED;
    update_heartbeat();
    
    SLOG_INFO("Successfully connected to Binance", {
        {"can_trade", account.can_trade ? "true" : "false"},
        {"balances_count", std::to_string(account.balances.size())}
    });
    
    // Record connection in performance monitor
    PerformanceMonitor::instance().update_heartbeat();
}

void BinanceExchange::disconnect() {
    SLOG_INFO("Disconnecting from Binance exchange");
    status_ = ExchangeStatus::DISCONNECTED;
    // API cleanup is handled by unique_ptr destructor
}

Price BinanceExchange::get_price(const std::string& symbol) {
    if (!is_connected()) {
        SLOG_WARNING("Attempted to get price while disconnected", {{"symbol", symbol}});
        return Price{symbol, 0.0, 0.0, 0.0, 0.0, 0};
    }
    
    if (!validate_symbol(symbol)) {
        SLOG_ERROR("Invalid symbol format", {{"symbol", symbol}});
        return Price{symbol, 0.0, 0.0, 0.0, 0.0, 0};
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Get ticker price from Binance
    auto ticker = api_->get_ticker_price(symbol);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    // Record latency
    PerformanceMonitor::instance().record_network_latency("binance_price", latency);
    
    if (api_->get_last_error().is_error()) {
        SLOG_ERROR("Failed to get price from Binance", {
            {"symbol", symbol},
            {"error", api_->get_last_error().message}
        });
        
        PerformanceMonitor::instance().record_api_error("binance", api_->get_last_error().message);
        return Price{symbol, 0.0, 0.0, 0.0, 0.0, 0};
    }
    
    update_heartbeat();
    return convert_ticker_to_price(ticker);
}

OrderResult BinanceExchange::place_order(const Order& order) {
    SLOG_INFO("Placing order on Binance", {
        {"symbol", order.symbol},
        {"side", order.side == OrderSide::BUY ? "BUY" : "SELL"},
        {"type", order.type == OrderType::MARKET ? "MARKET" : "LIMIT"},
        {"quantity", std::to_string(order.quantity)},
        {"price", std::to_string(order.price)}
    });
    
    if (!is_connected()) {
        SLOG_ERROR("Attempted to place order while disconnected");
        return OrderResult{
            "", order.client_order_id, order.symbol, order.side,
            0.0, 0.0, OrderStatus::REJECTED, 0.0, "", 0, "binance"
        };
    }
    
    PerformanceMonitor::instance().record_order_placed("binance");
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    BinanceOrderResponse response;
    
    try {
        if (order.type == OrderType::MARKET) {
            response = api_->place_market_order(
                order.symbol,
                convert_order_side(order.side),
                order.quantity
            );
        } else {
            response = api_->place_limit_order(
                order.symbol,
                convert_order_side(order.side),
                order.quantity,
                order.price
            );
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        if (api_->get_last_error().is_error()) {
            SLOG_ERROR("Order placement failed", {
                {"symbol", order.symbol},
                {"error", api_->get_last_error().message}
            });
            
            PerformanceMonitor::instance().record_api_error("binance", api_->get_last_error().message);
            return OrderResult{
                "", order.client_order_id, order.symbol, order.side,
                0.0, 0.0, OrderStatus::REJECTED, 0.0, "", 0, "binance"
            };
        }
        
        PerformanceMonitor::instance().record_order_filled("binance", latency);
        update_heartbeat();
        
        return convert_binance_order(response);
        
    } catch (const std::exception& e) {
        SLOG_ERROR("Exception during order placement", {
            {"symbol", order.symbol},
            {"error", e.what()}
        });
        
        return OrderResult{
            "", order.client_order_id, order.symbol, order.side,
            0.0, 0.0, OrderStatus::REJECTED, 0.0, "", 0, "binance"
        };
    }
}

std::vector<Balance> BinanceExchange::get_balances() {
    if (!is_connected()) {
        return {};
    }
    
    auto account = api_->get_account_info();
    if (api_->get_last_error().is_error()) {
        SLOG_ERROR("Failed to get balances", {{"error", api_->get_last_error().message}});
        return {};
    }
    
    update_heartbeat();
    return account.balances;
}

bool BinanceExchange::test_connectivity() {
    if (!api_) {
        return false;
    }
    
    return api_->test_connectivity();
}

bool BinanceExchange::initialize_api() {
    try {
        // Get API credentials from secure config
        SecureConfig secure_config;
        auto api_key = secure_config.get_exchange_api_key("binance");
        auto secret_key = secure_config.get_exchange_secret("binance");
        
        if (!api_key || !secret_key) {
            SLOG_ERROR("Missing Binance API credentials");
            return false;
        }
        
        if (!secure_config.validate_exchange_credentials("binance")) {
            SLOG_ERROR("Invalid Binance API credentials");
            return false;
        }
        
        api_ = std::make_unique<BinanceAPI>(*api_key, *secret_key, config_.base_url);
        
        SLOG_DEBUG("Binance API initialized", {
            {"base_url", config_.base_url},
            {"testnet", config_.testnet ? "true" : "false"}
        });
        
        return true;
    } catch (const std::exception& e) {
        SLOG_ERROR("Failed to initialize Binance API", {{"error", e.what()}});
        return false;
    }
}

bool BinanceExchange::validate_symbol(const std::string& symbol) {
    // Basic symbol validation
    if (symbol.empty() || symbol.length() < 3) {
        return false;
    }
    
    // Check for valid characters (letters and numbers only)
    for (char c : symbol) {
        if (!std::isalnum(c)) {
            return false;
        }
    }
    
    return true;
}

std::string BinanceExchange::convert_order_side(OrderSide side) {
    switch (side) {
        case OrderSide::BUY: return "BUY";
        case OrderSide::SELL: return "SELL";
        default: return "BUY";
    }
}

std::string BinanceExchange::convert_order_type(OrderType type) {
    switch (type) {
        case OrderType::MARKET: return "MARKET";
        case OrderType::LIMIT: return "LIMIT";
        default: return "MARKET";
    }
}

OrderStatus BinanceExchange::convert_binance_status(const std::string& status) {
    if (status == "NEW") return OrderStatus::NEW;
    if (status == "PARTIALLY_FILLED") return OrderStatus::PARTIAL;
    if (status == "FILLED") return OrderStatus::FILLED;
    if (status == "CANCELED") return OrderStatus::CANCELLED;
    if (status == "REJECTED") return OrderStatus::REJECTED;
    if (status == "EXPIRED") return OrderStatus::CANCELLED;
    return OrderStatus::PENDING;
}

Price BinanceExchange::convert_ticker_to_price(const BinanceTickerPrice& ticker) {
    // For ticker price, we only have the last price
    // In a real implementation, we'd need to get order book for bid/ask
    return Price{
        ticker.symbol,
        ticker.price * 0.999,  // Approximate bid (1 tick below)
        ticker.price * 1.001,  // Approximate ask (1 tick above)
        ticker.price,          // Last price
        0.0,                   // Volume (not available in ticker)
        ticker.timestamp
    };
}

OrderResult BinanceExchange::convert_binance_order(const BinanceOrderResponse& response) {
    return OrderResult{
        response.order_id,
        response.client_order_id,
        response.symbol,
        response.side == "BUY" ? OrderSide::BUY : OrderSide::SELL,
        response.executed_qty,
        response.cummulative_quote_qty,
        convert_binance_status(response.status),
        0.0,  // Commission (would need separate call to get this)
        "",   // Commission asset
        std::stoll(response.transact_time),
        "binance"
    };
}

void BinanceExchange::handle_connection_error(const std::string& error) {
    SLOG_ERROR("Binance connection error", {{"error", error}});
    status_ = ExchangeStatus::ERROR;
    PerformanceMonitor::instance().record_api_error("binance", error);
}

void BinanceExchange::update_heartbeat() {
    last_heartbeat_ = std::chrono::steady_clock::now();
    PerformanceMonitor::instance().update_heartbeat();
}

bool BinanceExchange::is_connected() const {
    return status_ == ExchangeStatus::CONNECTED;
}

} // namespace ats 