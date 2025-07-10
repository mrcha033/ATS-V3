cmake_minimum_required(VERSION 3.16)
project(ATS-V3 VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Set compiler flags
set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Find packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

# Include Conan
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/shared/include
    ${CMAKE_SOURCE_DIR}/price_collector/include
    ${CMAKE_SOURCE_DIR}/trading_engine/include
    ${CMAKE_SOURCE_DIR}/risk_manager/include
    ${CMAKE_SOURCE_DIR}/backtest_analytics/include
)

# Add subdirectories
add_subdirectory(shared)
add_subdirectory(price_collector)
add_subdirectory(trading_engine)
add_subdirectory(risk_manager)
add_subdirectory(backtest_analytics)

# Main executable
add_executable(ats-v3
    src/main.cpp
)

# Link libraries
target_link_libraries(ats-v3
    shared
    price_collector
    trading_engine
    risk_manager
    backtest_analytics
    ${CONAN_LIBS}
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    ${Boost_LIBRARIES}
)

# Install targets
install(TARGETS ats-v3
    RUNTIME DESTINATION bin
)

# Testing
enable_testing()
add_subdirectory(tests)