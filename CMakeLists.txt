cmake_minimum_required(VERSION 3.16)

# Set CMake policies before project() and any find_package calls
# cmake_policy(SET CMP0167 NEW)  # Use new FindBoost behavior - commented out for older CMake compatibility

project(ATS-V3 VERSION 1.0.0 LANGUAGES CXX)

# Include Conan toolchain if it exists
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/conan_toolchain.cmake")
    include("${CMAKE_CURRENT_BINARY_DIR}/conan_toolchain.cmake")
endif()

# Generate compile_commands.json for IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Set compiler flags
if(MSVC)
    set(CMAKE_CXX_FLAGS "/W4")
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2")
else()
    set(CMAKE_CXX_FLAGS "-Wall")
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

# Find packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Qt6 for UI dashboard
find_package(Qt6 QUIET COMPONENTS Core Quick QuickControls2 Widgets Charts Pdf)
if(Qt6_FOUND)
    set(BUILD_UI_DASHBOARD ON)
    message(STATUS "Qt6 found - UI Dashboard will be built")
else()
    set(BUILD_UI_DASHBOARD OFF)
    message(STATUS "Qt6 not found - UI Dashboard will be skipped")
endif()

# Include Conan dependencies
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/shared/include
    ${CMAKE_SOURCE_DIR}/security/include
    ${CMAKE_SOURCE_DIR}/price_collector/include
    ${CMAKE_SOURCE_DIR}/trading_engine/include
    ${CMAKE_SOURCE_DIR}/risk_manager/include
    ${CMAKE_SOURCE_DIR}/backtest_analytics/include
    ${CMAKE_SOURCE_DIR}/notification_service/include
    ${CMAKE_SOURCE_DIR}/third_party
)

# Add subdirectories
add_subdirectory(shared)
# add_subdirectory(security)
# add_subdirectory(price_collector)
# add_subdirectory(trading_engine)
# add_subdirectory(risk_manager)
# add_subdirectory(backtest_analytics)
# add_subdirectory(notification_service)

# Add UI dashboard if Qt6 is available
if(BUILD_UI_DASHBOARD)
    add_subdirectory(ui_dashboard)
endif()

# Main executable
add_executable(ats-v3
    main.cpp
)

# Link libraries - minimal for testing
target_link_libraries(ats-v3
    shared
    Threads::Threads
)

# Install targets
install(TARGETS ats-v3
    RUNTIME DESTINATION bin
)

# Testing - disabled for now
# enable_testing()
# add_subdirectory(tests)