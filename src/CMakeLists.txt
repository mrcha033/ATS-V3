file(GLOB_RECURSE SOURCES
    "core/*.cpp"
    "data/*.cpp"
    "exchange/*.cpp"
    "monitoring/*.cpp"
    "network/*.cpp"
    "utils/*.cpp"
)

add_library(ats-v3-lib ${SOURCES} data/sqlite3.c)

target_include_directories(ats-v3-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
