file(GLOB_RECURSE SOURCES
    "core/*.cpp"
    "data/*.cpp"
    "exchange/*.cpp"
    "monitoring/*.cpp"
    "network/*.cpp"
    "utils/*.cpp"
)

# Exclude main.cpp from library
list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

# Create static library
add_library(ats-v3-lib STATIC ${SOURCES} data/sqlite3.c)

# Create main executable
add_executable(ats-v3 main.cpp)

# Link libraries
target_link_libraries(ats-v3-lib 
    ${PLATFORM_LIBS}
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(ats-v3 
    ats-v3-lib
    ${PLATFORM_LIBS}
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Include directories
target_include_directories(ats-v3-lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(ats-v3 PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Add platform-specific include directories
if(LIBCURL_FOUND)
    target_include_directories(ats-v3-lib PUBLIC ${LIBCURL_INCLUDE_DIRS})
    target_include_directories(ats-v3 PRIVATE ${LIBCURL_INCLUDE_DIRS})
    target_compile_definitions(ats-v3-lib PRIVATE ${LIBCURL_CFLAGS_OTHER})
    target_compile_definitions(ats-v3 PRIVATE ${LIBCURL_CFLAGS_OTHER})
endif()
