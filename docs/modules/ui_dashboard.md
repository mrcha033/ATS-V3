# UI Dashboard Module

## Purpose

The `ui_dashboard` module provides a comprehensive and intuitive graphical user interface (GUI) for the ATS-V3. It allows users to monitor real-time trading activities, visualize portfolio performance, track risk metrics, manage settings, and interact with the core trading system in a user-friendly manner.

## Key Components

-   **Qt/QML Application Structure**:
    The dashboard is built as a cross-platform desktop application using the Qt framework, with its user interface defined declaratively using QML. This separation allows for rapid UI development and a rich, interactive user experience.
    -   `main.cpp`: The application's entry point, responsible for initializing the `DashboardApplication`.
    -   `main.qml`: The root QML file that defines the overall application window layout, including the header, navigation sidebar, and a `StackLayout` for managing different content pages.

-   **`DashboardApplication` (`include/dashboard_application.hpp`, `src/dashboard_application.cpp`)**:
    The main C++ application class, inheriting from `QGuiApplication`. It orchestrates the entire dashboard lifecycle:
    -   **Lifecycle Management**: Handles the application's initialization, startup, shutdown, and manages the main event loop.
    -   **Service Orchestration**: Initializes and manages the lifetime of various C++ backend services that provide data and functionality to the UI (e.g., `DataService`, `GrpcClientService`, `NotificationService`).
    -   **Controller Management**: Initializes and manages C++ controller classes (e.g., `DashboardController`, `TradingController`, `SettingsController`) that act as intermediaries, exposing high-level actions and data to the QML frontend.
    -   **QML Engine Integration**: Sets up the `QQmlApplicationEngine`, registers C++ types as QML singletons, and exposes C++ objects to the QML context, enabling seamless communication between the frontend and backend.
    -   **Configuration**: Loads and saves application settings (e.g., theme, language, data update interval, gRPC server URL) using Qt's `QSettings` framework.
    -   **Theming & Localization**: Manages the application's visual themes (e.g., dark/light mode) and handles language translations, providing a customizable user experience.
    -   **Error Handling**: Implements basic QML error handling and mechanisms for displaying critical error messages to the user.

-   **Services (`services/` directory)**:
    C++ classes that provide data and functionality to the QML frontend, often interacting with the core ATS-V3 backend.
    -   **`DataService` (`include/services/data_service.hpp`, `src/services/data_service.cpp`)**:
        The central data provider for the UI. It aggregates, processes, and manages all data displayed in the dashboard. It defines Q_GADGET structs for UI-friendly data models (`PortfolioData`, `TradeData`, `AlertData`, `MarketData`) and provides Q_INVOKABLE methods for QML to retrieve various data sets (portfolio positions, recent trades, alerts, performance metrics, chart data). It periodically updates its internal data (currently simulated with mock data for development) and emits signals to notify QML of changes.
    -   **`GrpcClientService` (`include/services/grpc_client_service.hpp`, `src/services/grpc_client_service.cpp`)**:
        Designed to be the primary communication bridge to the core ATS-V3 backend services. It will connect to the gRPC endpoints exposed by modules like `trading_engine` and `risk_manager` to fetch real-time data and send commands. (Currently, it serves as a placeholder, simulating a connected state).
    -   **`NotificationService` (`include/services/notification_service.hpp`)**:
        A placeholder for a UI-specific notification service, intended to display system alerts and messages directly within the dashboard's user interface.
    -   **`LocalizationService` (`include/services/localization_service.hpp`)**:
        A placeholder for managing application localization, allowing users to switch between different display languages.
    -   **`PdfReportService` (`include/services/pdf_report_service.hpp`)**:
        A placeholder for generating PDF reports, such as detailed performance summaries or trade logs.

-   **Controllers (`controllers/` directory)**:
    C++ classes that act as intermediaries between the QML frontend and the C++ services. They expose high-level actions and business logic to QML.
    -   **`DashboardController`**: Provides actions related to the main dashboard view, such as exporting reports, refreshing data, and starting/stopping trading strategies.
    -   **`TradingController`**: Provides actions specific to the trading interface, including emergency stop functionalities.
    -   **`SettingsController`**: Manages actions related to application settings and user preferences.

-   **QML UI Components (`qml/` directory)**:
    The declarative user interface elements that define the look and feel of the dashboard.
    -   `main.qml`: The root QML file, defining the main application window, including the header (with connection status and P&L summary), a navigation sidebar, and a `StackLayout` for switching between different content pages.
    -   `pages/`: Contains the main content views of the dashboard:
        -   `DashboardPage.qml`: Displays an overview of the portfolio, including summary cards (total value, P&L, active strategies, alerts), interactive charts (equity curve, P&L breakdown), recent trades, and current positions.
        -   `TradingPage.qml`: A placeholder for the detailed trading interface, where users can place orders, manage positions, etc.
        -   `BacktestPage.qml`: A placeholder for the backtesting interface, allowing users to configure and run backtests.
    -   `components/`: Reusable QML UI elements, such as `MaterialCard.qml` for consistent Material Design styling.
    -   `widgets/`: Specific UI widgets like `AlertPanel.qml` (for displaying alerts), `PortfolioChart.qml` (a reusable chart component using Qt Charts), and `TradeLogTable.qml` (for displaying trade history with sorting and filtering).
    -   `dialogs/`: QML files for various dialogs (e.g., `AboutDialog.qml`, `SettingsDialog.qml`).

-   **Utilities (`utils/` directory)**:
    Helper classes for UI-specific functionalities.
    -   **`ThemeManager`**: Manages the application's visual theme (e.g., dark mode, light mode).
    -   **`ChartDataProvider`**: A placeholder for providing data to chart components.

## Data Flow

1.  **Initialization**: The `DashboardApplication` initializes C++ services and controllers.
2.  **Data Provision**: The `DataService` (C++) periodically updates its internal data (either from mock sources or via `GrpcClientService` from the backend) and emits signals.
3.  **QML Binding**: QML components are bound to properties and signals of the `DataService` and controllers.
4.  **UI Update**: When `DataService` emits signals (e.g., `dataUpdated`), QML components automatically refresh to display the latest information.
5.  **User Interaction**: User actions in QML (e.g., clicking a button) trigger Q_INVOKABLE methods in C++ controllers.
6.  **Backend Communication**: Controllers then interact with services (e.g., `GrpcClientService`) to send commands or requests to the core ATS-V3 backend.
7.  **Visualization**: Charts and tables in QML visualize complex data, providing real-time insights into trading performance and system status.

## Integration with Other Modules

-   **`shared`**: Provides core data types, logging, and utility functions.
-   **`trading_engine`**: The `ui_dashboard` will communicate with the `trading_engine`'s gRPC service to fetch real-time trading data, portfolio information, and send trading commands.
-   **`risk_manager`**: Will connect to the `risk_manager`'s gRPC service to display real-time risk status, P&L, positions, and alerts.
-   **`backtest_analytics`**: Will provide an interface for configuring and running backtests, and for visualizing backtest results.
-   **`notification_service`**: Will display system alerts and notifications received from the `notification_service`.
-   **`security`**: Will handle user login, session management, and potentially display 2FA setup.

## Design Philosophy

The `ui_dashboard` module is designed with a focus on:
-   **User Experience**: Intuitive, responsive, and visually appealing interface.
-   **Real-time Insights**: Providing up-to-the-minute data and visualizations.
-   **Modularity**: Clear separation between C++ backend logic and QML frontend presentation.
-   **Extensibility**: Easy to add new UI features, pages, or integrate with new backend data sources.
-   **Cross-Platform**: Leveraging Qt/QML for deployment across various operating systems.
-   **Observability**: Acting as a primary interface for monitoring the health and performance of the entire ATS-V3 system.

