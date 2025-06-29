cmake_minimum_required(VERSION 3.10)
project(ATS-V3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(vcpkg/installed/x64-windows/include)
link_directories(vcpkg/installed/x64-windows/lib)

add_subdirectory(src)
add_subdirectory(tests)

add_executable(${PROJECT_NAME} src/main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE ats-v3-lib)
