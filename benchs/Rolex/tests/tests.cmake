
file(GLOB TSOURCES  "benchs/Rolex/tests/*.cc")
file(GLOB coretest_SORUCES "" "deps/r2/src/logging.cc" "benchs/Rolex/tests/*.cc")
add_executable(coretest ${coretest_SORUCES} ${TSOURCES} )

target_link_libraries(coretest gtest_main gtest gflags ibverbs pthread boost_system boost_coroutine)

