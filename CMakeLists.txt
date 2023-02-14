cmake_minimum_required(VERSION 3.2)

# set(CMAKE_CXX_COMPILER "clang++")

project(xxx)
ADD_DEFINITIONS(-std=c++17)

# directories
include_directories("./")
include_directories("deps")


## tests
include(benchs/Rolex/tests/tests.cmake)
enable_testing()

add_test(NAME test COMMAND coretest)
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --verbose
                  DEPENDS coretest )


set(LOG_SRC "./deps/r2/src/logging.cc"  "./deps/r2/src/sshed.cc"  "./benchs/terminate.cc")

# rolex
file(GLOB rolex_SORUCES ""  "./benchs/Rolex/rolex.cc"  "./deps/r2/src/logging.cc"  "./deps/r2/src/sshed.cc"  "./benchs/terminate.cc" )
add_executable(rolex ${rolex_SORUCES} )
target_link_libraries(rolex gflags ibverbs pthread boost_system boost_coroutine)


# rolex server and client
file(GLOB rolex_server_SORUCES ""  "./benchs/Rolex/rolex_server.cc"  "./deps/r2/src/logging.cc"  "./deps/r2/src/sshed.cc"  "./benchs/terminate.cc" )
add_executable(rolex_server ${rolex_server_SORUCES} )
target_link_libraries(rolex_server gflags ibverbs pthread boost_system boost_coroutine)
file(GLOB rolex_client_SORUCES ""  "./benchs/Rolex/rolex_client.cc"  "./deps/r2/src/logging.cc"  "./deps/r2/src/sshed.cc"  "./benchs/terminate.cc" )
add_executable(rolex_client ${rolex_client_SORUCES} )
target_link_libraries(rolex_client gflags ibverbs pthread boost_system boost_coroutine)




