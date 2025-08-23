cmake_minimum_required(VERSION 3.16)

# Notification Service Library
add_library(notification_service
    src/push_notification_service.cpp
    src/email_notification_service.cpp
    src/notification_settings_service.cpp
    src/notification_influxdb_storage.cpp
    src/notification_settings_utils.cpp
)

# Set target properties
set_target_properties(notification_service PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Include directories
target_include_directories(notification_service
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/shared/include
        ${CMAKE_SOURCE_DIR}/security/include
)

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(CURL QUIET)

# Find nlohmann_json
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
    HINTS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
        ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/include
)

if(NLOHMANN_JSON_INCLUDE_DIR)
    message(STATUS "Found nlohmann/json at: ${NLOHMANN_JSON_INCLUDE_DIR}")
    target_include_directories(notification_service PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
    target_compile_definitions(notification_service PRIVATE HAS_NLOHMANN_JSON)
else()
    message(STATUS "nlohmann/json not found - JSON serialization disabled")
endif()

# Link libraries
target_link_libraries(notification_service
    PUBLIC
        shared
        security
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Link CURL if available
if(CURL_FOUND)
    target_link_libraries(notification_service PUBLIC CURL::libcurl)
    target_compile_definitions(notification_service PRIVATE HAS_CURL)
    message(STATUS "CURL found - HTTP notifications enabled")
else()
    message(STATUS "CURL not found - HTTP notifications disabled")
endif()

# Compiler flags
if(MSVC)
    target_compile_options(notification_service PRIVATE
        /W4
        /wd4100  # unreferenced formal parameter
        /wd4996  # deprecated function
    )
else()
    target_compile_options(notification_service PRIVATE
        -Wall
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(notification_service PRIVATE
        _WIN32_WINNT=0x0601
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(notification_service PRIVATE
        ws2_32
        wldap32
        crypt32
        winmm
    )
elseif(UNIX)
    target_link_libraries(notification_service PRIVATE
        dl
        m
    )
endif()

# Installation
install(TARGETS notification_service
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include/notification_service
    FILES_MATCHING PATTERN "*.hpp"
)

# Tests (if enabled)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Create example/demo executable for testing
add_executable(notification_demo
    examples/notification_demo.cpp
)

target_link_libraries(notification_demo
    notification_service
    shared
    security
)

target_include_directories(notification_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Create simple test executable
add_executable(notification_simple_test
    examples/simple_test.cpp
)

target_link_libraries(notification_simple_test
    notification_service
    shared
    security
)

target_include_directories(notification_simple_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Install demo and test
install(TARGETS notification_demo notification_simple_test
    RUNTIME DESTINATION bin
)

# Add custom target for documentation
add_custom_target(notification_docs
    COMMAND doxygen ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating notification service documentation"
    VERBATIM
)

# Add this library to the main project
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # This is the main project
    message(STATUS "Building notification service as part of main project")
else()
    # This is included as a subdirectory
    message(STATUS "Building notification service as subdirectory")
endif()