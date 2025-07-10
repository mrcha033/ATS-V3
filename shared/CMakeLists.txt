# Shared library CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Create shared library
add_library(shared STATIC
    # Source files
    src/utils/logger.cpp
    src/utils/crypto_utils.cpp
    src/config/config_manager.cpp
    
    # Header files (for IDE support)
    include/types/common_types.hpp
    include/utils/logger.hpp
    include/utils/crypto_utils.hpp
    include/config/config_manager.hpp
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
    target_compile_options(shared PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Define version
target_compile_definitions(shared PRIVATE
    ATS_VERSION_MAJOR=1
    ATS_VERSION_MINOR=0
    ATS_VERSION_PATCH=0
)