#include <gtest/gtest.h>

#include "rlib/core/nicinfo.hh"
#include "rlib/core/qps/recv_iter.hh"
#include "rlib/core/utils/marshal.hh"

#include "../src/msg/ud_session.hh"

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

TEST(UDS, Create) {
  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty());
  auto nic = std::make_shared<RNic>(res[0]);
  ASSERT_TRUE(nic->valid());

  auto ud = UD::create(nic, QPConfig()).value();
  ASSERT_TRUE(ud->valid());

  // prepare the message buf
  auto mem =
      Arc<RMem>(new RMem(16 * 1024 * 1024)); // allocate a memory with 4M bytes
  ASSERT_TRUE(mem->valid());

  auto handler = RegHandler::create(mem, nic).value();
  SimpleAllocator alloc(mem, handler->get_reg_attr().value().key);

  // prepare buffer, contain 16 recv entries, each has 4096 bytes
  auto recv_rs = RecvEntriesFactory<SimpleAllocator, 1024, 1000>::create(alloc);

  // post these recvs to the UD
  {
    recv_rs->sanity_check();
    auto res = ud->post_recvs(*recv_rs, 1024);
    RDMA_ASSERT(res == IOCode::Ok);
  }

  /************************************/

  /** send the messages **/
  // 1. create the ah
  auto ud_session = UDSession::create(73,ud, ud->my_attr()).value();
  for (uint i = 0; i < 1024; ++i) {
    auto msg = ::rdmaio::Marshal::dump<u64>(i);
    //auto res_s = ud_session->send_blocking(
    //{.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
    auto res_s = ud_session->send_unsignaled({.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
    ASSERT(res_s == IOCode::Ok);
  }

  // 2. start to recv message
  sleep(1);

  // finally we check the messages
  uint recved_msgs = 0;
  for (RecvIter<UD, 1024> iter(ud, recv_rs); iter.has_msgs(); iter.next()) {
    // check message content
    auto imm_msg = iter.cur_msg().value();

    auto buf = static_cast<char *>(std::get<1>(imm_msg)) + kGRHSz;
    auto res = ::rdmaio::Marshal::dedump<u64>(std::string(buf, 128)).value();

    ASSERT_EQ(std::get<0>(imm_msg), 73);
    ASSERT_EQ(res, recved_msgs);

    recved_msgs += 1;
  }
  ASSERT_EQ(recved_msgs, 1024);
}

} // namespace test
