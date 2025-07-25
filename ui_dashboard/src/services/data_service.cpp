#include "../../include/services/data_service.hpp"
#include <QTimer>
#include <QDateTime>
#include <QDebug>

namespace ats {
namespace ui {

DataService::DataService(QObject *parent)
    : QObject(parent)
{
    m_updateTimer = std::make_unique<QTimer>();
    connect(m_updateTimer.get(), &QTimer::timeout, this, &DataService::onUpdateTimer);
}

DataService::~DataService() = default;

bool DataService::initialize()
{
    // Generate initial mock data
    generateMockData();
    
    m_updateTimer->start(m_updateInterval);
    m_isConnected = true;
    m_connectionStatus = "Connected";
    
    emit connectionStatusChanged();
    emit dataUpdated();
    
    return true;
}

void DataService::shutdown()
{
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
    m_isConnected = false;
    emit connectionStatusChanged();
}

double DataService::totalValue() const { return m_totalValue; }
double DataService::totalPnL() const { return m_totalPnL; }
double DataService::totalPnLPercentage() const { return m_totalPnLPercentage; }
double DataService::dayPnL() const { return m_dayPnL; }
double DataService::dayPnLPercentage() const { return m_dayPnLPercentage; }
bool DataService::isConnected() const { return m_isConnected; }
QString DataService::connectionStatus() const { return m_connectionStatus; }
QDateTime DataService::lastUpdate() const { return m_lastUpdate; }
int DataService::updateInterval() const { return m_updateInterval; }

void DataService::setUpdateInterval(int intervalMs)
{
    if (m_updateInterval != intervalMs) {
        m_updateInterval = intervalMs;
        if (m_updateTimer) {
            m_updateTimer->setInterval(intervalMs);
        }
        emit updateIntervalChanged();
    }
}

QVariantList DataService::getPortfolioPositions() const
{
    QVariantList result;
    for (const auto& position : m_portfolioData) {
        result.append(portfolioDataToVariant(position));
    }
    return result;
}

QVariantList DataService::getRecentTrades(int limit) const
{
    QVariantList result;
    int count = 0;
    for (auto it = m_tradeHistory.rbegin(); 
         it != m_tradeHistory.rend() && count < limit; ++it, ++count) {
        result.append(tradeDataToVariant(*it));
    }
    return result;
}

QVariantList DataService::getAlerts(bool unreadOnly) const
{
    QVariantList result;
    for (const auto& alert : m_alerts) {
        if (!unreadOnly || !alert.isRead) {
            result.append(alertDataToVariant(alert));
        }
    }
    return result;
}

int DataService::getUnreadAlertsCount() const
{
    int count = 0;
    for (const auto& alert : m_alerts) {
        if (!alert.isRead) count++;
    }
    return count;
}

bool DataService::markAlertAsRead(const QString& alertId)
{
    for (auto& alert : m_alerts) {
        if (alert.alertId == alertId) {
            alert.isRead = true;
            emit alertsUpdated();
            return true;
        }
    }
    return false;
}

bool DataService::markAllAlertsAsRead()
{
    bool changed = false;
    for (auto& alert : m_alerts) {
        if (!alert.isRead) {
            alert.isRead = true;
            changed = true;
        }
    }
    if (changed) {
        emit alertsUpdated();
    }
    return changed;
}

void DataService::updateData()
{
    calculatePortfolioMetrics();
    m_lastUpdate = QDateTime::currentDateTime();
    emit dataUpdated();
    emit lastUpdateChanged();
}

void DataService::refreshAllData()
{
    generateMockData();
    updateData();
}

void DataService::onUpdateTimer()
{
    updateData();
}

void DataService::calculatePortfolioMetrics()
{
    m_totalValue = 0.0;
    for (const auto& position : m_portfolioData) {
        m_totalValue += position.marketValue;
    }
    
    // Simulate some P&L changes
    static double lastValue = m_totalValue;
    m_totalPnL = m_totalValue - lastValue + (rand() % 200 - 100);
    m_totalPnLPercentage = lastValue > 0 ? (m_totalPnL / lastValue) * 100.0 : 0.0;
    
    m_dayPnL = m_totalPnL * 0.7; // Approximate day P&L
    m_dayPnLPercentage = m_totalPnLPercentage * 0.7;
    
    emit totalValueChanged();
    emit totalPnLChanged();
    emit totalPnLPercentageChanged();
    emit dayPnLChanged();
    emit dayPnLPercentageChanged();
}

void DataService::generateMockData()
{
    m_portfolioData.clear();
    m_tradeHistory.clear();
    m_alerts.clear();
    
    // Generate mock portfolio positions
    m_portfolioData.push_back(createMockPortfolioData("BTC", "Binance"));
    m_portfolioData.push_back(createMockPortfolioData("ETH", "Upbit"));
    m_portfolioData.push_back(createMockPortfolioData("ADA", "Coinbase"));
    
    // Generate mock trade history
    for (int i = 0; i < 20; ++i) {
        m_tradeHistory.push_back(createMockTradeData());
    }
    
    // Generate mock alerts
    for (int i = 0; i < 5; ++i) {
        m_alerts.push_back(createMockAlertData());
    }
    
    calculatePortfolioMetrics();
}

PortfolioData DataService::createMockPortfolioData(const QString& symbol, const QString& exchange) const
{
    PortfolioData data;
    data.symbol = symbol;
    data.exchange = exchange;
    data.quantity = (rand() % 1000) / 100.0;
    data.currentPrice = (rand() % 50000) + 1000;
    data.marketValue = data.quantity * data.currentPrice;
    data.unrealizedPnL = (rand() % 2000) - 1000;
    data.unrealizedPnLPercentage = data.marketValue > 0 ? (data.unrealizedPnL / data.marketValue) * 100.0 : 0.0;
    data.lastUpdate = QDateTime::currentDateTime();
    return data;
}

TradeData DataService::createMockTradeData() const
{
    TradeData data;
    data.tradeId = QString("T%1").arg(rand() % 10000);
    data.timestamp = QDateTime::currentDateTime().addSecs(-(rand() % 3600));
    data.symbol = QStringList{"BTC", "ETH", "ADA"}[rand() % 3];
    data.exchange = QStringList{"Binance", "Upbit", "Coinbase"}[rand() % 3];
    data.side = (rand() % 2) ? "buy" : "sell";
    data.quantity = (rand() % 100) / 100.0;
    data.price = (rand() % 50000) + 1000;
    data.fee = data.quantity * data.price * 0.001;
    data.pnl = (rand() % 200) - 100;
    data.strategy = "Arbitrage";
    data.status = "filled";
    return data;
}

AlertData DataService::createMockAlertData() const
{
    AlertData data;
    data.alertId = QString("A%1").arg(rand() % 10000);
    data.timestamp = QDateTime::currentDateTime().addSecs(-(rand() % 1800));
    data.type = QStringList{"info", "warning", "error", "success"}[rand() % 4];
    data.title = "System Alert";
    data.message = "This is a mock alert message for testing purposes.";
    data.strategy = "Arbitrage";
    data.isRead = (rand() % 3) != 0; // 2/3 chance of being read
    return data;
}

QVariantMap DataService::portfolioDataToVariant(const PortfolioData& data) const
{
    QVariantMap map;
    map["symbol"] = data.symbol;
    map["exchange"] = data.exchange;
    map["quantity"] = data.quantity;
    map["currentPrice"] = data.currentPrice;
    map["marketValue"] = data.marketValue;
    map["unrealizedPnL"] = data.unrealizedPnL;
    map["unrealizedPnLPercentage"] = data.unrealizedPnLPercentage;
    map["lastUpdate"] = data.lastUpdate;
    return map;
}

QVariantMap DataService::tradeDataToVariant(const TradeData& data) const
{
    QVariantMap map;
    map["tradeId"] = data.tradeId;
    map["timestamp"] = data.timestamp;
    map["symbol"] = data.symbol;
    map["exchange"] = data.exchange;
    map["side"] = data.side;
    map["quantity"] = data.quantity;
    map["price"] = data.price;
    map["fee"] = data.fee;
    map["pnl"] = data.pnl;
    map["strategy"] = data.strategy;
    map["status"] = data.status;
    return map;
}

QVariantMap DataService::alertDataToVariant(const AlertData& data) const
{
    QVariantMap map;
    map["alertId"] = data.alertId;
    map["timestamp"] = data.timestamp;
    map["type"] = data.type;
    map["title"] = data.title;
    map["message"] = data.message;
    map["strategy"] = data.strategy;
    map["isRead"] = data.isRead;
    return map;
}

} // namespace ui
} // namespace ats