cmake_minimum_required(VERSION 3.16)
project(ATS-V3 LANGUAGES CXX)

# Set C++ standard  
# Use C++17 to avoid ranges issues for now
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific flags
if(MSVC)
    # MSVC compiler flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /Zi /DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
    # Disable specific MSVC warnings
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    # GCC/Clang compiler flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Find external dependencies - vcpkg makes this much easier
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Optional dependencies
find_package(OpenSSL QUIET)
find_package(jwt-cpp CONFIG QUIET)

# If vcpkg integration didn't work, try pkg-config as fallback
if(NOT CURL_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CURL QUIET libcurl)
    endif()
endif()

# Define source files
set(SOURCES
    src/main.cpp
    src/core/arbitrage_engine.cpp
    src/core/opportunity_detector.cpp
    src/core/portfolio_manager.cpp
    src/core/price_monitor.cpp
    src/core/risk_manager.cpp
    src/core/trade_executor.cpp
    src/data/market_data.cpp
    src/data/price_cache.cpp
    src/exchange/exchange_interface.cpp
    src/exchange/binance_exchange.cpp
    src/exchange/upbit_exchange.cpp
    src/monitoring/health_check.cpp
    src/monitoring/system_monitor.cpp
    src/network/rate_limiter.cpp
    src/network/rest_client.cpp
    src/network/websocket_client.cpp
    src/utils/config_manager.cpp
    src/utils/json_parser.cpp
    src/utils/logger.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    Threads::Threads
    CURL::libcurl
    nlohmann_json::nlohmann_json
)

# Link OpenSSL if available
if(OpenSSL_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_OPENSSL)
    message(STATUS "Found OpenSSL via vcpkg")
else()
    message(WARNING "OpenSSL not found - UpbitExchange will have limited functionality")
    message(STATUS "Install with: vcpkg install openssl:x64-windows")
endif()

# Link jwt-cpp if available
if(jwt-cpp_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE jwt-cpp::jwt-cpp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_JWT_CPP)
    message(STATUS "Found jwt-cpp")
else()
    message(WARNING "jwt-cpp not found - UpbitExchange will have limited functionality")
    message(STATUS "Install with: vcpkg install jwt-cpp:x64-windows")
endif()

# Set compile definitions for available libraries
target_compile_definitions(${PROJECT_NAME} PRIVATE 
    HAVE_CURL
    HAVE_NLOHMANN_JSON
)

message(STATUS "Found CURL via vcpkg")
message(STATUS "Found nlohmann_json via vcpkg")

# Enable position independent code
set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)

# Create directories for output
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logs)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/config)

# Copy configuration files
if(EXISTS ${CMAKE_SOURCE_DIR}/config/settings.json.example)
    configure_file(
        ${CMAKE_SOURCE_DIR}/config/settings.json.example
        ${CMAKE_BINARY_DIR}/config/settings.json.example
        COPYONLY
    )
endif()

# Install targets
install(TARGETS ${PROJECT_NAME}
    DESTINATION bin
)

install(FILES config/settings.json.example
    DESTINATION etc/ats-v3
    OPTIONAL
)

install(FILES systemd/ats-v3.service
    DESTINATION lib/systemd/system
    OPTIONAL
)

# Debug info
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")

# Platform-specific settings
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Link additional libraries on Linux
    target_link_libraries(${PROJECT_NAME} PRIVATE dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Windows-specific settings
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 winmm)
endif()

# Raspberry Pi specific optimizations
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    message(STATUS "ARM processor detected - applying ARM optimizations")
    if(NOT MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a72 -mfpu=neon-fp-armv8 -mfloat-abi=hard")
    endif()
endif()

# Add custom targets for common tasks
add_custom_target(run
    COMMAND ${CMAKE_BINARY_DIR}/${PROJECT_NAME}
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running ATS-V3"
)

add_custom_target(clean-logs
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/logs
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/logs
    COMMENT "Cleaning log files"
)

# Testing (if test files exist)
enable_testing()
if(EXISTS ${CMAKE_SOURCE_DIR}/tests)
    add_subdirectory(tests)
endif()

# CPack configuration for packaging
set(CPACK_PACKAGE_NAME "ats-v3")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Automated Trading System V3")
set(CPACK_PACKAGE_VENDOR "ATS-V3")
set(CPACK_PACKAGE_CONTACT "admin@ats-v3.local")
set(CPACK_GENERATOR "TGZ;DEB")

# DEB package specific
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17)")
if(CURL_FOUND)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libcurl4")
endif()

include(CPack) 