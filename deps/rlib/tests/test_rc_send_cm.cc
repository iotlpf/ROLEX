#include <gtest/gtest.h>

#include "../core/nicinfo.hh"

#include "../core/qps/mod.hh"

#include "../core/utils/marshal.hh"

#include "../core/lib.hh"

namespace test {

using namespace rdmaio;
using namespace rdmaio::qp;
using namespace rdmaio::rmem;

class SimpleAllocator : public AbsRecvAllocator {
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

TEST(RCCM, SR) {
#if 0
  const usize recv_depth = 128;

  RCtrl ctrl(8899);
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

  auto alloc = std::make_shared<SimpleAllocator>(
      mem, handler->get_reg_attr().value().key);

  manager.reg_recv_cqs.create_then_reg("test_channel", recv_cq, alloc);

  // 2. create the sender QP using the CM
  auto qp = RC::create(nic, QPConfig()).value();
  qp->bind_remote_mr(mr);
  qp->bind_local_mr(mr);

  ctrl.start_daemon();
  // 3. use the CM to connect for this QP
  ConnectManager cm("localhost:8899");
  if (cm.wait_ready(1000000, 2) ==
      IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
    assert(false);

  auto qp_res =
      cm.cc_rc_msg("client_qp", "test_channel", 4096, qp, 0, QPConfig());
  RDMA_ASSERT(qp_res == IOCode::Ok) << std::get<0>(qp_res.desc);

  {
    // main test body
    for (uint i = 0; i < recv_depth; ++i) {
      auto msg = ::rdmaio::Marshal::dump<u64>(i);
      auto res_s =
          qp->send_normal({.op = IBV_WR_SEND_WITH_IMM,
                           .flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE,
                           .len = sizeof(u64),
                           .wr_id = 0},
                          {.local_addr = (RMem::raw_ptr_t)(msg.data()),
                           .remote_addr = 0,
                           .imm_data = 73 + i});
      RDMA_ASSERT(res_s == IOCode::Ok);
      auto res_p = qp->wait_one_comp();
      RDMA_ASSERT(res_p == IOCode::Ok);
    }

    sleep(1);


    // 1. fetch the QP for recv
    auto recv_qp = ctrl.registered_qps.query("client_qp").value();
    auto recv_rs = manager.reg_recv_entries.query("client_qp").value();

    // test to ensure that we recv enough msgs
    usize recved_msgs = 0;
    for (RecvIter<Dummy, recv_depth> iter(recv_qp, recv_rs); iter.has_msgs();
         iter.next()) {
      // check message content
      auto imm_msg = iter.cur_msg().value();

      auto buf = static_cast<char *>(std::get<1>(imm_msg));
      auto res = ::rdmaio::Marshal::dedump<u64>(std::string(buf, 128)).value();

      ASSERT_EQ(std::get<0>(imm_msg), 73 + recved_msgs);
      ASSERT_EQ(res, recved_msgs);

      recved_msgs += 1;
    }
    ASSERT_EQ(recved_msgs, recv_depth);
  }
#endif
}

} // namespace test
