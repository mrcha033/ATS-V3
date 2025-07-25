#include "../include/dashboard_application.hpp"
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QDir>
#include <iostream>

// Logging categories
Q_LOGGING_CATEGORY(main, "ats.dashboard.main")

// Application constants
constexpr const char* APP_DESCRIPTION = "ATS Trading System Dashboard";
constexpr const char* LOG_FILE_NAME = "ats-dashboard.log";

// Command line argument handling
struct CommandLineArgs {
    bool showVersion = false;
    bool showHelp = false;
    bool enableDebug = false;
    bool enableVerbose = false;
    QString configFile;
    QString logLevel = "info";
};

CommandLineArgs parseCommandLine(const QStringList& arguments);
void showVersion();
void showHelp(const QString& programName);
void setupLogging(const QString& logLevel, bool verbose);
void createLogDirectory();

int main(int argc, char *argv[])
{
    // Enable high DPI scaling
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    
    // Create application instance
    ats::ui::DashboardApplication app(argc, argv);
    
    // Parse command line arguments
    CommandLineArgs args = parseCommandLine(app.arguments());
    
    // Handle command line options
    if (args.showVersion) {
        showVersion();
        return 0;
    }
    
    if (args.showHelp) {
        showHelp(app.arguments().first());
        return 0;
    }
    
    // Setup logging
    setupLogging(args.logLevel, args.enableVerbose);
    
    qCInfo(main) << "Starting ATS Dashboard application";
    qCInfo(main) << "Version:" << app.applicationVersion();
    qCInfo(main) << "Qt Version:" << qVersion();
    qCInfo(main) << "Platform:" << QGuiApplication::platformName();
    
    // Initialize application
    if (!app.initialize()) {
        qCCritical(main) << "Failed to initialize application";
        return 1;
    }
    
    qCInfo(main) << "Application initialized successfully";
    
    // Run application event loop
    int result = app.exec();
    
    qCInfo(main) << "Application exiting with code:" << result;
    return result;
}

CommandLineArgs parseCommandLine(const QStringList& arguments)
{
    CommandLineArgs args;
    
    for (int i = 1; i < arguments.size(); ++i) {
        const QString& arg = arguments.at(i);
        
        if (arg == "--version" || arg == "-v") {
            args.showVersion = true;
        }
        else if (arg == "--help" || arg == "-h") {
            args.showHelp = true;
        }
        else if (arg == "--debug" || arg == "-d") {
            args.enableDebug = true;
            args.logLevel = "debug";
        }
        else if (arg == "--verbose") {
            args.enableVerbose = true;
        }
        else if (arg == "--config" || arg == "-c") {
            if (i + 1 < arguments.size()) {
                args.configFile = arguments.at(++i);
            }
        }
        else if (arg == "--log-level") {
            if (i + 1 < arguments.size()) {
                args.logLevel = arguments.at(++i);
            }
        }
        else if (arg.startsWith("--")) {
            std::cerr << "Unknown option: " << arg.toStdString() << std::endl;
        }
    }
    
    return args;
}

void showVersion()
{
    std::cout << "ATS Dashboard " << APP_VERSION << std::endl;
    std::cout << "Built with Qt " << QT_VERSION_STR << std::endl;
    std::cout << "Copyright (c) 2024 ATS Trading Systems" << std::endl;
}

void showHelp(const QString& programName)
{
    std::cout << "Usage: " << programName.toStdString() << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << APP_DESCRIPTION << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help           Show this help message" << std::endl;
    std::cout << "  -v, --version        Show version information" << std::endl;
    std::cout << "  -d, --debug          Enable debug logging" << std::endl;
    std::cout << "  --verbose            Enable verbose output" << std::endl;
    std::cout << "  -c, --config FILE    Use custom configuration file" << std::endl;
    std::cout << "  --log-level LEVEL    Set log level (debug, info, warning, error)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName.toStdString() << " --debug" << std::endl;
    std::cout << "  " << programName.toStdString() << " --config /path/to/config.ini" << std::endl;
    std::cout << "  " << programName.toStdString() << " --log-level warning" << std::endl;
}

void setupLogging(const QString& logLevel, bool verbose)
{
    // Create log directory
    createLogDirectory();
    
    // Configure Qt logging
    QString logLevelUpper = logLevel.toUpper();
    
    // Set logging rules
    QString loggingRules;
    if (logLevelUpper == "DEBUG") {
        loggingRules = "ats.*.debug=true";
    } else if (logLevelUpper == "INFO") {
        loggingRules = "ats.*.info=true\nats.*.debug=false";
    } else if (logLevelUpper == "WARNING") {
        loggingRules = "ats.*.warning=true\nats.*.info=false\nats.*.debug=false";
    } else if (logLevelUpper == "ERROR") {
        loggingRules = "ats.*.critical=true\nats.*.warning=false\nats.*.info=false\nats.*.debug=false";
    }
    
    if (!loggingRules.isEmpty()) {
        QLoggingCategory::setFilterRules(loggingRules);
    }
    
    // Setup log file if needed
    if (verbose) {
        QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString logFile = QDir(logDir).filePath(LOG_FILE_NAME);
        
        // Note: In a full implementation, you would set up file logging here
        // For now, we'll just log to console
        qCDebug(main) << "Verbose logging enabled, would log to:" << logFile;
    }
    
    qCDebug(main) << "Logging configured with level:" << logLevel;
}

void createLogDirectory()
{
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir;
    if (!dir.exists(logDir)) {
        if (!dir.mkpath(logDir)) {
            qCWarning(main) << "Failed to create log directory:" << logDir;
        }
    }
}