#pragma once

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QDateTime>
#include <QVariantMap>
#include <QAbstractListModel>
#include <memory>
#include <vector>

// Forward declarations for ATS types
namespace ats {
    namespace backtest {
        struct PerformanceMetrics;
        struct TradeResult;
        struct PortfolioSnapshot;
    }
}

namespace ats {
namespace ui {

/**
 * @brief Portfolio data structure for UI consumption
 */
struct PortfolioData {
    QString symbol;
    QString exchange;
    double quantity = 0.0;
    double currentPrice = 0.0;
    double marketValue = 0.0;
    double unrealizedPnL = 0.0;
    double unrealizedPnLPercentage = 0.0;
    QDateTime lastUpdate;
    
    Q_GADGET
    Q_PROPERTY(QString symbol MEMBER symbol)
    Q_PROPERTY(QString exchange MEMBER exchange)
    Q_PROPERTY(double quantity MEMBER quantity)
    Q_PROPERTY(double currentPrice MEMBER currentPrice)
    Q_PROPERTY(double marketValue MEMBER marketValue)
    Q_PROPERTY(double unrealizedPnL MEMBER unrealizedPnL)
    Q_PROPERTY(double unrealizedPnLPercentage MEMBER unrealizedPnLPercentage)
    Q_PROPERTY(QDateTime lastUpdate MEMBER lastUpdate)
};

/**
 * @brief Trading data structure for UI consumption
 */
struct TradeData {
    QString tradeId;
    QDateTime timestamp;
    QString symbol;
    QString exchange;
    QString side; // "buy" or "sell"
    double quantity = 0.0;
    double price = 0.0;
    double fee = 0.0;
    double pnl = 0.0;
    QString strategy;
    QString status; // "pending", "filled", "cancelled"
    
    Q_GADGET
    Q_PROPERTY(QString tradeId MEMBER tradeId)
    Q_PROPERTY(QDateTime timestamp MEMBER timestamp)
    Q_PROPERTY(QString symbol MEMBER symbol)
    Q_PROPERTY(QString exchange MEMBER exchange)
    Q_PROPERTY(QString side MEMBER side)
    Q_PROPERTY(double quantity MEMBER quantity)
    Q_PROPERTY(double price MEMBER price)
    Q_PROPERTY(double fee MEMBER fee)
    Q_PROPERTY(double pnl MEMBER pnl)
    Q_PROPERTY(QString strategy MEMBER strategy)
    Q_PROPERTY(QString status MEMBER status)
};

/**
 * @brief Alert/notification data structure
 */
struct AlertData {
    QString alertId;
    QDateTime timestamp;
    QString type; // "info", "warning", "error", "success"
    QString title;
    QString message;
    QString strategy;
    bool isRead = false;
    
    Q_GADGET
    Q_PROPERTY(QString alertId MEMBER alertId)
    Q_PROPERTY(QDateTime timestamp MEMBER timestamp)
    Q_PROPERTY(QString type MEMBER type)
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(QString message MEMBER message)
    Q_PROPERTY(QString strategy MEMBER strategy)
    Q_PROPERTY(bool isRead MEMBER isRead)
};

/**
 * @brief Market data structure
 */
struct MarketData {
    QString symbol;
    QString exchange;
    double price = 0.0;
    double volume = 0.0;
    double change24h = 0.0;
    double changePercentage24h = 0.0;
    QDateTime timestamp;
    
    Q_GADGET
    Q_PROPERTY(QString symbol MEMBER symbol)
    Q_PROPERTY(QString exchange MEMBER exchange)
    Q_PROPERTY(double price MEMBER price)
    Q_PROPERTY(double volume MEMBER volume)
    Q_PROPERTY(double change24h MEMBER change24h)
    Q_PROPERTY(double changePercentage24h MEMBER changePercentage24h)
    Q_PROPERTY(QDateTime timestamp MEMBER timestamp)
};

/**
 * @brief Central data service for the dashboard
 * 
 * Manages all data flowing through the application, including:
 * - Portfolio data
 * - Trade history
 * - Market data
 * - Performance metrics
 * - Alerts and notifications
 */
class DataService : public QObject
{
    Q_OBJECT
    
    // Overall portfolio metrics
    Q_PROPERTY(double totalValue READ totalValue NOTIFY totalValueChanged)
    Q_PROPERTY(double totalPnL READ totalPnL NOTIFY totalPnLChanged)
    Q_PROPERTY(double totalPnLPercentage READ totalPnLPercentage NOTIFY totalPnLPercentageChanged)
    Q_PROPERTY(double dayPnL READ dayPnL NOTIFY dayPnLChanged)
    Q_PROPERTY(double dayPnLPercentage READ dayPnLPercentage NOTIFY dayPnLPercentageChanged)
    
    // Connection status
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)
    
    // Data freshness
    Q_PROPERTY(QDateTime lastUpdate READ lastUpdate NOTIFY lastUpdateChanged)
    Q_PROPERTY(int updateInterval READ updateInterval WRITE setUpdateInterval NOTIFY updateIntervalChanged)

public:
    explicit DataService(QObject *parent = nullptr);
    ~DataService() override;
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Data access methods
    double totalValue() const;
    double totalPnL() const;
    double totalPnLPercentage() const;
    double dayPnL() const;
    double dayPnLPercentage() const;
    
    bool isConnected() const;
    QString connectionStatus() const;
    QDateTime lastUpdate() const;
    
    int updateInterval() const;
    void setUpdateInterval(int intervalMs);
    
    // Portfolio data
    Q_INVOKABLE QVariantList getPortfolioPositions() const;
    Q_INVOKABLE QVariantMap getPortfolioSummary() const;
    Q_INVOKABLE QVariantList getPortfolioHistory(const QDateTime& fromDate, 
                                                 const QDateTime& toDate) const;
    
    // Trade data
    Q_INVOKABLE QVariantList getRecentTrades(int limit = 100) const;
    Q_INVOKABLE QVariantList getTradeHistory(const QDateTime& fromDate, 
                                            const QDateTime& toDate,
                                            const QString& symbol = QString(),
                                            const QString& strategy = QString()) const;
    
    // Market data
    Q_INVOKABLE QVariantList getMarketData() const;
    Q_INVOKABLE QVariantMap getMarketDataForSymbol(const QString& symbol, 
                                                   const QString& exchange) const;
    
    // Alert data
    Q_INVOKABLE QVariantList getAlerts(bool unreadOnly = false) const;
    Q_INVOKABLE int getUnreadAlertsCount() const;
    Q_INVOKABLE bool markAlertAsRead(const QString& alertId);
    Q_INVOKABLE bool markAllAlertsAsRead();
    
    // Performance metrics
    Q_INVOKABLE QVariantMap getPerformanceMetrics() const;
    Q_INVOKABLE QVariantList getPerformanceHistory(const QDateTime& fromDate, 
                                                   const QDateTime& toDate) const;
    
    // Chart data
    Q_INVOKABLE QVariantList getEquityCurveData(const QDateTime& fromDate, 
                                               const QDateTime& toDate) const;
    Q_INVOKABLE QVariantList getPnLChartData(const QString& timeframe = "1D") const;
    Q_INVOKABLE QVariantList getVolumeChartData(const QString& timeframe = "1D") const;

public slots:
    // Data update methods
    void updateData();
    void updatePortfolioData();
    void updateTradeData();
    void updateMarketData();
    void updatePerformanceData();
    
    // Data management
    void clearCache();
    void refreshAllData();
    
    // Connection management
    void setConnectionStatus(bool connected, const QString& status = QString());

signals:
    // Data change signals
    void dataUpdated();
    void portfolioDataUpdated();
    void tradeDataUpdated();
    void marketDataUpdated();
    void performanceDataUpdated();
    void alertsUpdated();
    
    // Property change signals
    void totalValueChanged();
    void totalPnLChanged();
    void totalPnLPercentageChanged();
    void dayPnLChanged();
    void dayPnLPercentageChanged();
    void connectionStatusChanged();
    void lastUpdateChanged();
    void updateIntervalChanged();
    
    // New data signals
    void newTradeReceived(const QVariantMap& tradeData);
    void newAlertReceived(const QVariantMap& alertData);
    void portfolioValueChanged(double newValue, double change);

private slots:
    void onUpdateTimer();
    void onDataReceived();

private:
    // Data storage
    std::vector<PortfolioData> m_portfolioData;
    std::vector<TradeData> m_tradeHistory;
    std::vector<AlertData> m_alerts;
    std::vector<MarketData> m_marketData;
    QVariantMap m_performanceMetrics;
    
    // Cache for chart data
    mutable QMutex m_dataMutex;
    QVariantList m_equityCurveCache;
    QVariantList m_pnlChartCache;
    QDateTime m_lastCacheUpdate;
    
    // State
    double m_totalValue = 0.0;
    double m_totalPnL = 0.0;
    double m_totalPnLPercentage = 0.0;
    double m_dayPnL = 0.0;
    double m_dayPnLPercentage = 0.0;
    
    bool m_isConnected = false;
    QString m_connectionStatus = "Disconnected";
    QDateTime m_lastUpdate;
    
    // Configuration
    int m_updateInterval = 1000; // 1 second
    int m_maxTradeHistory = 10000;
    int m_maxAlerts = 1000;
    
    // Timer for periodic updates
    std::unique_ptr<QTimer> m_updateTimer;
    
    // Helper methods
    void calculatePortfolioMetrics();
    void processNewTradeData(const TradeData& trade);
    void processNewAlertData(const AlertData& alert);
    void updateEquityCurveCache();
    void cleanupOldData();
    
    // Data conversion helpers
    QVariantMap portfolioDataToVariant(const PortfolioData& data) const;
    QVariantMap tradeDataToVariant(const TradeData& data) const;
    QVariantMap alertDataToVariant(const AlertData& data) const;
    QVariantMap marketDataToVariant(const MarketData& data) const;
    
    // Mock data generation (for development/testing)
    void generateMockData();
    PortfolioData createMockPortfolioData(const QString& symbol, const QString& exchange) const;
    TradeData createMockTradeData() const;
    AlertData createMockAlertData() const;
    MarketData createMockMarketData(const QString& symbol, const QString& exchange) const;
};

} // namespace ui
} // namespace ats

// Register types for QML
Q_DECLARE_METATYPE(ats::ui::PortfolioData)
Q_DECLARE_METATYPE(ats::ui::TradeData)
Q_DECLARE_METATYPE(ats::ui::AlertData)
Q_DECLARE_METATYPE(ats::ui::MarketData)