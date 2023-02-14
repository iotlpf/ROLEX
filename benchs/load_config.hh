#pragma once
#include <gflags/gflags.h>
#include <vector>
#include <map>

#include "statics.hh"

namespace rolex {

namespace bench {


DEFINE_uint64(reg_leaf_region, 101, "The name to register an MR at rctrl for data nodes.");
// load datasets
DEFINE_uint64(nkeys, 100000, "Number of keys to load");
DEFINE_uint64(non_nkeys, 100000, "Number of non_keys for inserting");
DEFINE_string(workloads, "normal", "The workloads for evaluation");
// test config
DEFINE_uint64(mem_threads, 3, "Server threads.");
DEFINE_uint64(threads, 24, "Server threads.");
DEFINE_int32(coros, 10, "num client coroutine used per threads");
DEFINE_double(read_ratio, 1, "The ratio for reading");
DEFINE_double(insert_ratio, 0, "The ratio for writing");
DEFINE_double(update_ratio, 0, "The ratio for updating");


enum WORKLOAD{
  YCSB_A, YCSB_B, YCSB_C, YCSB_D, YCSB_E, YCSB_F, NORMAL, LOGNORMAL, WEBLOG, DOCID
};

struct BenchmarkConfig {
  u64 nkeys;
  u64 non_nkeys;
  i32 workloads;

  u64 mem_threads;
  u64 threads;
  i32 coros;
  double read_ratio;
  double insert_ratio;
  double update_ratio;
  std::vector<Statics> statics;
}BenConfig;


void load_benchmark_config() {
  BenConfig.nkeys     = FLAGS_nkeys;
  BenConfig.non_nkeys = FLAGS_non_nkeys;
  std::map<std::string, i32> workloads_map = {
    { "ycsba", YCSB_A },
    { "ycsbb", YCSB_B },
    { "ycsbc", YCSB_C },
    { "ycsbd", YCSB_D },
    { "ycsbe", YCSB_E },
    { "ycsbf", YCSB_F },
    { "normal", NORMAL },
    { "lognormal", LOGNORMAL },
    { "weblog", WEBLOG },
    { "docid", DOCID }
  };
  ASSERT (workloads_map.find(FLAGS_workloads) != workloads_map.end()) 
    << "unsupported workload type: " << FLAGS_workloads;
  BenConfig.workloads = workloads_map[FLAGS_workloads];
  
  BenConfig.mem_threads   = FLAGS_mem_threads;
  BenConfig.threads       = FLAGS_threads;
  BenConfig.coros         = FLAGS_coros;
  BenConfig.read_ratio    = FLAGS_read_ratio;
  BenConfig.insert_ratio  = FLAGS_insert_ratio;
  BenConfig.update_ratio  = FLAGS_update_ratio;

  BenConfig.statics.reserve(FLAGS_threads);
}


} // namespace bench

} // namespace rolex