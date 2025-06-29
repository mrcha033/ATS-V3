


cmake_minimum_required(VERSION 3.10)

project(ATS-V3-Tests)

# Include the main executable's sources to resolve dependencies
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/googletest/googletest/include
    ${CMAKE_CURRENT_SOURCE_DIR}/googletest/googlemock/include
)

add_executable(run_tests
    test_main.cpp
    test_portfolio_manager.cpp
    test_risk_manager.cpp
    test_arbitrage_engine.cpp
    test_opportunity_detector.cpp
    test_price_monitor.cpp
)

add_subdirectory(googletest)

target_link_libraries(run_tests
    gtest
    gmock
    gtest_main
    ats-v3-lib
)

include(GoogleTest)
gtest_discover_tests(run_tests)





