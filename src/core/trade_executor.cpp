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
        Order buy_order;
        buy_order.symbol = opportunity.symbol;
        buy_order.side = OrderSide::BUY;
        buy_order.type = OrderType::MARKET; // Assuming market order for simplicity
        buy_order.price = opportunity.buy_price;
        buy_order.quantity = trade_size;

        Order sell_order;
        sell_order.symbol = opportunity.symbol;
        sell_order.side = OrderSide::SELL;
        sell_order.type = OrderType::MARKET; // Assuming market order for simplicity
        sell_order.price = opportunity.sell_price;
        sell_order.quantity = trade_size;

        OrderResult buy_result = buy_exchange->place_order(buy_order);
        OrderResult sell_result = sell_exchange->place_order(sell_order);

        portfolio_manager_->update_position(buy_result);
        portfolio_manager_->update_position(sell_result);

        if (execution_callback_) {
            Trade trade;
            trade.symbol = opportunity.symbol;
            trade.buy_exchange = opportunity.buy_exchange;
            trade.sell_exchange = opportunity.sell_exchange;
            trade.buy_price = buy_result.executed_quantity > 0 ? buy_result.cummulative_quote_quantity / buy_result.executed_quantity : opportunity.buy_price;
            trade.sell_price = sell_result.executed_quantity > 0 ? sell_result.cummulative_quote_quantity / sell_result.executed_quantity : opportunity.sell_price;
            trade.volume = trade_size;
            trade.profit = opportunity.profit * trade_size; // This needs to be recalculated based on actual fill prices and fees
            trade.buy_order_id = buy_result.order_id;
            trade.sell_order_id = sell_result.order_id;
            trade.executed_buy_quantity = buy_result.executed_quantity;
            trade.executed_sell_quantity = sell_result.executed_quantity;
            execution_callback_(trade);
        }
    } catch (const ExchangeException& e) {
        Logger::error("Error executing trade: " + std::string(e.what()));
    }
}

}