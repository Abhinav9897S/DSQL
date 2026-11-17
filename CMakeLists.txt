cmake_minimum_required(VERSION 3.10)
project(minisql CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB ENGINE_SRC src/ds/*.cpp src/util/*.cpp src/storage/*.cpp src/index/*.cpp src/sql/*.cpp)
file(GLOB SERVER_SRC src/server/*.cpp)

add_library(minisql_engine STATIC ${ENGINE_SRC})
target_include_directories(minisql_engine PUBLIC src)

add_executable(minisql src/main.cpp src/Cli.cpp ${SERVER_SRC})
target_link_libraries(minisql minisql_engine)
if(WIN32)
  target_link_libraries(minisql ws2_32)
endif()

add_executable(run_tests tests/test_main.cpp)
target_link_libraries(run_tests minisql_engine)

enable_testing()
add_test(NAME minisql_tests COMMAND run_tests)
