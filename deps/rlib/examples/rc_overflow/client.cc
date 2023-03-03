#include <assert.h>
#include <gflags/gflags.h>

#include "../../core/lib.hh"
#include "../../core/qps/mod.hh"
#include "../../core/utils/timer.hh"

using namespace rdmaio;
using namespace rdmaio::qp;
using namespace rdmaio::rmem;

DEFINE_string(addr, "localhost:8888", "Server address to connect to.");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_string(cq_name, "test_channel", "The name to register an receive cq");
DEFINE_int64(msg_cnt, 10000, "The count of sending message");

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto nic =
      RNic::create(RNicInfo::query_dev_names().at(FLAGS_use_nic_idx)).value();

  // 1. create the local QP to send
  auto qp = RC::create(nic, QPConfig()).value();

  ConnectManager cm(FLAGS_addr);
  if (cm.wait_ready(100000, 4) ==
      IOCode::Timeout)  // wait 1 second for server to ready, retry 2 times
    RDMA_LOG(4) << "connect to the " << FLAGS_addr << " timeout!";

  sleep(1);
  // 2. create the remote QP and connect
  auto qp_res = cm.cc_rc_msg("client_qp", FLAGS_cq_name, 4096, qp,
                             FLAGS_use_nic_idx, QPConfig());
  RDMA_ASSERT(qp_res == IOCode::Ok) << std::get<0>(qp_res.desc);

  // 3. fetch the remote MR for usage
  auto fetch_res = cm.fetch_remote_mr(FLAGS_reg_mem_name);
  RDMA_ASSERT(fetch_res == IOCode::Ok) << std::get<0>(fetch_res.desc);
  rmem::RegAttr remote_attr = std::get<1>(fetch_res.desc);

  // 4. register a local buffer for sending messages
  auto local_mr = RegHandler::create(Arc<RMem>(new RMem(1024 * 1024)), nic).value();
  char *buf = (char *)(local_mr->get_reg_attr().value().buf);

  qp->bind_remote_mr(remote_attr);
  qp->bind_local_mr(local_mr->get_reg_attr().value());

  RDMA_LOG(2) << "rc client ready to send message to the server!";

  Timer timer;
  // 5. send msg
  for (int i = 1; i <= FLAGS_msg_cnt; ++i) {
    std::string msg = std::to_string(i);
    memset(buf, 0, msg.size() + 1);
    memcpy(buf, msg.data(), msg.size());

    auto res_s = qp->send_normal(
        {.op = IBV_WR_SEND_WITH_IMM,
         .flags = IBV_SEND_SIGNALED,
         .len = (u32) msg.size() + 1, 
         .wr_id = 0},
        {.local_addr = reinterpret_cast<RMem::raw_ptr_t>(buf),
         .remote_addr = 0,
         .imm_data = 0});

    RDMA_ASSERT(res_s == IOCode::Ok);
    auto res_p = qp->wait_one_comp();
    RDMA_ASSERT(res_p == IOCode::Ok);
  }
  double passed_msec = timer.passed_msec();
  RDMA_LOG(2) << "rc client send " << FLAGS_msg_cnt << " msg in " << passed_msec
              << " msec";
  return 0;
}
