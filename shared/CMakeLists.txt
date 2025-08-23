# Shared library CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Try to find spdlog
find_package(spdlog QUIET)
if(spdlog_FOUND)
    message(STATUS "Found spdlog - advanced logging enabled")
else()
    message(STATUS "spdlog not found - using fallback logging")
endif()

# Create shared library
add_library(shared STATIC
    # Source files
    src/utils/logger.cpp
    src/utils/crypto_utils.cpp
    src/utils/prometheus_exporter.cpp
    src/config/config_manager.cpp
    src/exchange/failover_manager.cpp
    src/exchange/resilient_exchange_adapter.cpp
    src/exchange/exchange_notification_system.cpp
    src/exchange/exchange_plugin_manager.cpp
    src/exchange/base_exchange_plugin.cpp
    src/exchange/sample_exchange_plugin.cpp
    
    # Header files (for IDE support)
    include/types/common_types.hpp
    include/utils/logger.hpp
    include/utils/crypto_utils.hpp
    include/utils/prometheus_exporter.hpp
    include/utils/redis_client.hpp
    include/utils/influxdb_client.hpp
    include/config/config_manager.hpp
    include/exchange/failover_manager.hpp
    include/exchange/resilient_exchange_adapter.hpp
    include/exchange/exchange_notification_system.hpp
    include/exchange/exchange_plugin_interface.hpp
    include/exchange/exchange_plugin_manager.hpp
    include/exchange/base_exchange_plugin.hpp
)

# Include directories
target_include_directories(shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/shared/include
)

# Link libraries
target_link_libraries(shared
    PUBLIC
        ${CONAN_LIBS}
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)

# Link spdlog if available
if(spdlog_FOUND)
    target_link_libraries(shared PUBLIC spdlog::spdlog)
    target_compile_definitions(shared PUBLIC HAS_SPDLOG)
endif()

# Set properties
set_target_properties(shared PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Compiler-specific options
if(MSVC)
    target_compile_options(shared PRIVATE /W4)
else()
    target_compile_options(shared PRIVATE -Wall -Wpedantic)
endif()

# Define version
target_compile_definitions(shared PRIVATE
    ATS_VERSION_MAJOR=1
    ATS_VERSION_MINOR=0
    ATS_VERSION_PATCH=0
)