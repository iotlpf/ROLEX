#pragma once

#include <vector>

#include "../core/utils/timer.hh"

#include "./statics.hh"

namespace rdmaio {

struct Reporter {
  static double report_thpt(std::vector<Statics> &statics, int epoches) {

    std::vector<Statics> old_statics(statics.size());

    Timer timer;
    for (int epoch = 0; epoch < epoches; epoch += 1) {
      sleep(1);

      u64 sum = 0;
      // now report the throughput
      for (uint i = 0; i < statics.size(); ++i) {
        auto temp = statics[i].data.counter;
        sum += (temp - old_statics[i].data.counter);
        old_statics[i].data.counter = temp;
      }

      double passed_msec = timer.passed_msec();
      double res = static_cast<double>(sum) / passed_msec * 1000000.0;
      ::rdmaio::compile_fence();
      timer.reset();

      RDMA_LOG(2) << "epoch @ " << epoch << ": thpt: " << res << " reqs/sec."
                  << passed_msec << " msec passed since last epoch.";
    }
    return 0.0;
  }

  static double report_bandwidth(std::vector<Statics> &statics, int epoches, const usize &payload_per_req) {

    std::vector<Statics> old_statics(statics.size());

    Timer timer;
    for (int epoch = 0; epoch < epoches; epoch += 1) {
      sleep(1);

      u64 sum = 0;
      // now report the throughput
      for (uint i = 0; i < statics.size(); ++i) {
        auto temp = statics[i].data.counter;
        sum += (temp - old_statics[i].data.counter);
        old_statics[i].data.counter = temp;
      }

      double passed_msec = timer.passed_msec();
      double res = static_cast<double>(sum) / passed_msec * 1000000.0;

      double bw = res * payload_per_req;
      ::rdmaio::compile_fence();
      timer.reset();

      RDMA_LOG(2) << "epoch @ " << epoch << ": thpt: " << res << " reqs/sec, "
                  << "bandwidht: " << bw / (1024L * 1024 * 1024) * 8 << " Gbps;"
                  << passed_msec << " msec passed since last epoch.";
    }
    return 0.0;
  }
};

} // namespace rdmaio
