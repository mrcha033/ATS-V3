# Trading Engine CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Create trading engine library
add_library(trading_engine STATIC
    # Source files
    src/trading_engine_service.cpp
    src/order_router.cpp
    src/redis_subscriber.cpp
    src/spread_calculator.cpp
    src/trade_logger.cpp
    src/exchange_trading_adapter.cpp
    
    # Header files (for IDE support)
    include/trading_engine_service.hpp
    include/order_router.hpp
    include/redis_subscriber.hpp
    include/spread_calculator.hpp
    include/exchange_trading_adapter.hpp
    include/influxdb_client.hpp
    include/rollback_manager.hpp
    grpc/trading_engine_grpc_service.hpp
)

# Include directories
target_include_directories(trading_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/shared/include
    ${CMAKE_SOURCE_DIR}/security/include
)

# Link libraries
target_link_libraries(trading_engine
    PUBLIC
        shared
        security
        ${CONAN_LIBS}
        Threads::Threads
)

# Set properties
set_target_properties(trading_engine PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Compiler-specific options
if(MSVC)
    target_compile_options(trading_engine PRIVATE /W4)
else()
    target_compile_options(trading_engine PRIVATE -Wall -Wpedantic)
endif()