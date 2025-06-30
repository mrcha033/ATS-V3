#include "trade_executor.hpp"
#include <iostream>
#include "../utils/logger.hpp"
#include "../exchange/exchange_exception.hpp"

namespace ats {

TradeExecutor::TradeExecutor(ConfigManager* config_manager, PortfolioManager* portfolio_manager, RiskManager* risk_manager)
    : config_manager_(config_manager), portfolio_manager_(portfolio_manager), risk_manager_(risk_manager) {}

bool TradeExecutor::Initialize() {
    return true;
}

void TradeExecutor::Start() {
    // Start the trade executor
}

void TradeExecutor::Stop() {
    // Stop the trade executor
}

bool TradeExecutor::IsHealthy() const {
    return true;
}

void TradeExecutor::AddExchange(std::shared_ptr<ExchangeInterface> exchange) {
    exchanges_.push_back(exchange);
}

void TradeExecutor::SetExecutionCallback(ExecutionCallback callback) {
    execution_callback_ = callback;
}

std::shared_ptr<ExchangeInterface> TradeExecutor::get_exchange(const std::string& name) {
    for (const auto& exchange : exchanges_) {
        if (exchange->get_name() == name) {
            return exchange;
        }
    }
    return nullptr;
}

void TradeExecutor::execute_trade(const ArbitrageOpportunity& opportunity) {
    double trade_size = risk_manager_->CalculateMaxPositionSize(opportunity);
    if (trade_size <= 0) {
        return;
    }

    auto buy_exchange = get_exchange(opportunity.buy_exchange);
    auto sell_exchange = get_exchange(opportunity.sell_exchange);

    if (!buy_exchange || !sell_exchange) {
        return;
    }

    try {
        // For simplicity, we'll assume that the orders are filled instantly and at the expected prices.
        // In a real-world application, you would need to monitor the orders and handle partial fills.
        buy_exchange->place_order(opportunity.symbol, "buy", trade_size, opportunity.buy_price);
        sell_exchange->place_order(opportunity.symbol, "sell", trade_size, opportunity.sell_price);

        portfolio_manager_->update_position(opportunity.symbol, trade_size);
        portfolio_manager_->update_position(opportunity.symbol, -trade_size);

        if (execution_callback_) {
            Trade trade;
            trade.symbol = opportunity.symbol;
            trade.buy_exchange = opportunity.buy_exchange;
            trade.sell_exchange = opportunity.sell_exchange;
            trade.buy_price = opportunity.buy_price;
            trade.sell_price = opportunity.sell_price;
            trade.volume = trade_size;
            trade.profit = opportunity.profit * trade_size;
            execution_callback_(trade);
        }
    } catch (const ExchangeException& e) {
        Logger::error("Error executing trade: " + std::string(e.what()));
    }
}

}