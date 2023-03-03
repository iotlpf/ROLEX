#include <gtest/gtest.h>

#include "rlib/core/qps/recv_iter.hh"

#include "../src/ring_msg/mod.hh"

#include "../src/mem_block.hh"

namespace test {

using namespace r2;
using namespace r2::ring_msg;

TEST(Ring, Basic) {
  MemBlock test(nullptr, 1024);
  LocalRing<1024> ring(test);

  ASSERT_TRUE(ring.cur_msg(64));
  ASSERT_FALSE(ring.cur_msg(1024));
}

class AAllocator : public AbsRecvAllocator {
  RMem::raw_ptr_t buf = nullptr;
  usize total_mem = 0;
  mr_key_t key;

  RegAttr mr;

public:
  AAllocator(Arc<RMem> mem, const RegAttr &mr)
      : buf(mem->raw_ptr), total_mem(mem->sz), mr(mr), key(mr.key) {
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

  ::rdmaio::Option<std::pair<rmem::RMem::raw_ptr_t, rmem::RegAttr>>
  alloc_one_for_remote(const usize &sz) override {
    if (total_mem < sz)
      return {};
    auto ret = buf;
    buf = static_cast<char *>(buf) + sz;
    total_mem -= sz;
    return std::make_pair(ret, mr);
  }
};

TEST(RM, Connect) {

  RCtrl ctrl(8885);
  RingManager<128> rm(ctrl);

  // init the structure for creating receiver
  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty());
  auto nic = RNic::create(res.at(0)).value();
  RDMA_ASSERT(ctrl.opened_nics.reg(0, nic));

  const usize recv_depth = 4096;
  // 1. create recv commm data structure
  auto recv_cq_res = ::rdmaio::qp::Impl::create_cq(nic, recv_depth);
  RDMA_ASSERT(recv_cq_res == IOCode::Ok);
  auto recv_cq = std::get<0>(recv_cq_res.desc);

  // 2. init the mem
  auto mem =
      Arc<RMem>(new RMem(16 * 1024 * 1024)); // allocate a memory with 4M bytes
  ASSERT_TRUE(mem->valid());

  auto handler = RegHandler::create(mem, nic).value();
  auto mr = handler->get_reg_attr().value();

  auto alloc =
      Arc<AAllocator>(new AAllocator(mem, handler->get_reg_attr().value()));

  // 3. create a receiver for receving all messages
  // auto receiver =
  // rm.create_receiver<128, 1024, 64>("test_channel", recv_cq, alloc).value();
  auto receiver =
      RecvFactory<128, 4096, 64>::create(rm, "test_channel", recv_cq, alloc)
          .value();
  ASSERT_TRUE(receiver);

  ctrl.start_daemon();

  // This recv cq is used for the sender
  auto recv_cq_res1 = ::rdmaio::qp::Impl::create_cq(nic, recv_depth);
  RDMA_ASSERT(recv_cq_res1 == IOCode::Ok);
  auto recv_cq1 = std::get<0>(recv_cq_res1.desc);

  // used to receive sender's reply
  // auto receiver_s =
  // rm.create_receiver<128, 1024, 64>("reply_channel", recv_cq1, alloc)
  //.value();
  auto receiver_s =
      std::make_shared<Receiver<128, 4096, 64>>("reply_channel", recv_cq1);
  ASSERT_TRUE(receiver_s);

  // 4. Try connect the session
  auto ss = std::make_shared<Session<128, 4096, 64>>(
      static_cast<u16>(73), nic, QPConfig(), recv_cq1, alloc);
  receiver_s->reg_channel(ss);

  RingCM cm("localhost:8885");
  if (cm.wait_ready(1000000, 2) ==
      IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
    assert(false);

  ASSERT(ss->connect(cm, "test_channel", 0, QPConfig()) == IOCode::Ok);

  // try send something
  auto msg = ::rdmaio::Marshal::dump<u64>(0xdeadbeaf);
  auto res_s =
      ss->send_blocking({.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
  ASSERT(res_s == IOCode::Ok);

  sleep(1);

  LocalRing<1024> local_ring(rm.query_ring(std::to_string(73)).value());

  // now we use the more advanced iterator for test
  using RI = RingRecvIter<128, 4096, 64>;
  for (RI iter(receiver); iter.has_msgs(); iter.next()) {
    auto cur_msg = iter.cur_msg();
    ASSERT_EQ(cur_msg.sz, sizeof(u64));

    u64 *value_ptr = cur_msg.interpret_as<u64>(0);
    ASSERT_EQ(*value_ptr, 0xdeadbeaf);
    // ASSERT_EQ(*value_ptr, 0);
  }

  LOG(4) << "one round simple test done";

  // test of send more messages
  for (uint i = 0; i < 64; ++i) {
    auto msg = ::rdmaio::Marshal::dump<u64>(i);
    auto res_s = ss->send_unsignaled(
        {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
    ASSERT(res_s == IOCode::Ok);
  }
  sleep(1);

  LOG(4) << "Recv round 2";
  usize recv_count = 0;

  // try recv
  for (RI iter(receiver); iter.has_msgs(); iter.next()) {
    auto cur_msg = iter.cur_msg();
    u64 *value_ptr = cur_msg.interpret_as<u64>(0);
    ASSERT_EQ(*value_ptr, recv_count++);

    // send a reply
    auto cur_session = iter.cur_session();

    auto msg = ::rdmaio::Marshal::dump<u64>(recv_count + 73);
    auto res_s = cur_session->send_blocking(
        {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
    ASSERT(res_s == IOCode::Ok);
  }
  ASSERT_EQ(recv_count, 64);

  sleep(1);

  // try recv reply from client
  {
    usize recv_count = 0;
    for (RI iter(receiver_s); iter.has_msgs(); iter.next()) {
      auto cur_msg = iter.cur_msg();
      u64 *value_ptr = cur_msg.interpret_as<u64>(0);
      ASSERT_EQ(*value_ptr, recv_count++ + (1 + 73));
    }
    ASSERT_EQ(recv_count, 64);
  }

  // test with more rounds
  {
    for (uint i = 0; i < 32; ++i) {
      auto msg = ::rdmaio::Marshal::dump<u64>(i);
      auto res_s = ss->send_unsignaled(
          {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
      ASSERT(res_s == IOCode::Ok);
    }
    sleep(1);

    LOG(4) << "Recv round 3";
    usize recv_count = 0;

    // try recv
    for (RI iter(receiver); iter.has_msgs(); iter.next()) {
      auto cur_msg = iter.cur_msg();
      u64 *value_ptr = cur_msg.interpret_as<u64>(0);
      ASSERT_EQ(*value_ptr, recv_count++);

      // send a reply
      auto cur_session = iter.cur_session();

      auto msg = ::rdmaio::Marshal::dump<u64>(recv_count + 73);
      auto res_s = cur_session->send_blocking(
          {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
      ASSERT(res_s == IOCode::Ok);
    }
    ASSERT_EQ(recv_count, 32);

    sleep(1);

    // try recv reply from client
    {
      usize recv_count = 0;
      for (RI iter(receiver_s); iter.has_msgs(); iter.next()) {
        auto cur_msg = iter.cur_msg();
        u64 *value_ptr = cur_msg.interpret_as<u64>(0);
        ASSERT_EQ(*value_ptr, recv_count++ + (1 + 73));
      }
      ASSERT_EQ(recv_count, 32);
    }
  }

  {
    for (uint i = 0; i < 16; ++i) {
      auto msg = ::rdmaio::Marshal::dump<u64>(i);
      auto res_s = ss->send_unsignaled(
          {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
      ASSERT(res_s == IOCode::Ok);
    }
    sleep(1);

    LOG(4) << "Recv round 4";
    usize recv_count = 0;

    // try recv
    for (RI iter(receiver); iter.has_msgs(); iter.next()) {
      auto cur_msg = iter.cur_msg();
      u64 *value_ptr = cur_msg.interpret_as<u64>(0);
      ASSERT_EQ(*value_ptr, recv_count++);

      // send a reply
      auto cur_session = iter.cur_session();

      auto msg = ::rdmaio::Marshal::dump<u64>(recv_count + 73);
      auto res_s = cur_session->send_blocking(
          {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
      ASSERT(res_s == IOCode::Ok);
    }
    ASSERT_EQ(recv_count, 16);

    sleep(1);

    // try recv reply from client
    {
      usize recv_count = 0;
      for (RI iter(receiver_s); iter.has_msgs(); iter.next()) {
        auto cur_msg = iter.cur_msg();
        u64 *value_ptr = cur_msg.interpret_as<u64>(0);
        ASSERT_EQ(*value_ptr, recv_count++ + (1 + 73));
      }
      ASSERT_EQ(recv_count, 16);
    }
  }

  {
    for (uint i = 0; i < 16; ++i) {
      auto msg = ::rdmaio::Marshal::dump<u64>(i);
      auto res_s = ss->send_unsignaled(
          {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
      ASSERT(res_s == IOCode::Ok);
    }
    sleep(1);

    LOG(4) << "Recv round 5";
    usize recv_count = 0;

    // try recv
    for (RI iter(receiver); iter.has_msgs(); iter.next()) {
      auto cur_msg = iter.cur_msg();
      u64 *value_ptr = cur_msg.interpret_as<u64>(0);
      ASSERT_EQ(*value_ptr, recv_count++);

      // send a reply
      auto cur_session = iter.cur_session();

      auto msg = ::rdmaio::Marshal::dump<u64>(recv_count + 73);
      auto res_s = cur_session->send_blocking(
          {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
      ASSERT(res_s == IOCode::Ok);
    }
    ASSERT_EQ(recv_count, 16);

    sleep(1);

    // try recv reply from client
    {
      usize recv_count = 0;
      for (RI iter(receiver_s); iter.has_msgs(); iter.next()) {
        auto cur_msg = iter.cur_msg();
        u64 *value_ptr = cur_msg.interpret_as<u64>(0);
        ASSERT_EQ(*value_ptr, recv_count++ + (1 + 73));
      }
      ASSERT_EQ(recv_count, 16);
    }
  }

  // test more rounds
  for(uint r = 0;r < 12;++r) {
    for (uint i = 0; i < 16; ++i) {
      auto msg = ::rdmaio::Marshal::dump<u64>(i);
      auto res_s = ss->send_unsignaled(
          {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
      ASSERT(res_s == IOCode::Ok);
    }
    sleep(1);

    LOG(4) << "Recv round " << r + 6;
    usize recv_count = 0;

    // try recv
    for (RI iter(receiver); iter.has_msgs(); iter.next()) {
      auto cur_msg = iter.cur_msg();
      u64 *value_ptr = cur_msg.interpret_as<u64>(0);
      ASSERT_EQ(*value_ptr, recv_count++);

      // send a reply
      auto cur_session = iter.cur_session();

      auto msg = ::rdmaio::Marshal::dump<u64>(recv_count + 73);
      auto res_s = cur_session->send_blocking(
          {.mem_ptr = (void *)(msg.data()), .sz = sizeof(u64)});
      ASSERT(res_s == IOCode::Ok);
    }
    ASSERT_EQ(recv_count, 16);

    sleep(1);

    // try recv reply from client
    {
      usize recv_count = 0;
      for (RI iter(receiver_s); iter.has_msgs(); iter.next()) {
        auto cur_msg = iter.cur_msg();
        u64 *value_ptr = cur_msg.interpret_as<u64>(0);
        ASSERT_EQ(*value_ptr, recv_count++ + (1 + 73));
      }
      ASSERT_EQ(recv_count, 16);
    }

  }

}

} // namespace test
