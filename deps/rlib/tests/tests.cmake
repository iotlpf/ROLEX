

file(GLOB TSOURCES  "tests/*.cc" )
add_executable(coretest ${TSOURCES} )

target_link_libraries(coretest gtest gtest_main ibverbs pthread)
