cmake_minimum_required(VERSION 3.16)
project(ATS_V3 VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Raspberry Pi ARM optimization flags
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=armv8-a+crc+simd -mtune=cortex-a72 -flto")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -march=armv8-a+crc+simd -mtune=cortex-a72 -flto")
    message(STATUS "ARM optimization flags enabled for Raspberry Pi")
endif()

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Find libcurl (optional for now)
find_package(CURL QUIET)

# Find SQLite3 (optional for now)
find_package(SQLite3 QUIET)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Source files - Phase 1, 2 & 3 Complete
set(SOURCES
    # Main application
    src/main.cpp
    
    # Utilities (Phase 1)
    src/utils/logger.cpp
    src/utils/config_manager.cpp
    src/utils/json_parser.cpp
    
    # Network layer (Phase 2)
    src/network/rest_client.cpp
    src/network/rate_limiter.cpp
    src/network/websocket_client.cpp
    
    # Data management (Phase 2) 
    src/data/market_data.cpp
    src/data/price_cache.cpp
    
    # Core arbitrage engine (Phase 1-3)
    src/core/arbitrage_engine.cpp
    src/core/price_monitor.cpp
    src/core/opportunity_detector.cpp
    src/core/risk_manager.cpp
    src/core/trade_executor.cpp
    src/core/portfolio_manager.cpp
    
    # Exchange interface (Phase 1)
    src/exchange/exchange_interface.cpp
    
    # Monitoring (Phase 1)
    src/monitoring/system_monitor.cpp
    src/monitoring/health_check.cpp
    
    # TODO: Phase 3-5 components
    # src/utils/time_utils.cpp
    # src/utils/math_utils.cpp  
    # src/utils/memory_monitor.cpp
    # src/exchange/exchange_manager.cpp
    # src/exchange/binance.cpp
    # src/exchange/upbit.cpp
    # src/network/websocket_client.cpp
    # src/network/connection_pool.cpp
    # src/data/trade_data.cpp
    # src/data/database.cpp
    # src/core/opportunity_detector.cpp
    # src/core/trade_executor.cpp
    # src/core/risk_manager.cpp
    # src/core/portfolio_manager.cpp
    # src/monitoring/performance_monitor.cpp
    # src/monitoring/alert_manager.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Threads::Threads
)

# Link optional libraries if found
if(CURL_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CURL_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_CURL=1)
endif()

if(SQLite3_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE SQLite::SQLite3)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_SQLITE3=1)
endif()

# Compiler-specific options
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3 /WX-
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -fPIC
    )
endif()

# Install target
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Install config files
install(DIRECTORY config/
    DESTINATION etc/ats-v3
    FILES_MATCHING PATTERN "*.json"
)

# Install systemd service
install(FILES systemd/ats-v3.service
    DESTINATION /etc/systemd/system
) 