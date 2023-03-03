// XD: change this file name to bench_client.cc ?

#include <gflags/gflags.h>

#include <vector>

#include "../core/lib.hh"
#include "../tests/random.hh"
#include "./reporter.hh"
#include "./thread.hh"
#include "./bench_op.hh"

using namespace rdmaio;  // warning: should not use it in a global space often
using namespace rdmaio::qp;
using namespace rdmaio::rmem;

using Thread_t = bench::Thread<usize>;

DEFINE_string(addr, "val09:8888", "Server address to connect to.");
DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(payload, 1024, "Payload of each req");
DEFINE_string(client_name, "localhost", "Unique name to identify machine.");
DEFINE_int64(op_type, 0, "RDMA_READ(0) RDMA_WRITE(1) ATOMIC_CAS(2) ATOMIC_FAA(3)");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");

usize worker_fn(const usize &worker_id, Statics *s);

bool volatile running = true;

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::vector<Thread_t *> workers;
  std::vector<Statics> worker_statics(FLAGS_threads);

  for (uint i = 0; i < FLAGS_threads; ++i) {
    workers.push_back(
        new Thread_t(std::bind(worker_fn, i, &(worker_statics[i]))));
  }

  // start the workers
  for (auto w : workers) {
    w->start();
  }

  //Reporter::report_thpt(worker_statics, 10);  // report for 10 seconds
  Reporter::report_bandwidth(worker_statics, 10, FLAGS_payload);  // report for 10 seconds
  running = false;                            // stop workers

  // wait for workers to join
  for (auto w : workers) {
    w->join();
  }

  RDMA_LOG(4) << "done";
}

usize worker_fn(const usize &worker_id, Statics *s) {
  Statics &ss = *s;

  // 1. create a local QP to use
  // FIXME: hard coded the nic selection to worker_id % 2
  auto nic =
    RNic::create(RNicInfo::query_dev_names().at(worker_id % 2)).value();
  auto qp = RC::create(nic, QPConfig()).value();

  // 2. create the pair QP at server using CM
  ConnectManager cm(FLAGS_addr);
  if (cm.wait_ready(1000000, 2) ==
      IOCode::Timeout)  // wait 1 second for server to ready, retry 2 times
    RDMA_ASSERT(false) << "cm connect to server timeout";

  // FIXME: hard coded the remote nic selection to worker_id % 2
  auto qp_res = cm.cc_rc(FLAGS_client_name + " thread-qp" + std::to_string(worker_id), qp,
                         worker_id % 2, QPConfig());
  RDMA_ASSERT(qp_res == IOCode::Ok) << std::get<0>(qp_res.desc);

  auto key = std::get<1>(qp_res.desc);
  RDMA_LOG(4) << "t-" << worker_id << " fetch QP authentical key: " << key;

  auto local_mem = Arc<RMem>(new RMem(1024 * 1024 * 20));  // 20M
  auto local_mr = RegHandler::create(local_mem, nic).value();

  auto fetch_res = cm.fetch_remote_mr(worker_id % 2);
  RDMA_ASSERT(fetch_res == IOCode::Ok) << std::get<0>(fetch_res.desc);
  rmem::RegAttr remote_attr = std::get<1>(fetch_res.desc);

  qp->bind_remote_mr(remote_attr);
  qp->bind_local_mr(local_mr->get_reg_attr().value());

  RDMA_LOG(4) << "t-" << worker_id << " started";
  u64 *test_buf = (u64 *)(qp->local_mr.value().buf);
  *test_buf = 0;
  u64 *remote_buf = (u64 *)remote_attr.buf;

  BenchOp<1> op(FLAGS_op_type);
  op.init_lbuf(test_buf, FLAGS_payload, qp->local_mr.value().key, 1000);
  op.init_rbuf(remote_buf, remote_attr.key, 10000);
  RDMA_ASSERT(op.valid());

  while (running) {
    compile_fence();
    op.refresh();
    auto res_s = op.execute(qp, IBV_SEND_SIGNALED);
    RDMA_ASSERT(res_s == IOCode::Ok);
    auto res_p = qp->wait_one_comp();
    RDMA_ASSERT(res_p == IOCode::Ok);
    ss.increment();  // finish one request
  }
  RDMA_LOG(4) << "t-" << worker_id << " stoped";
  cm.delete_remote_rc(FLAGS_client_name + " thread-qp" + std::to_string(worker_id), key);
  return 0;
}
