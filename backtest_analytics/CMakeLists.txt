# Backtest Analytics Module
cmake_minimum_required(VERSION 3.16)

# Set module name
set(MODULE_NAME backtest_analytics)

# Include directories
include_directories(include)
include_directories(../shared/include)

# Source files
set(SOURCES
    src/backtest_engine.cpp
    src/data_loader.cpp
    src/performance_metrics.cpp
    src/ai_prediction_module.cpp
    src/influxdb_storage.cpp
)

# Header files
set(HEADERS
    include/backtest_engine.hpp
    include/data_loader.hpp
    include/performance_metrics.hpp
    include/ai_prediction_module.hpp
    include/influxdb_storage.hpp
)

# Create static library
add_library(${MODULE_NAME} STATIC ${SOURCES} ${HEADERS})

# Link with shared utilities
target_link_libraries(${MODULE_NAME} 
    shared
    ${CMAKE_THREAD_LIBS_INIT}
)

# Set target properties
set_target_properties(${MODULE_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${MODULE_NAME} PRIVATE
        -Wall -Wpedantic
        -O3
        -march=native
        -mtune=native
    )
endif()

# Debug build options
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${MODULE_NAME} PRIVATE DEBUG=1)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${MODULE_NAME} PRIVATE
            -g -O0
            -fsanitize=address
            -fno-omit-frame-pointer
        )
        target_link_options(${MODULE_NAME} PRIVATE
            -fsanitize=address
        )
    endif()
endif()

# Install targets
install(TARGETS ${MODULE_NAME}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${HEADERS}
    DESTINATION include/${MODULE_NAME}
)

# Export target for use by other modules
export(TARGETS ${MODULE_NAME}
    FILE ${CMAKE_BINARY_DIR}/${MODULE_NAME}Targets.cmake
)

# Optional: Build tests if enabled
option(BUILD_BACKTEST_TESTS "Build backtest analytics tests" ON)

if(BUILD_BACKTEST_TESTS)
    # Test executable
    set(TEST_SOURCES
        tests/test_backtest_engine.cpp
        tests/test_performance_metrics.cpp
        tests/test_data_loader.cpp
        tests/test_ai_prediction.cpp
    )
    
    # Create tests directory if it doesn't exist
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    
    # Only add test executable if test files exist
    file(GLOB TEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    if(TEST_FILES)
        add_executable(${MODULE_NAME}_tests ${TEST_FILES})
        
        target_link_libraries(${MODULE_NAME}_tests
            ${MODULE_NAME}
            shared
            ${CMAKE_THREAD_LIBS_INIT}
        )
        
        # Add test target
        add_test(NAME ${MODULE_NAME}_tests COMMAND ${MODULE_NAME}_tests)
    endif()
endif()

# Documentation
option(BUILD_BACKTEST_DOCS "Build backtest analytics documentation" OFF)

if(BUILD_BACKTEST_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
        set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
        
        if(EXISTS ${DOXYGEN_IN})
            configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
            
            add_custom_target(${MODULE_NAME}_docs ALL
                COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                COMMENT "Generating API documentation for ${MODULE_NAME}"
                VERBATIM
            )
        endif()
    endif()
endif()

# Performance benchmarks
option(BUILD_BACKTEST_BENCHMARKS "Build backtest performance benchmarks" OFF)

if(BUILD_BACKTEST_BENCHMARKS)
    file(GLOB BENCHMARK_FILES "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp")
    if(BENCHMARK_FILES)
        add_executable(${MODULE_NAME}_benchmarks ${BENCHMARK_FILES})
        
        target_link_libraries(${MODULE_NAME}_benchmarks
            ${MODULE_NAME}
            shared
            ${CMAKE_THREAD_LIBS_INIT}
        )
        
        # Set high optimization for benchmarks
        target_compile_options(${MODULE_NAME}_benchmarks PRIVATE
            -O3 -DNDEBUG -march=native -mtune=native
        )
    endif()
endif()

# Package configuration
set(BACKTEST_ANALYTICS_VERSION_MAJOR 1)
set(BACKTEST_ANALYTICS_VERSION_MINOR 0)
set(BACKTEST_ANALYTICS_VERSION_PATCH 0)
set(BACKTEST_ANALYTICS_VERSION 
    ${BACKTEST_ANALYTICS_VERSION_MAJOR}.${BACKTEST_ANALYTICS_VERSION_MINOR}.${BACKTEST_ANALYTICS_VERSION_PATCH})

# Generate version header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/version.hpp"
    @ONLY
)

target_include_directories(${MODULE_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Print configuration summary
message(STATUS "Backtest Analytics Module Configuration:")
message(STATUS "  Version: ${BACKTEST_ANALYTICS_VERSION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Tests: ${BUILD_BACKTEST_TESTS}")
message(STATUS "  Documentation: ${BUILD_BACKTEST_DOCS}")
message(STATUS "  Benchmarks: ${BUILD_BACKTEST_BENCHMARKS}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")