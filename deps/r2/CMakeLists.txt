cmake_minimum_required(VERSION 3.2)

project(rtwo)

ADD_DEFINITIONS(-std=c++1z)

set(CMAKE_CXX_FLAGS "-O2")

include_directories("../")

## install dependies
#include(deps/deps.cmake)

add_library(r2 src/logging.cc src/sshed.cc)
target_include_directories (r2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(r2 boost_context boost_system boost_coroutine boost_thread boost_chrono)


## tests
#include(tests/tests.cmake)


## benchs
include(benchs/bench.cmake)

#[[
enable_testing()

add_test(NAME test COMMAND tests/coretest)
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --verbose
                  DEPENDS tests/coretest )
]]


