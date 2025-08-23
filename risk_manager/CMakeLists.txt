# Risk Manager CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Create risk manager library
add_library(risk_manager STATIC
    # Source files
    src/enhanced_risk_manager.cpp
    src/risk_manager_grpc_service.cpp
    
    # Header files (for IDE support)
    include/enhanced_risk_manager.hpp
)

# Include directories
target_include_directories(risk_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/shared/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/trading_engine/include
)

# Link libraries
target_link_libraries(risk_manager
    PUBLIC
        shared
        ${CONAN_LIBS}
        Threads::Threads
)

# Set properties
set_target_properties(risk_manager PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Compiler-specific options
if(MSVC)
    target_compile_options(risk_manager PRIVATE /W4)
else()
    target_compile_options(risk_manager PRIVATE -Wall -Wpedantic)
endif()

# Note: Implementation files will be added when the enhanced risk manager is fully implemented