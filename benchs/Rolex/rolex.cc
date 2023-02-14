#include <gflags/gflags.h>
#include <iostream>

#include "rlib/core/nicinfo.hh"      /// RNicInfo
#include "rlib/core/rctrl.hh"        /// RCtrl
#include "rlib/core/common.hh"       /// LOG


#include "rolex/huge_region.hh"
#include "rolex/trait.hpp"
#include "rolex/model_allocator.hpp"
#include "rolex/remote_memory.hh"
#include "../load_data.hh"



DEFINE_int64(port, 8888, "Server listener (UDP) port.");
DEFINE_uint64(leaf_num, 10000000, "The number of registed leaves.");
// DEFINE_uint64(reg_leaf_region, 101, "The name to register an MR at rctrl for data nodes.");



using namespace rdmaio;
using namespace rolex;


volatile bool running = false;
std::atomic<size_t> ready_threads(0);
RCtrl* ctrl;
rolex_t *rolex_index;

void prepare();
void run_benchmark(size_t sec);
void *run_fg(void *param);

int main(int argc, char** argv)
{ 
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  bench::load_benchmark_config();

  prepare();
  // rolex_index->print_data();
  //
  run_benchmark(5);
  // sleep(10);

  return 0;
}

void prepare() {
  const usize MB = 1024 * 1024;
  ctrl = new RCtrl(FLAGS_port);
  RM_config conf(ctrl, 1024 * MB, FLAGS_leaf_num*sizeof(leaf_t), FLAGS_reg_leaf_region, FLAGS_leaf_num);
  remote_memory_t* RM = new remote_memory_t(conf);

  load_data();
  LOG(2) << "[processing data]";
  std::sort(exist_keys.begin(), exist_keys.end());
  exist_keys.erase(std::unique(exist_keys.begin(), exist_keys.end()), exist_keys.end());
  std::sort(exist_keys.begin(), exist_keys.end());
  for(size_t i=1; i<exist_keys.size(); i++){
      assert(exist_keys[i]>=exist_keys[i-1]);
  }
  rolex_index = new rolex_t(RM, exist_keys, exist_keys);
  // rolex_index->print_data();

  RDMA_LOG(2) << "Data distribution bench server started!";
  RM->start_daemon();
}



void run_benchmark(size_t sec) {
    pthread_t threads[BenConfig.threads];
    thread_param_t thread_params[BenConfig.threads];
    // check if parameters are cacheline aligned
    for (size_t i = 0; i < BenConfig.threads; i++) {
        ASSERT ((uint64_t)(&(thread_params[i])) % CACHELINE_SIZE == 0) <<
            "wrong parameter address: " << &(thread_params[i]);
    }

    running = false;
    for(size_t worker_i = 0; worker_i < BenConfig.threads; worker_i++){
        thread_params[worker_i].thread_id = worker_i;
        thread_params[worker_i].throughput = 0;
        int ret = pthread_create(&threads[worker_i], nullptr, run_fg,
                                (void *)&thread_params[worker_i]);
        ASSERT (ret==0) <<"Error:" << ret;
    }

    LOG(3)<<"[micro] prepare data ...";
    while (ready_threads < BenConfig.threads) sleep(0.5);

    running = true;
    std::vector<size_t> tput_history(BenConfig.threads, 0);
    size_t current_sec = 0;
    while (current_sec < sec) {
        sleep(1);
        uint64_t tput = 0;
        for (size_t i = 0; i < BenConfig.threads; i++) {
            tput += thread_params[i].throughput - tput_history[i];
            tput_history[i] = thread_params[i].throughput;
        }
        LOG(2)<<"[micro] >>> sec " << current_sec << " throughput: " << tput;
        ++current_sec;
    }

    running = false;
    void *status;
    for (size_t i = 0; i < BenConfig.threads; i++) {
        int rc = pthread_join(threads[i], &status);
        ASSERT (!rc) "Error:unable to join," << rc;
    }

    size_t throughput = 0;
    for (auto &p : thread_params) {
        throughput += p.throughput;
    }
    LOG(2)<<"[micro] Throughput(op/s): " << throughput / sec;
}

void *run_fg(void *param) {
    thread_param_t &thread_param = *(thread_param_t *)param;
    uint32_t thread_id = thread_param.thread_id;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> ratio_dis(0, 1);

    size_t non_exist_key_n_per_thread = nonexist_keys.size() / BenConfig.threads;
    size_t non_exist_key_start = thread_id * non_exist_key_n_per_thread;
    size_t non_exist_key_end = (thread_id + 1) * non_exist_key_n_per_thread;
    std::vector<K> op_keys(nonexist_keys.begin() + non_exist_key_start,
                                   nonexist_keys.begin() + non_exist_key_end);

    LOG(2) << "[micro] Worker" << thread_id << " Ready.";
    size_t query_i = 0, insert_i = 0, delete_i = 0, update_i = 0;
    // exsiting keys fall within range [delete_i, insert_i)
    ready_threads++;
    V dummy_value = 1234;

    while (!running)
        ;
	while (running) {
        double d = ratio_dis(gen);
        if (d <= BenConfig.read_ratio) {                   // search
            K dummy_key = exist_keys[query_i % exist_keys.size()];
            rolex_index->search(dummy_key, dummy_value);
            query_i++;
            if (unlikely(query_i == exist_keys.size())) {
                query_i = 0;
            }
        } else if (d <= BenConfig.read_ratio+BenConfig.insert_ratio){  // insert
            K dummy_key = nonexist_keys[insert_i % nonexist_keys.size()];
            rolex_index->insert(dummy_key, dummy_key);
            insert_i++;
            if (unlikely(insert_i == nonexist_keys.size())) {
                insert_i = 0;
            }
        } else if (d <= BenConfig.read_ratio+BenConfig.insert_ratio+BenConfig.update_ratio) {    // update
            K dummy_key = nonexist_keys[update_i % nonexist_keys.size()];
            rolex_index->update(dummy_key, dummy_key);
            update_i++;
            if (unlikely(update_i == nonexist_keys.size())) {
                update_i = 0;
            }
        }  else {                // remove
            K dummy_key = exist_keys[delete_i % exist_keys.size()];
            rolex_index->remove(dummy_key);
            delete_i++;
            if (unlikely(delete_i == exist_keys.size())) {
                delete_i = 0;
            }
        }
        thread_param.throughput++;
    }
    pthread_exit(nullptr);
}