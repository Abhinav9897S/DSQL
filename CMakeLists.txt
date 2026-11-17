cmake_minimum_required(VERSION 3.16)
project(MiniSQL VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The no-STL rule is checked on every build, Windows included.
add_custom_target(audit_stl ALL
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/AuditStl.cmake
    COMMENT "Checking that no STL containers are used")

# The engine: data structures, storage, indexes, SQL. Plain C++17, no Qt.
file(GLOB ENGINE_SRC CONFIGURE_DEPENDS
    src/ds/*.cpp src/util/*.cpp src/storage/*.cpp src/index/*.cpp src/sql/*.cpp)
add_library(minisql_engine STATIC ${ENGINE_SRC})
target_include_directories(minisql_engine PUBLIC src)
add_dependencies(minisql_engine audit_stl)

# The desktop app. Listed first so Qt Creator picks it as the thing to run.
find_package(Qt6 6.2 COMPONENTS Widgets)
if(Qt6_FOUND)
    file(GLOB GUI_SRC CONFIGURE_DEPENDS src/gui/*.cpp src/gui/*.h)
    add_executable(MiniSQL ${GUI_SRC} resources.qrc)
    set_target_properties(MiniSQL PROPERTIES
        AUTOMOC ON
        AUTORCC ON
        WIN32_EXECUTABLE ON
        MACOSX_BUNDLE ON)
    target_link_libraries(MiniSQL PRIVATE minisql_engine Qt6::Widgets)
else()
    message(WARNING "Qt 6 not found: building only the engine, the CLI and the tests. "
                    "Install Qt 6 or point CMAKE_PREFIX_PATH at it to get the MiniSQL app.")
endif()

# Terminal version of the engine: SQL prompt, script runner, benchmark.
add_executable(minisql-cli src/main.cpp src/Cli.cpp)
target_link_libraries(minisql-cli PRIVATE minisql_engine)

add_executable(run_tests tests/test_main.cpp)
target_link_libraries(run_tests PRIVATE minisql_engine)
enable_testing()
add_test(NAME minisql_tests COMMAND run_tests)
