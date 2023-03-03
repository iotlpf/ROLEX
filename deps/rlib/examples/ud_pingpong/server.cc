#include <gflags/gflags.h>

#include "../../core/lib.hh"
#include "../../core/qps/recv_iter.hh"

DEFINE_int64(port, 8888, "Server listener (UDP) port.");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");

using namespace rdmaio;
using namespace rdmaio::rmem;
using namespace rdmaio::qp;

class SimpleAllocator : AbsRecvAllocator {
  RMem::raw_ptr_t buf = nullptr;
  usize total_mem = 0;
  mr_key_t key;

public:
  SimpleAllocator(Arc<RMem> mem, mr_key_t key)
      : buf(mem->raw_ptr), total_mem(mem->sz), key(key) {
    // RDMA_LOG(4) << "simple allocator use key: " << key;
  }

  Option<std::pair<rmem::RMem::raw_ptr_t, rmem::mr_key_t>>
  alloc_one(const usize &sz) override {
    if (total_mem < sz)
      return {};
    auto ret = buf;
    buf = static_cast<char *>(buf) + sz;
    total_mem -= sz;
    return std::make_pair(ret, key);
  }

  Option<std::pair<rmem::RMem::raw_ptr_t, rmem::RegAttr>>
  alloc_one_for_remote(const usize &sz) override {
    return {};
  }
};

int main(int argc, char **argv) {

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  RCtrl ctrl(FLAGS_port);
  RDMA_LOG(4) << "(UD) Pingping server listenes at localhost:" << FLAGS_port;

  // first we open the NIC
  auto nic =
      RNic::create(RNicInfo::query_dev_names().at(FLAGS_use_nic_idx)).value();

  // prepare the message buf
  auto mem =
      Arc<RMem>(new RMem(16 * 1024 * 1024)); // allocate a memory with 4M bytes

  auto handler = RegHandler::create(mem, nic).value();
  SimpleAllocator alloc(mem, handler->get_reg_attr().value().key);

  // prepare buffer, contain 16 recv entries, each has 4096 bytes
  auto recv_rs = RecvEntriesFactory<SimpleAllocator, 1024, 4096>::create(alloc);
  ctrl.registered_mrs.reg(FLAGS_reg_mem_name, handler);

  auto ud = UD::create(nic, QPConfig()).value();
  ctrl.registered_qps.reg("server_ud", ud);

  // post these recvs to the UD
  {
    recv_rs->sanity_check();
    auto res = ud->post_recvs(*recv_rs, 1024);
    RDMA_ASSERT(res == IOCode::Ok);
  }

  ctrl.start_daemon();

  while (1) {
    for (RecvIter<UD, 1024> iter(ud, recv_rs); iter.has_msgs(); iter.next()) {
      auto imm_msg = iter.cur_msg().value();

      auto buf = static_cast<char *>(std::get<1>(imm_msg)) + kGRHSz;
      const std::string msg(buf, 4096); // wrap the received msg
      RDMA_LOG(2) << "server received one msg: " << msg;
    }
    sleep(1);
  }
  return 0;
}
