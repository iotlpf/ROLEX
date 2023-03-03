

add_executable(bench_client ./benchs/bench_client.cc)
add_executable(bench_server ./benchs/bench_server.cc)
add_executable(fly_client ./benchs/fly_bench/client.cc)
add_executable(or_client ./benchs/or_bench/client.cc)
add_executable(db_client ./benchs/db_bench/client.cc)


set(benchs
bench_client bench_server
fly_client or_client db_client)

foreach(b ${benchs})
 target_link_libraries(${b} pthread ibverbs gflags)
endforeach(b)