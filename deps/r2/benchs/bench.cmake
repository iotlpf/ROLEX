
set(benchs
bench_server co_client)

add_executable(co_client benchs/co_bench/client.cc)
add_executable(bench_server benchs/bench_server.cc)

foreach(b ${benchs})
  target_link_libraries(${b} pthread gflags boost_serialization jemalloc ibverbs boost_coroutine boost_chrono boost_thread boost_context boost_system r2)
endforeach(b)
