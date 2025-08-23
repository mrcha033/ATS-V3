# Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Find required packages
find_package(GTest REQUIRED)
# find_package(GMock REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/shared/include
    ${CMAKE_SOURCE_DIR}/security/include
    ${CMAKE_SOURCE_DIR}/price_collector/include
    ${CMAKE_SOURCE_DIR}/trading_engine/include
    ${CMAKE_SOURCE_DIR}/risk_manager/include
    ${CMAKE_SOURCE_DIR}/backtest_analytics/include
)

# Shared utilities tests
add_executable(test_shared
    test_shared.cpp
)

target_link_libraries(test_shared
    PRIVATE
        shared
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        ${CONAN_LIBS}
)

# Add test to CTest
add_test(NAME SharedUtilsTest COMMAND test_shared)

# Set working directory for tests
set_tests_properties(SharedUtilsTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Test configuration for different build types
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(test_shared PRIVATE DEBUG_BUILD)
endif()

# Coverage support
if(ENABLE_COVERAGE)
    target_compile_options(test_shared PRIVATE --coverage)
    target_link_libraries(test_shared PRIVATE --coverage)
endif()

# Security tests
add_executable(test_security
    test_security.cpp
)

target_link_libraries(test_security
    PRIVATE
        shared
        security
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        OpenSSL::SSL
        OpenSSL::Crypto
        ${CONAN_LIBS}
)

# Add test to CTest
add_test(NAME SecurityTest COMMAND test_security)

# Set working directory for tests
set_tests_properties(SecurityTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Coverage support for security tests
if(ENABLE_COVERAGE)
    target_compile_options(test_security PRIVATE --coverage)
    target_link_libraries(test_security PRIVATE --coverage)
endif()

# Exchange plugin system tests
add_executable(test_exchange_plugin_system
    test_exchange_plugin_system.cpp
)

target_link_libraries(test_exchange_plugin_system
    PRIVATE
        shared
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        ${CONAN_LIBS}
)

# Add test to CTest
add_test(NAME ExchangePluginSystemTest COMMAND test_exchange_plugin_system)

# Set working directory for tests
set_tests_properties(ExchangePluginSystemTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Coverage support for plugin tests
if(ENABLE_COVERAGE)
    target_compile_options(test_exchange_plugin_system PRIVATE --coverage)
    target_link_libraries(test_exchange_plugin_system PRIVATE --coverage)
endif()

# Additional test targets will be added here for other modules
# add_executable(test_price_collector ...)
# add_executable(test_trading_engine ...)
# add_executable(test_risk_manager ...)
# add_executable(test_backtest_analytics ...)