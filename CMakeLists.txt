cmake_minimum_required(VERSION 3.10)
project(ATS-V3 CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific options
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_WIN32_WINNT=0x0601) # Windows 7+
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)
    endif()
endif()

# Platform-specific libraries
if(WIN32)
    # Windows-specific libraries
    set(PLATFORM_LIBS ws2_32 winhttp)
else()
    # Unix-specific libraries
    set(PLATFORM_LIBS)
    # Try to find curl, but make it optional
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBCURL QUIET libcurl)
        if(LIBCURL_FOUND)
            set(PLATFORM_LIBS ${PLATFORM_LIBS} ${LIBCURL_LIBRARIES})
            add_compile_definitions(HAS_LIBCURL)
        endif()
    endif()
endif()

# Find Threads (usually available)
find_package(Threads REQUIRED)

# Include nlohmann/json header-only library
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(src)
add_subdirectory(tests)
