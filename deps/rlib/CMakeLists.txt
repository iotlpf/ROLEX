cmake_minimum_required(VERSION 2.8...3.13)

add_definitions(-std=c++1z)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_COMPILER /usr/bin/g++)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")


## tests
enable_testing()

add_test(NAME test COMMAND coretest)
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --verbose
                  DEPENDS coretest)

include(tests/tests.cmake)


## for examples

include(examples/exp.cmake)

include(benchs/bench.cmake)


project (rlib)

