#include <gtest/gtest.h>

#include "rlib/core/nicinfo.hh"
#include "rlib/core/qps/mod.hh"
#include "rlib/core/qps/recv_iter.hh"
#include "rlib/core/utils/marshal.hh"

#include "rlib/core/lib.hh"
#include "rlib/core/rctrl.hh"

#include "rlib/core/qps/rc_recv_manager.hh"

#include "../src/msg/rc_session.hh"

namespace test {

using namespace rdmaio;
using namespace rdmaio::qp;
using namespace rdmaio::rmem;

using namespace r2;

class SimpleAllocator : public AbsRecvAllocator {
  RMem::raw_ptr_t buf = nullptr;
  usize total_mem = 0;
  mr_key_t key;

public:
  SimpleAllocator(Arc<RMem> mem, mr_key_t key)
      : buf(mem->raw_ptr), total_mem(mem->sz), key(key) {
    // RDMA_LOG(4) << "simple allocator use key: " << key;
  }

  ::r2::Option<std::pair<rmem::RMem::raw_ptr_t, rmem::mr_key_t>>
  alloc_one(const usize &sz) override {
    if (total_mem < sz)
      return {};
    auto ret = buf;
    buf = static_cast<char *>(buf) + sz;
    total_mem -= sz;
    return std::make_pair(ret, key);
  }
  ::r2::Option<std::pair<rmem::RMem::raw_ptr_t, rmem::RegAttr>>
  alloc_one_for_remote(const usize &sz) {
    RDMA_ASSERT(false) << "not implemented";
    return {};
  }
};

TEST(RCS, Basic) {

  const usize recv_depth = 128;

  RCtrl ctrl(8889);
  RecvManager<recv_depth, 2048> manager(ctrl);

  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty());
  auto nic = RNic::create(res.at(0)).value();
  RDMA_ASSERT(ctrl.opened_nics.reg(0, nic));

  // 1. create recv commm data structure
  auto recv_cq_res = ::rdmaio::qp::Impl::create_cq(nic, recv_depth);
  RDMA_ASSERT(recv_cq_res == IOCode::Ok);
  auto recv_cq = std::get<0>(recv_cq_res.desc);

  auto mem =
      Arc<RMem>(new RMem(16 * 1024 * 1024)); // allocate a memory with 4M bytes
  ASSERT_TRUE(mem->valid());

  auto handler = RegHandler::create(mem, nic).value();
  auto mr = handler->get_reg_attr().value();

  Arc<AbsRecvAllocator> alloc = std::make_shared<SimpleAllocator>(
      mem, handler->get_reg_attr().value().key);

  manager.reg_recv_cqs.create_then_reg("test_channel", recv_cq, alloc);

  // 2. create the sender QP using the CM
  auto qp = RC::create(nic, QPConfig()).value();
  qp->bind_remote_mr(mr);
  qp->bind_local_mr(mr);

  ctrl.start_daemon();
  // 3. use the CM to connect for this QP
  ConnectManager cm("localhost:8889");
  if (cm.wait_ready(1000000, 2) ==
      IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
    assert(false);

  int id = 73;
  auto qp_res =
      cm.cc_rc_msg(std::to_string(id), "test_channel", 4096, qp, 0, QPConfig());
  RDMA_ASSERT(qp_res == IOCode::Ok) << std::get<0>(qp_res.desc);

  RCSession s(id, qp);

  {
    // send body
    for (uint i = 0; i < recv_depth; ++i) {
      auto msg = ::rdmaio::Marshal::dump<u64>(i);
      auto res_s = s.send_unsignaled(
          {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
      ASSERT(res_s == IOCode::Ok);
    }
  }

  sleep(1);

  {
    auto recv_rs = manager.reg_recv_entries.query(std::to_string(id)).value();
    auto recv_qp = std::dynamic_pointer_cast<RC>(
        ctrl.registered_qps.query(std::to_string(id)).value());

    RCRecvSession<recv_depth> rs(recv_qp,recv_rs);

    Arc<RC> dummy = nullptr;

    usize recved_msgs = 0;
    ibv_wc wcs[recv_depth];

    for (RecvIter<RC, recv_depth> iter(recv_cq,wcs); iter.has_msgs(); iter.next()) {
      auto imm_msg = iter.cur_msg().value();

      auto buf = static_cast<char *>(std::get<1>(imm_msg));
      auto res = ::rdmaio::Marshal::dedump<u64>(std::string(buf, 128)).value();

      ASSERT_EQ(res, recved_msgs);

      recved_msgs += 1;
    }
    ASSERT_EQ(recved_msgs, recv_depth);
  }
}

} // namespace test
