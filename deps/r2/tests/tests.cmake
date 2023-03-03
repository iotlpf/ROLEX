
## main test files
file(GLOB TSOURCES  "tests/test_srop.cc"  "tests/test_ud_session.cc" "tests/test_rc_session.cc" "tests/test_rm.cc")
add_executable(coretest ${TSOURCES} )

target_link_libraries(coretest gtest gtest_main rocksdb boost_serialization jemalloc ibverbs boost_coroutine boost_chrono boost_thread boost_context boost_system r2 pthread)

## test file when there is no RDMA, allow local debug
file(GLOB T_WO_SOURCES  "tests/test_list.cc" "tests/test_rdtsc.cc" "tests/test_ssched.cc" )
add_executable(coretest_wo_rdma ${T_WO_SOURCES} "src/logging.cc")
target_link_libraries(coretest_wo_rdma gtest gtest_main boost_context boost_system boost_coroutine boost_thread boost_chrono r2 pthread)
