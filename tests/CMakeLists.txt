# Enable testing
enable_testing()

# Use FetchContent to get GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.13.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Test executable
add_executable(run_tests
    test_main.cpp
    test_portfolio_manager.cpp
    test_risk_manager.cpp
    test_arbitrage_engine.cpp
    test_opportunity_detector.cpp
    test_price_monitor.cpp
    integration/test_end_to_end.cpp
    integration/test_exchange_integration.cpp
)

# Include directories
target_include_directories(run_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
)

# Link libraries
target_link_libraries(run_tests
    ats-v3-lib
    gtest_main
    gmock_main
    nlohmann_json::nlohmann_json
)

# Compiler definitions
target_compile_definitions(run_tests PRIVATE
    GTEST_HAS_PTHREAD=1
)

# Set C++ standard for tests
set_target_properties(run_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Discover tests
include(GoogleTest)
gtest_discover_tests(run_tests)