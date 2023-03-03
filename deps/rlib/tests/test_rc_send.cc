#include <gtest/gtest.h>

#include "../core/nicinfo.hh"

#include "../core/qps/mod.hh"

#include "../core/utils/marshal.hh"

namespace test {

using namespace rdmaio;
using namespace rdmaio::qp;
using namespace rdmaio::rmem;

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
};

TEST(RC, SR) {
  // this test is deprecated, check **test_rc_send_cm.cc**
#if 0
  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty());
  auto nic = RNic::create(res.at(0)).value();

  // 1. create the recv cq
  const usize recv_depth = 1024;
  auto recv_cq_res = ::rdmaio::qp::Impl::create_cq(nic, recv_depth);
  RDMA_ASSERT(recv_cq_res == IOCode::Ok);
  auto recv_cq = std::get<0>(recv_cq_res.desc);

  // 2. create the QP for send
  auto qp = RC::create(nic,QPConfig(),recv_cq).value();

  // 3. post recvs
  // prepare the message buf
  auto mem =
      Arc<RMem>(new RMem(16 * 1024 * 1024)); // allocate a memory with 4M bytes
  ASSERT_TRUE(mem->valid());

  auto handler = RegHandler::create(mem, nic).value();
  auto mr = handler->get_reg_attr().value();

  SimpleAllocator alloc(mem, handler->get_reg_attr().value().key);

  // prepare buffer, contain 16 recv entries, each has 4096 bytes
  auto recv_rs = RecvEntriesFactory<SimpleAllocator, recv_depth, 1000>::create(alloc);
  {
    recv_rs->sanity_check();
    auto res = qp->post_recvs(*recv_rs, recv_depth);
    RDMA_ASSERT(res == IOCode::Ok);
  }

  // 4. connect the QP with myself
  qp->bind_remote_mr(mr);
  qp->bind_local_mr(mr);

  auto res_c = qp->connect(qp->my_attr());
  RDMA_ASSERT(res_c == IOCode::Ok);

  /************************************************/
  for(uint i = 0;i < recv_depth;++i) {
    auto msg = ::rdmaio::Marshal::dump<u64>(i);
    auto res_s = qp->send_normal(
        {.op = IBV_WR_SEND_WITH_IMM,
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

  // test to ensure that we recv enough msgs
  usize recved_msgs = 0;
  for (RecvIter<RC, recv_depth> iter(qp, recv_rs); iter.has_msgs(); iter.next()) {
    // check message content
    auto imm_msg = iter.cur_msg().value();

    auto buf = static_cast<char *>(std::get<1>(imm_msg));
    auto res = ::rdmaio::Marshal::dedump<u64>(std::string(buf, 128)).value();

    ASSERT_EQ(std::get<0>(imm_msg), 73 + recved_msgs);
    ASSERT_EQ(res, recved_msgs);

    recved_msgs += 1;
  }
  ASSERT_EQ(recved_msgs, recv_depth);

  // test whether we can re-send messages

  for (uint i = 0; i < recv_depth; ++i) {
    auto msg = ::rdmaio::Marshal::dump<u64>(i);
    auto res_s = qp->send_normal({.op = IBV_WR_SEND_WITH_IMM,
                                  .flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE,
                                  .len = sizeof(u64),
                                  .wr_id = 0},
                                 {.local_addr = (RMem::raw_ptr_t)(msg.data()),
                                  .remote_addr = 0,
                                  .imm_data = 73 + i * 2});
    RDMA_ASSERT(res_s == IOCode::Ok);
    auto res_p = qp->wait_one_comp();
    RDMA_ASSERT(res_p == IOCode::Ok);
  }

  {
    sleep(1);

    // test to ensure that we recv enough msgs
    usize recved_msgs = 0;
    for (RecvIter<RC, recv_depth> iter(qp, recv_rs); iter.has_msgs();
         iter.next()) {
      // check message content
      auto imm_msg = iter.cur_msg().value();

      auto buf = static_cast<char *>(std::get<1>(imm_msg));
      auto res = ::rdmaio::Marshal::dedump<u64>(std::string(buf, 128)).value();

      ASSERT_EQ(std::get<0>(imm_msg), 73 + recved_msgs * 2);
      ASSERT_EQ(res, recved_msgs);

      recved_msgs += 1;
    }
    ASSERT_EQ(recved_msgs, recv_depth);
  }
#endif
}

} // namespace test
