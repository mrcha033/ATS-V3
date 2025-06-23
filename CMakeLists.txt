cmake_minimum_required(VERSION 3.16)
project(ATS-V3 LANGUAGES CXX)

# Set C++ standard
# Note: C++20 is preferred (as documented in README) but requires GCC 8+ or Clang 10+
# Current compiler GNU 6.3.0 only supports up to C++17
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

# Find external dependencies for UpbitExchange
find_package(PkgConfig QUIET)
find_package(OpenSSL QUIET)

# Find jwt-cpp (header-only library)
find_path(JWT_CPP_INCLUDE_DIR
    NAMES jwt-cpp/jwt.h
    PATHS /usr/include /usr/local/include
    PATH_SUFFIXES jwt-cpp
)

# Find libuuid
if(PkgConfig_FOUND)
    pkg_check_modules(UUID QUIET uuid)
endif()

# Find CURL package (works with vcpkg)
find_package(CURL QUIET)

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
)

# Link OpenSSL if available
if(OpenSSL_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_OPENSSL)
    message(STATUS "Found OpenSSL: ${OPENSSL_VERSION}")
else()
    message(WARNING "OpenSSL not found - UpbitExchange will have limited functionality")
endif()

# Link uuid if available
if(UUID_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${UUID_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${UUID_INCLUDE_DIRS})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_UUID)
    message(STATUS "Found libuuid")
else()
    message(WARNING "libuuid not found - UpbitExchange will have limited functionality")
endif()

# Include jwt-cpp if available
if(JWT_CPP_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${JWT_CPP_INCLUDE_DIR})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_JWT_CPP)
    message(STATUS "Found jwt-cpp headers")
else()
    message(WARNING "jwt-cpp not found - UpbitExchange will have limited functionality")
endif()

# Link curl if available
if(CURL_FOUND)
    if(TARGET CURL::libcurl)
        # Modern CMake target (vcpkg provides this)
        target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
        target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_CURL)
        message(STATUS "Found libcurl via CMake: ${CURL_VERSION}")
    elseif(CURL_LIBRARIES)
        # Legacy variables (pkg-config provides this)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${CURL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
        target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_CURL)
        message(STATUS "Found libcurl via pkg-config: ${CURL_VERSION}")
    endif()
else()
    message(WARNING "libcurl not found - REST client will have limited functionality")
    message(STATUS "To install libcurl on Windows:")
    message(STATUS "  1. Install vcpkg: git clone https://github.com/Microsoft/vcpkg.git")
    message(STATUS "  2. Bootstrap: .\\vcpkg\\bootstrap-vcpkg.bat")
    message(STATUS "  3. Install curl: .\\vcpkg\\vcpkg install curl:x64-windows")
    message(STATUS "  4. Integrate: .\\vcpkg\\vcpkg integrate install")
    message(STATUS "  5. Reconfigure with: cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake")
    target_compile_definitions(${PROJECT_NAME} PRIVATE NO_CURL)
endif()

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