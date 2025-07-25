#include "../include/dashboard_application.hpp"
#include "../include/controllers/dashboard_controller.hpp"
#include "../include/controllers/trading_controller.hpp"
#include "../include/controllers/settings_controller.hpp"
#include "../include/services/data_service.hpp"
#include "../include/services/grpc_client_service.hpp"
#include "../include/services/notification_service.hpp"
#include "../include/services/localization_service.hpp"
#include "../include/services/pdf_report_service.hpp"
#include "../include/utils/theme_manager.hpp"

#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QMessageBox>
#include <QQmlError>
#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(dashboardApp, "ats.dashboard.app")

namespace ats {
namespace ui {

DashboardApplication::DashboardApplication(int &argc, char **argv)
    : QGuiApplication(argc, argv)
    , m_isInitialized(false)
    , m_isShuttingDown(false)
{
    // Set application properties
    setApplicationName(APP_NAME);
    setOrganizationName(ORGANIZATION_NAME);
    setOrganizationDomain(ORGANIZATION_DOMAIN);
    setApplicationVersion(APP_VERSION);
    
    // Setup application icon
    setWindowIcon(QIcon(":/resources/images/logo.png"));
    
    // Connect application signals
    connect(this, &QGuiApplication::applicationStateChanged,
            this, &DashboardApplication::onApplicationStateChanged);
    connect(this, &QGuiApplication::aboutToQuit,
            this, &DashboardApplication::onAboutToQuit);
    
    qCDebug(dashboardApp) << "Dashboard application created";
}

DashboardApplication::~DashboardApplication()
{
    qCDebug(dashboardApp) << "Dashboard application destroyed";
}

bool DashboardApplication::initialize()
{
    if (m_isInitialized) {
        qCWarning(dashboardApp) << "Application already initialized";
        return true;
    }
    
    qCDebug(dashboardApp) << "Initializing dashboard application";
    
    try {
        // Load configuration
        loadApplicationSettings();
        
        // Initialize services
        if (!initializeServices()) {
            qCCritical(dashboardApp) << "Failed to initialize services";
            return false;
        }
        
        // Initialize controllers
        if (!initializeControllers()) {
            qCCritical(dashboardApp) << "Failed to initialize controllers";
            return false;
        }
        
        // Initialize QML engine
        if (!initializeQmlEngine()) {
            qCCritical(dashboardApp) << "Failed to initialize QML engine";
            return false;
        }
        
        // Setup periodic updates
        m_updateTimer = std::make_unique<QTimer>();
        m_updateTimer->setInterval(m_config.updateIntervalMs);
        connect(m_updateTimer.get(), &QTimer::timeout,
                this, &DashboardApplication::onPeriodicUpdate);
        m_updateTimer->start();
        
        // Setup system tray if enabled
        if (m_config.enableSystemTray) {
            setupSystemTray();
        }
        
        // Apply theme and language
        setTheme(m_config.theme);
        setLanguage(m_config.language);
        
        m_isInitialized = true;
        emit applicationReady();
        
        qCInfo(dashboardApp) << "Dashboard application initialized successfully";
        return true;
        
    } catch (const std::exception& e) {
        qCCritical(dashboardApp) << "Exception during initialization:" << e.what();
        return false;
    }
}

void DashboardApplication::shutdown()
{
    if (m_isShuttingDown) {
        return;
    }
    
    m_isShuttingDown = true;
    emit applicationShuttingDown();
    
    qCDebug(dashboardApp) << "Shutting down dashboard application";
    
    // Stop periodic updates
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
    
    // Save settings
    saveSettings();
    
    // Shutdown services in reverse order
    m_settingsController.reset();
    m_tradingController.reset();
    m_dashboardController.reset();
    
    m_themeManager.reset();
    m_pdfReportService.reset();
    m_localizationService.reset();
    m_notificationService.reset();
    m_grpcService.reset();
    m_dataService.reset();
    
    qCInfo(dashboardApp) << "Dashboard application shutdown complete";
}

bool DashboardApplication::initializeServices()
{
    qCDebug(dashboardApp) << "Initializing services";
    
    try {
        // Data service
        m_dataService = std::make_unique<DataService>();
        if (!m_dataService->initialize()) {
            qCCritical(dashboardApp) << "Failed to initialize data service";
            return false;
        }
        
        // gRPC client service
        m_grpcService = std::make_unique<GrpcClientService>();
        if (!m_grpcService->initialize(m_config.grpcServerUrl)) {
            qCWarning(dashboardApp) << "Failed to initialize gRPC service";
            // Continue without gRPC for now
        }
        
        // Notification service
        m_notificationService = std::make_unique<NotificationService>();
        m_notificationService->setEnabled(m_config.enableNotifications);
        
        // Localization service
        m_localizationService = std::make_unique<LocalizationService>();
        
        // PDF report service
        m_pdfReportService = std::make_unique<PdfReportService>();
        
        // Theme manager
        m_themeManager = std::make_unique<ThemeManager>();
        
        // Connect service signals
        connect(m_dataService.get(), &DataService::dataUpdated,
                this, [this]() { emit dataServiceReady(); });
        
        connect(m_grpcService.get(), &GrpcClientService::connectionStatusChanged,
                this, &DashboardApplication::connectionStatusChanged);
        
        qCDebug(dashboardApp) << "Services initialized successfully";
        return true;
        
    } catch (const std::exception& e) {
        qCCritical(dashboardApp) << "Exception initializing services:" << e.what();
        return false;
    }
}

bool DashboardApplication::initializeControllers()
{
    qCDebug(dashboardApp) << "Initializing controllers";
    
    try {
        // Dashboard controller
        m_dashboardController = std::make_unique<DashboardController>(
            m_dataService.get(), m_notificationService.get());
        
        // Trading controller
        m_tradingController = std::make_unique<TradingController>(
            m_grpcService.get(), m_dataService.get());
        
        // Settings controller
        m_settingsController = std::make_unique<SettingsController>(
            m_localizationService.get(), m_themeManager.get());
        
        qCDebug(dashboardApp) << "Controllers initialized successfully";
        return true;
        
    } catch (const std::exception& e) {
        qCCritical(dashboardApp) << "Exception initializing controllers:" << e.what();
        return false;
    }
}

bool DashboardApplication::initializeQmlEngine()
{
    qCDebug(dashboardApp) << "Initializing QML engine";
    
    try {
        // Set Material Design style
        QQuickStyle::setStyle("Material");
        
        // Create QML engine
        m_engine = std::make_unique<QQmlApplicationEngine>();
        
        // Setup QML types and context
        setupQmlTypes();
        setupQmlContext();
        
        // Load translations
        loadTranslations();
        
        // Connect QML engine signals
        connect(m_engine.get(), &QQmlApplicationEngine::objectCreated,
                this, &DashboardApplication::onQmlEngineObjectCreated);
        
        connect(m_engine.get(), &QQmlApplicationEngine::warnings,
                this, &DashboardApplication::handleQmlErrors);
        
        // Load main QML file
        const QUrl mainQmlUrl(QStringLiteral("qrc:/qml/main.qml"));
        m_engine->load(mainQmlUrl);
        
        if (m_engine->rootObjects().isEmpty()) {
            qCCritical(dashboardApp) << "Failed to load main QML file";
            return false;
        }
        
        qCDebug(dashboardApp) << "QML engine initialized successfully";
        return true;
        
    } catch (const std::exception& e) {
        qCCritical(dashboardApp) << "Exception initializing QML engine:" << e.what();
        return false;
    }
}

void DashboardApplication::setupQmlTypes()
{
    // Register C++ types for QML
    qmlRegisterSingletonInstance("ATS.Dashboard", 1, 0, "DashboardController", 
                                m_dashboardController.get());
    qmlRegisterSingletonInstance("ATS.Dashboard", 1, 0, "TradingController", 
                                m_tradingController.get());
    qmlRegisterSingletonInstance("ATS.Dashboard", 1, 0, "SettingsController", 
                                m_settingsController.get());
    
    // Register utility types
    qmlRegisterSingletonInstance("ATS.Dashboard", 1, 0, "ThemeManager", 
                                m_themeManager.get());
    qmlRegisterSingletonInstance("ATS.Dashboard", 1, 0, "LocalizationService", 
                                m_localizationService.get());
}

void DashboardApplication::setupQmlContext()
{
    QQmlContext* rootContext = m_engine->rootContext();
    
    // Set context properties
    rootContext->setContextProperty("app", this);
    rootContext->setContextProperty("dataService", m_dataService.get());
    rootContext->setContextProperty("notificationService", m_notificationService.get());
    
    // Application info
    rootContext->setContextProperty("appVersion", applicationVersion());
    rootContext->setContextProperty("appName", applicationName());
    rootContext->setContextProperty("organizationName", organizationName());
}

void DashboardApplication::loadTranslations()
{
    m_translator = std::make_unique<QTranslator>();
    
    QString languageFile = QString(":/translations/app_%1.qm").arg(m_config.language);
    
    if (m_translator->load(languageFile)) {
        installTranslator(m_translator.get());
        qCDebug(dashboardApp) << "Loaded translations for language:" << m_config.language;
    } else {
        qCWarning(dashboardApp) << "Failed to load translations for language:" << m_config.language;
    }
}

void DashboardApplication::loadApplicationSettings()
{
    QSettings settings(SETTINGS_FILE, QSettings::IniFormat);
    
    // Load configuration
    m_config.theme = settings.value("ui/theme", m_config.theme).toString();
    m_config.language = settings.value("ui/language", m_config.language).toString();
    m_config.enableSystemTray = settings.value("ui/systemTray", m_config.enableSystemTray).toBool();
    m_config.enableAutoStart = settings.value("app/autoStart", m_config.enableAutoStart).toBool();
    m_config.enableNotifications = settings.value("app/notifications", m_config.enableNotifications).toBool();
    m_config.updateIntervalMs = settings.value("app/updateInterval", m_config.updateIntervalMs).toInt();
    m_config.grpcServerUrl = settings.value("network/grpcUrl", m_config.grpcServerUrl).toString();
    m_config.enableDebugMode = settings.value("debug/enabled", m_config.enableDebugMode).toBool();
    
    qCDebug(dashboardApp) << "Loaded application settings";
}

void DashboardApplication::saveSettings()
{
    QSettings settings(SETTINGS_FILE, QSettings::IniFormat);
    
    // Save current configuration
    settings.setValue("ui/theme", m_config.theme);
    settings.setValue("ui/language", m_config.language);
    settings.setValue("ui/systemTray", m_config.enableSystemTray);
    settings.setValue("app/autoStart", m_config.enableAutoStart);
    settings.setValue("app/notifications", m_config.enableNotifications);
    settings.setValue("app/updateInterval", m_config.updateIntervalMs);
    settings.setValue("network/grpcUrl", m_config.grpcServerUrl);
    settings.setValue("debug/enabled", m_config.enableDebugMode);
    
    settings.sync();
    qCDebug(dashboardApp) << "Saved application settings";
}

void DashboardApplication::setTheme(const QString& themeName)
{
    if (m_currentTheme == themeName) {
        return;
    }
    
    m_currentTheme = themeName;
    m_config.theme = themeName;
    
    if (m_themeManager) {
        m_themeManager->setTheme(themeName);
    }
    
    emit themeChanged(themeName);
    qCDebug(dashboardApp) << "Theme changed to:" << themeName;
}

void DashboardApplication::setLanguage(const QString& languageCode)
{
    if (m_currentLanguage == languageCode) {
        return;
    }
    
    m_currentLanguage = languageCode;
    m_config.language = languageCode;
    
    // Reload translations
    if (m_translator) {
        removeTranslator(m_translator.get());
        QString languageFile = QString(":/translations/app_%1.qm").arg(languageCode);
        if (m_translator->load(languageFile)) {
            installTranslator(m_translator.get());
        }
    }
    
    emit languageChanged(languageCode);
    qCDebug(dashboardApp) << "Language changed to:" << languageCode;
}

void DashboardApplication::onQmlEngineObjectCreated(QObject* obj, const QUrl& objUrl)
{
    if (!obj && objUrl == QUrl(QStringLiteral("qrc:/qml/main.qml"))) {
        qCCritical(dashboardApp) << "Failed to create main QML object";
        quit();
    }
}

void DashboardApplication::onPeriodicUpdate()
{
    if (m_isShuttingDown) {
        return;
    }
    
    // Periodic tasks
    checkConnectionStatus();
    
    // Update data service
    if (m_dataService) {
        m_dataService->updateData();
    }
}

void DashboardApplication::checkConnectionStatus()
{
    if (m_grpcService) {
        bool isConnected = m_grpcService->isConnected();
        emit connectionStatusChanged(isConnected);
    }
}

void DashboardApplication::setupSystemTray()
{
    // System tray implementation would go here
    // For now, just log the intent
    qCDebug(dashboardApp) << "System tray setup requested";
}

void DashboardApplication::handleQmlErrors(const QList<QQmlError>& errors)
{
    for (const auto& error : errors) {
        qCWarning(dashboardApp) << "QML Error:" << error.toString();
    }
}

void DashboardApplication::onApplicationStateChanged(Qt::ApplicationState state)
{
    qCDebug(dashboardApp) << "Application state changed to:" << state;
    
    switch (state) {
        case Qt::ApplicationSuspended:
            // Pause updates when suspended
            if (m_updateTimer) {
                m_updateTimer->stop();
            }
            break;
        case Qt::ApplicationActive:
            // Resume updates when active
            if (m_updateTimer) {
                m_updateTimer->start();
            }
            break;
        default:
            break;
    }
}

void DashboardApplication::onAboutToQuit()
{
    qCDebug(dashboardApp) << "Application about to quit";
    shutdown();
}

void DashboardApplication::onCriticalError(const QString& error)
{
    qCCritical(dashboardApp) << "Critical error:" << error;
    showCriticalErrorDialog(tr("Critical Error"), error);
}

void DashboardApplication::onNetworkError(const QString& error)
{
    qCWarning(dashboardApp) << "Network error:" << error;
    if (m_notificationService) {
        m_notificationService->showNotification(tr("Network Error"), error);
    }
}

void DashboardApplication::showCriticalErrorDialog(const QString& title, const QString& message)
{
    // In a GUI application, we'd show a message box
    // For now, just log and exit
    qCCritical(dashboardApp) << title << ":" << message;
    quit();
}

// Service getters
DataService* DashboardApplication::getDataService() const
{
    return m_dataService.get();
}

GrpcClientService* DashboardApplication::getGrpcService() const
{
    return m_grpcService.get();
}

NotificationService* DashboardApplication::getNotificationService() const
{
    return m_notificationService.get();
}

LocalizationService* DashboardApplication::getLocalizationService() const
{
    return m_localizationService.get();
}

void DashboardApplication::loadSettings()
{
    loadApplicationSettings();
}

} // namespace ui
} // namespace ats