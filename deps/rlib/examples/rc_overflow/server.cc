#include <gflags/gflags.h>

#include "../../core/lib.hh"
#include "../../core/qps/rc_recv_manager.hh"
#include "../../core/qps/recv_iter.hh"

DEFINE_int64(port, 8888, "Server listener (RC) port.");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_string(cq_name, "test_channel", "The name to register an receive cq");
DEFINE_int64(msg_cnt, 10000, "The count of sending message");

using namespace rdmaio;
using namespace rdmaio::rmem;
using namespace rdmaio::qp;

class SimpleAllocator : public AbsRecvAllocator {
  RMem::raw_ptr_t buf = nullptr;
  usize total_mem = 0;
  mr_key_t key;

 public:
  SimpleAllocator(Arc<RMem> mem, mr_key_t key)
      : buf(mem->raw_ptr), total_mem(mem->sz), key(key) {
    // RDMA_LOG(4) << "simple allocator use key: " << key;
  }

  Option<std::pair<rmem::RMem::raw_ptr_t, rmem::mr_key_t>> alloc_one(
      const usize &sz) override {
    if (total_mem < sz) return {};
    auto ret = buf;
    buf = static_cast<char *>(buf) + sz;
    total_mem -= sz;
    return std::make_pair(ret, key);
  }

  Option<std::pair<rmem::RMem::raw_ptr_t, rmem::RegAttr>> alloc_one_for_remote(
      const usize &sz) override {
    return {};
  }
};

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  const usize entry_num = 128;

  RCtrl ctrl(FLAGS_port);
  RecvManager<entry_num, 2048> manager(ctrl);
  auto nic =
      RNic::create(RNicInfo::query_dev_names().at(FLAGS_use_nic_idx)).value();
  RDMA_ASSERT(ctrl.opened_nics.reg(0, nic));
  RDMA_LOG(4) << "(RC) Pingping server listenes at localhost:" << FLAGS_port;

  // 1. create receive cq
  auto recv_cq_res = ::rdmaio::qp::Impl::create_cq(nic, entry_num);
  RDMA_ASSERT(recv_cq_res == IOCode::Ok);
  auto recv_cq = std::get<0>(recv_cq_res.desc);

  // 2. prepare the message buffer with allocator
  auto mem =
      Arc<RMem>(new RMem(16 * 1024 * 1024));  // allocate a memory with 4M bytes
  auto handler = RegHandler::create(mem, nic).value();
  auto alloc = std::make_shared<SimpleAllocator>(
      mem, handler->get_reg_attr().value().key);

  // 3. register receive cq
  manager.reg_recv_cqs.create_then_reg(FLAGS_cq_name, recv_cq, alloc);
  RDMA_LOG(4) << "Register test_channel";
  ctrl.registered_mrs.reg(FLAGS_reg_mem_name, handler);

  ctrl.start_daemon();
  sleep(3); // wait client to create this side qp.

  auto recv_qp = ctrl.registered_qps.query("client_qp").value();
  auto recv_rs = manager.reg_recv_entries.query("client_qp").value();
  u64 recv_cnt = 0;

  // receive all msgs.
  while (1) {
    for (RecvIter<Dummy, entry_num> iter(recv_qp, recv_rs); iter.has_msgs();
         iter.next()) {
      auto imm_msg = iter.cur_msg().value();

      auto buf = static_cast<char *>(std::get<1>(imm_msg));
      const std::string msg(buf, 2048);  // wrap the received msg
      
      u64 seq = std::atoi(msg.c_str());
      recv_cnt++;
      if (seq >= FLAGS_msg_cnt) {
        RDMA_LOG(4) << "rc server receive " << recv_cnt << " msg, all "
                    << FLAGS_msg_cnt << " msg";
        return 0;
      }
    }
    
  }
  return 0;
}
