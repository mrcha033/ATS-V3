# Qt Desktop Dashboard UI Module
cmake_minimum_required(VERSION 3.16)

set(MODULE_NAME ui_dashboard)

# Find required Qt6 components
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Quick
    QuickControls2
    Widgets
    Charts
    Pdf
    Network
    Concurrent
    Sql
)

# Enable automatic Qt tool invocation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Set Qt6 as required
qt_standard_project_setup()

# Include directories
include_directories(include)
include_directories(../shared/include)
include_directories(../security/include)
include_directories(../backtest_analytics/include)
include_directories(../trading_engine/include)

# Source files
set(SOURCES
    src/main.cpp
    src/dashboard_application.cpp
    src/models/portfolio_model.cpp
    src/models/trade_log_model.cpp
    src/models/alert_model.cpp
    src/services/grpc_client_service.cpp
    src/services/data_service.cpp
    src/services/notification_service.cpp
    src/services/localization_service.cpp
    src/services/pdf_report_service.cpp
    src/controllers/dashboard_controller.cpp
    src/controllers/trading_controller.cpp
    src/controllers/settings_controller.cpp
    src/utils/chart_data_provider.cpp
    src/utils/theme_manager.cpp
)

# Header files
set(HEADERS
    include/dashboard_application.hpp
    include/models/portfolio_model.hpp
    include/models/trade_log_model.hpp
    include/models/alert_model.hpp
    include/services/grpc_client_service.hpp
    include/services/data_service.hpp
    include/services/notification_service.hpp
    include/services/localization_service.hpp
    include/services/pdf_report_service.hpp
    include/controllers/dashboard_controller.hpp
    include/controllers/trading_controller.hpp
    include/controllers/settings_controller.hpp
    include/utils/chart_data_provider.hpp
    include/utils/theme_manager.hpp
)

# QML files
set(QML_SOURCES
    qml/main.qml
    qml/components/MaterialButton.qml
    qml/components/MaterialCard.qml
    qml/components/MaterialTextField.qml
    qml/pages/DashboardPage.qml
    qml/pages/TradingPage.qml
    qml/pages/BacktestPage.qml
    qml/pages/SettingsPage.qml
    qml/widgets/PortfolioChart.qml
    qml/widgets/TradeLogTable.qml
    qml/widgets/AlertPanel.qml
    qml/widgets/StrategyControls.qml
    qml/dialogs/SettingsDialog.qml
    qml/dialogs/AboutDialog.qml
)

# Resource files
set(RESOURCE_FILES
    resources/icons/dashboard.svg
    resources/icons/trading.svg
    resources/icons/backtest.svg
    resources/icons/settings.svg
    resources/images/logo.png
    resources/styles/Material.qml
    resources/themes/dark.qml
    resources/themes/light.qml
)

# Translation files
set(TRANSLATION_FILES
    translations/app_ko.ts
    translations/app_en.ts
)

# Qt resource file
qt_add_resources(RESOURCES
    PREFIX "/"
    FILES ${QML_SOURCES} ${RESOURCE_FILES}
)

# Create executable
qt_add_executable(${MODULE_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Qt QML module
qt_add_qml_module(${MODULE_NAME}
    URI ATS.Dashboard
    VERSION 1.0
    QML_FILES ${QML_SOURCES}
    RESOURCES ${RESOURCE_FILES}
)

# Link Qt libraries
target_link_libraries(${MODULE_NAME}
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Charts
    Qt6::Pdf
    Qt6::Network
    Qt6::Concurrent
    Qt6::Sql
)

# Link project modules
target_link_libraries(${MODULE_NAME}
    shared
    security
    backtest_analytics
    trading_engine
    ${CMAKE_THREAD_LIBS_INIT}
)

# Set target properties
set_target_properties(${MODULE_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${MODULE_NAME} PRIVATE
        -Wall -Wextra -Wpedantic
        -O3 -march=native
    )
endif()

# Debug configuration
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${MODULE_NAME} PRIVATE DEBUG=1 QT_QML_DEBUG=1)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${MODULE_NAME} PRIVATE -g -O0)
    endif()
endif()

# Platform-specific configurations
if(WIN32)
    # Windows-specific settings
    set_target_properties(${MODULE_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    # Add Windows resources if needed
    # target_sources(${MODULE_NAME} PRIVATE resources/app.rc)
endif()

if(APPLE)
    # macOS-specific settings
    set_target_properties(${MODULE_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.ats.dashboard
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        MACOSX_BUNDLE_BUNDLE_NAME "ATS Dashboard"
    )
endif()

if(UNIX AND NOT APPLE)
    # Linux-specific settings
    install(FILES resources/desktop/ats-dashboard.desktop
        DESTINATION share/applications
    )
    install(FILES resources/icons/ats-dashboard.png
        DESTINATION share/icons/hicolor/256x256/apps
    )
endif()

# Translations
qt_add_translations(${MODULE_NAME}
    TS_FILES ${TRANSLATION_FILES}
    RESOURCE_PREFIX "/translations"
)

# Install target
install(TARGETS ${MODULE_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Qt deployment
if(WIN32)
    # Windows deployment
    find_program(QT_WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(QT_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${MODULE_NAME} POST_BUILD
            COMMAND ${QT_WINDEPLOYQT_EXECUTABLE} --qmldir ${CMAKE_CURRENT_SOURCE_DIR}/qml $<TARGET_FILE:${MODULE_NAME}>
            COMMENT "Deploying Qt libraries"
        )
    endif()
endif()

if(APPLE)
    # macOS deployment
    find_program(QT_MACDEPLOYQT_EXECUTABLE macdeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(QT_MACDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${MODULE_NAME} POST_BUILD
            COMMAND ${QT_MACDEPLOYQT_EXECUTABLE} $<TARGET_BUNDLE_DIR:${MODULE_NAME}> -qmldir=${CMAKE_CURRENT_SOURCE_DIR}/qml
            COMMENT "Deploying Qt libraries"
        )
    endif()
endif()

# Testing
option(BUILD_UI_TESTS "Build UI dashboard tests" ON)

if(BUILD_UI_TESTS)
    enable_testing()
    
    # Qt Test framework
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        set(TEST_SOURCES
            tests/test_dashboard_controller.cpp
            tests/test_portfolio_model.cpp
            tests/test_grpc_service.cpp
        )
        
        file(GLOB TEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
        if(TEST_FILES)
            qt_add_executable(${MODULE_NAME}_tests ${TEST_FILES})
            
            target_link_libraries(${MODULE_NAME}_tests
                ${MODULE_NAME}
                Qt6::Test
                Qt6::Quick
                Qt6::QuickTest
            )
            
            add_test(NAME ${MODULE_NAME}_tests COMMAND ${MODULE_NAME}_tests)
        endif()
    endif()
endif()

# Package information
set(DASHBOARD_VERSION_MAJOR 1)
set(DASHBOARD_VERSION_MINOR 0)
set(DASHBOARD_VERSION_PATCH 0)
set(DASHBOARD_VERSION ${DASHBOARD_VERSION_MAJOR}.${DASHBOARD_VERSION_MINOR}.${DASHBOARD_VERSION_PATCH})

target_compile_definitions(${MODULE_NAME} PRIVATE
    APP_VERSION="${DASHBOARD_VERSION}"
    APP_NAME="ATS Dashboard"
    ORGANIZATION_NAME="ATS Trading Systems"
    ORGANIZATION_DOMAIN="ats.trading"
)

# Configuration summary
message(STATUS "UI Dashboard Configuration:")
message(STATUS "  Version: ${DASHBOARD_VERSION}")
message(STATUS "  Qt Version: ${Qt6_VERSION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Tests: ${BUILD_UI_TESTS}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")

# Static analysis support
option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis" OFF)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        set_target_properties(${MODULE_NAME} PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=-*,readability-*,performance-*"
        )
    endif()
endif()