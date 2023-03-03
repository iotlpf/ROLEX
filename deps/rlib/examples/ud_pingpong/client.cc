#include <gflags/gflags.h>

#include <assert.h>

#include "../../core/lib.hh"
#include "../../core/qps/mod.hh"

using namespace rdmaio;
using namespace rdmaio::qp;
using namespace rdmaio::rmem;

DEFINE_string(addr, "localhost:8888", "Server address to connect to.");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto nic =
      RNic::create(RNicInfo::query_dev_names().at(FLAGS_use_nic_idx)).value();

  // 1. create the local QP to send
  auto ud = UD::create(nic, QPConfig().set_qkey(73)).value();

  ConnectManager cm(FLAGS_addr);
  if (cm.wait_ready(100000, 4) ==
      IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
    RDMA_LOG(4) << "connect to the " << FLAGS_addr << " timeout!";

  // 2. fetch the remote QP attribute
  auto fetch_qp_attr_res = cm.fetch_qp_attr("server_ud");
  RDMA_ASSERT(fetch_qp_attr_res == IOCode::Ok)
      << "fetch qp attr error: " << fetch_qp_attr_res.code.name() << " "
      << std::get<0>(fetch_qp_attr_res.desc);

  // 3. register a local buffer for sending messages
  auto mr = RegHandler::create(Arc<RMem>(new RMem(4000)), nic)
                .value(); // UD can send at most 4000 bytes
  char *buf = (char *)(mr->get_reg_attr().value().buf);

  // 4. prepare the sender structure
  ibv_send_wr wr;
  ibv_sge sge;

  wr.opcode = IBV_WR_SEND_WITH_IMM;
  wr.num_sge = 1;
  wr.imm_data = 73;
  wr.next = nullptr;
  wr.sg_list = &sge;

  wr.wr.ud.ah = ud->create_ah(std::get<1>(fetch_qp_attr_res.desc));
  wr.wr.ud.remote_qpn = std::get<1>(fetch_qp_attr_res.desc).qpn;
  wr.wr.ud.remote_qkey = std::get<1>(fetch_qp_attr_res.desc).qkey;
  wr.send_flags = IBV_SEND_SIGNALED;

  sge.addr = (uintptr_t)(buf);

  RDMA_LOG(2) << "client ready to send pingpong message to the server!";
  RDMA_LOG(2) << "try type anything~";

  // 5. loop the inputs, and send to remote
  while (true) {
    std::string msg;
    std::getline(std::cin, msg);
    if (msg.size() > 2048)
      RDMA_LOG(2) << "message too large, please retry";

    memset(buf,0,msg.size() + 1);
    memcpy(buf,msg.data(),msg.size());
    sge.length = msg.size() + 1;
    sge.lkey = mr->get_reg_attr().value().key;

    struct ibv_send_wr *bad_sr = nullptr;
    RDMA_ASSERT(ibv_post_send(ud->qp, &wr, &bad_sr) == 0);

    // wait one completion
    auto ret_r = ud->wait_one_comp();
    RDMA_ASSERT(ret_r == IOCode::Ok) << UD::wc_status(ret_r.desc);
  }

  return 0;
}
