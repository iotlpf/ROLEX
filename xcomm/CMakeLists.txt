cmake_minimum_required(VERSION 3.2)
project(xxx)
ADD_DEFINITIONS(-std=c++17)

include_directories("src")
include_directories("deps")
include_directories("../deps/")
include_directories("/opt/intel/oneapi/mkl/2021.1.1/include")
set(CMAKE_CXX_FLAGS "-O2 -g -mrtm")
file(GLOB bench_local_SORUCES ""  "./benchs/main.cc"  "../deps/r2/src/logging.cc" )
add_executable(bench_local ${bench_local_SORUCES} )
target_link_libraries(bench_local boost_system boost_coroutine pthread )
file(GLOB bench_ud_SORUCES ""  "./benchs/bench_ud.cc"  "../deps/r2/src/logging.cc" )
add_executable(bench_ud ${bench_ud_SORUCES} )
target_link_libraries(bench_ud boost_system boost_coroutine pthread )
file(GLOB rpc_server_SORUCES ""  "./benchs/rpc_server.cc"  "../deps/r2/src/logging.cc" )
add_executable(rpc_server ${rpc_server_SORUCES} )
target_link_libraries(rpc_server boost_system boost_coroutine pthread gflags ibverbs )
file(GLOB rpc_client_SORUCES ""  "./benchs/rpc_client.cc"  "../deps/r2/src/logging.cc"  "../deps/r2/src/sshed.cc" )
add_executable(rpc_client ${rpc_client_SORUCES} )
target_link_libraries(rpc_client boost_system boost_coroutine pthread gflags ibverbs )

#[[
file(GLOB test_rw_SORUCES ""  "./tests/test_local_rw.cc"  "../deps/r2/src/logging.cc" )
add_executable(test_rw ${test_rw_SORUCES} )
target_link_libraries(test_rw gtest )
]]

file(GLOB test_transport_SORUCES ""  "./tests/test_transport.cc"  "../deps/r2/src/logging.cc" )
add_executable(test_transport ${test_transport_SORUCES} )
target_link_libraries(test_transport gtest ibverbs pthread)
file(GLOB test_ud_SORUCES ""  "./tests/test_ud_transport.cc"  "../deps/r2/src/logging.cc" )
add_executable(test_ud ${test_ud_SORUCES} )
target_link_libraries(test_ud gtest ibverbs boost_system pthread)
file(GLOB test_rpc_SORUCES ""  "./tests/test_rpc.cc"  "../deps/r2/src/logging.cc" )
add_executable(test_rpc ${test_rpc_SORUCES} )
target_link_libraries(test_rpc gtest ibverbs boost_system pthread)
file(GLOB test_remote_rw_SORUCES ""  "./tests/test_remote_rw.cc"  "../deps/r2/src/logging.cc"  "../deps/r2/src/sshed.cc" )
add_executable(test_remote_rw ${test_remote_rw_SORUCES} )
target_link_libraries(test_remote_rw gtest ibverbs boost_system boost_coroutine pthread )
file(GLOB test_async_local_SORUCES ""  "./tests/test_async_local.cc"  "../deps/r2/src/logging.cc"  "../deps/r2/src/sshed.cc" )
add_executable(test_async_local ${test_async_local_SORUCES} )
target_link_libraries(test_async_local gtest boost_system boost_coroutine pthread)
file(GLOB test_batch_SORUCES ""  "./tests/test_batch_rdma.cc"  "../deps/r2/src/logging.cc"  "../deps/r2/src/sshed.cc" )
add_executable(test_batch ${test_batch_SORUCES} )
target_link_libraries(test_batch gtest ibverbs boost_system boost_coroutine pthread )
file(GLOB test_remote_con_rw_SORUCES ""  "./tests/test_remote_concurrent.cc"  "../deps/r2/src/logging.cc" )
add_executable(test_remote_con_rw ${test_remote_con_rw_SORUCES} )
target_link_libraries(test_remote_con_rw gtest ibverbs boost_system boost_coroutine pthread )
file(GLOB test_con_rw_SORUCES ""  "./tests/test_local_concurrent.cc"  "../deps/r2/src/logging.cc" )
add_executable(test_con_rw ${test_con_rw_SORUCES} )
target_link_libraries(test_con_rw gtest pthread )
