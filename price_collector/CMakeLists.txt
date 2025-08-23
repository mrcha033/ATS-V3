# Price Collector module CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Create price_collector library
add_library(price_collector STATIC
    # Core source files
    src/http_client.cpp
    src/websocket_client.cpp
    src/price_collector_service.cpp
    src/market_data_storage.cpp
    
    # Exchange adapters
    adapters/binance_adapter.cpp
    adapters/upbit_adapter.cpp
    adapters/coinbase_adapter.cpp
    adapters/kraken_adapter.cpp
    adapters/bitfinex_adapter.cpp
    
    # Header files (for IDE support)
    include/exchange_interface.hpp
    include/http_client.hpp
    include/websocket_client.hpp
    include/price_collector_service.hpp
    include/market_data_storage.hpp
    
    # Adapter headers
    adapters/binance_adapter.hpp
    adapters/upbit_adapter.hpp
    adapters/coinbase_adapter.hpp
    adapters/kraken_adapter.hpp
    adapters/bitfinex_adapter.hpp
)

# Include directories
target_include_directories(price_collector PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters
    ${CMAKE_SOURCE_DIR}/shared/include
)

# Link libraries
target_link_libraries(price_collector
    PUBLIC
        shared
        ${CONAN_LIBS}
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
    PRIVATE
        Boost::system
        Boost::thread
)

# Set properties
set_target_properties(price_collector PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Compiler-specific options
if(MSVC)
    target_compile_options(price_collector PRIVATE /W4)
    # Disable specific warnings for Boost.Beast
    target_compile_options(price_collector PRIVATE /wd4996 /wd4267 /wd4244)
else()
    target_compile_options(price_collector PRIVATE 
        -Wall -Wpedantic
        -Wno-unused-parameter
        -Wno-unused-variable
    )
endif()

# Define preprocessor macros
target_compile_definitions(price_collector PRIVATE
    BOOST_BEAST_USE_STD_STRING_VIEW
    BOOST_ASIO_HEADER_ONLY
    BOOST_ASIO_SEPARATE_COMPILATION
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(price_collector PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601
    )
    target_link_libraries(price_collector PRIVATE ws2_32 wsock32)
endif()

# Debug/Release specific settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(price_collector PRIVATE
        DEBUG_BUILD
        BOOST_ASIO_ENABLE_HANDLER_TRACKING
    )
endif()

# Install headers for development
install(DIRECTORY include/
    DESTINATION include/price_collector
    FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY adapters/
    DESTINATION include/price_collector/adapters
    FILES_MATCHING PATTERN "*.hpp"
)