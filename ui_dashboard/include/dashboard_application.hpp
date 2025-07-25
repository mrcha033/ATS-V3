#pragma once

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTranslator>
#include <QLocale>
#include <QTimer>
#include <memory>

// Forward declarations
class DashboardController;
class TradingController;
class SettingsController;
class DataService;
class GrpcClientService;
class NotificationService;
class LocalizationService;
class PdfReportService;
class ThemeManager;

namespace ats {
namespace ui {

/**
 * @brief Main application class for the ATS Dashboard
 * 
 * Manages the Qt application lifecycle, QML engine initialization,
 * service coordination, and overall application state.
 */
class DashboardApplication : public QGuiApplication
{
    Q_OBJECT

public:
    explicit DashboardApplication(int &argc, char **argv);
    ~DashboardApplication() override;

    // Application lifecycle
    bool initialize();
    void shutdown();
    
    // Service access
    DataService* getDataService() const;
    GrpcClientService* getGrpcService() const;
    NotificationService* getNotificationService() const;
    LocalizationService* getLocalizationService() const;
    
    // Configuration
    void loadSettings();
    void saveSettings();
    
    // Theme and localization
    void setTheme(const QString& themeName);
    void setLanguage(const QString& languageCode);
    
public slots:
    // Application events
    void onApplicationStateChanged(Qt::ApplicationState state);
    void onAboutToQuit();
    
    // Error handling
    void onCriticalError(const QString& error);
    void onNetworkError(const QString& error);

signals:
    // Application signals
    void applicationReady();
    void applicationShuttingDown();
    void themeChanged(const QString& theme);
    void languageChanged(const QString& language);
    
    // Data signals
    void dataServiceReady();
    void connectionStatusChanged(bool connected);

private slots:
    void onQmlEngineObjectCreated(QObject* obj, const QUrl& objUrl);
    void onPeriodicUpdate();
    void checkConnectionStatus();

private:
    // Initialization methods
    bool initializeQmlEngine();
    bool initializeServices();
    bool initializeControllers();
    void setupQmlTypes();
    void setupQmlContext();
    void loadTranslations();
    void applyDefaultTheme();
    
    // Configuration
    void loadApplicationSettings();
    void setupSystemTray();
    void setupAutoStart();
    
    // Error handling
    void handleQmlErrors(const QList<QQmlError>& errors);
    void showCriticalErrorDialog(const QString& title, const QString& message);
    
    // Core components
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<QTranslator> m_translator;
    std::unique_ptr<QTimer> m_updateTimer;
    
    // Services
    std::unique_ptr<DataService> m_dataService;
    std::unique_ptr<GrpcClientService> m_grpcService;
    std::unique_ptr<NotificationService> m_notificationService;
    std::unique_ptr<LocalizationService> m_localizationService;
    std::unique_ptr<PdfReportService> m_pdfReportService;
    std::unique_ptr<ThemeManager> m_themeManager;
    
    // Controllers
    std::unique_ptr<DashboardController> m_dashboardController;
    std::unique_ptr<TradingController> m_tradingController;
    std::unique_ptr<SettingsController> m_settingsController;
    
    // Application state
    QString m_currentTheme;
    QString m_currentLanguage;
    bool m_isInitialized;
    bool m_isShuttingDown;
    
    // Configuration
    struct AppConfig {
        QString theme = "material";
        QString language = "en";
        bool enableSystemTray = true;
        bool enableAutoStart = false;
        bool enableNotifications = true;
        int updateIntervalMs = 1000;
        QString grpcServerUrl = "localhost:50051";
        bool enableDebugMode = false;
    } m_config;
    
    // Constants
    static constexpr const char* APP_NAME = "ATS Dashboard";
    static constexpr const char* ORGANIZATION_NAME = "ATS Trading Systems";
    static constexpr const char* ORGANIZATION_DOMAIN = "ats.trading";
    static constexpr const char* SETTINGS_FILE = "dashboard_config.ini";
    static constexpr int DEFAULT_UPDATE_INTERVAL = 1000; // 1 second
};

} // namespace ui
} // namespace ats