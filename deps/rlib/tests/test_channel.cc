#include <gtest/gtest.h>

#include "../core/bootstrap/channel.hh"

namespace test {

using namespace rdmaio;
using namespace rdmaio::bootstrap;

TEST(Channel, Naming) {
  auto host = "localhost";
  auto ip = IPNameHelper::host2ip(host);
  RDMA_ASSERT(ip == IOCode::Ok);
  RDMA_LOG(4) << "parsed ip: " << ip.desc;
  //auto ip1 = IPNameHelper::host2ip("val02");
  auto ip1 = IPNameHelper::host2ip("192.168.3.101");
  RDMA_ASSERT(ip1 == IOCode::Ok);
  RDMA_LOG(4) << "parsed ip for val02: " << ip1.desc;

  auto ip3 = IPNameHelper::host2ip("  192.168.3.101  ");
  RDMA_ASSERT(ip3 == IOCode::Ok)
      << "get ip3 code: " << ip3.code.name() << " " << ip3.desc;
  ASSERT_EQ(ip3.desc, ip1.desc);
}

TEST(Channel, Basic) {
  const usize total_sent = 12;

  auto send_c = SendChannel::create("localhost:7777").value();
  RDMA_LOG(2) << "create send channel done";

  auto recv_c = RecvChannel::create(7777).value();
  RDMA_LOG(2) << "create recv channel done";

  //auto recv_c_fail = RecvChannel::create(8888);
  //ASSERT_FALSE(recv_c_fail);

  for (uint i = 0; i < total_sent; ++i) {
    auto b = Marshal::dump<u64>(i + 73);
    send_c->send(b);
  }

  RDMA_LOG(2) << "send all done";

  usize count = 0;
  for (recv_c->start(); recv_c->has_msg(); recv_c->next(), count += 1) {
    auto &msg = recv_c->cur();
    u64 val = Marshal::dedump<u64>(msg).value();
    ASSERT_EQ(val, count + 73);

    // now prepare the reply
    auto reply = Marshal::dump<u64>(count + 75);
    recv_c->reply_cur(reply);
  }

  ASSERT_EQ(count, total_sent);

  RDMA_LOG(2) << "start sanity check replies";

  // finally we check the reply
  for (uint i = 0; i < total_sent; ++i) {
    auto reply_res = send_c->recv();
    RDMA_ASSERT(reply_res == IOCode::Ok);
    auto &msg = reply_res.desc;
    u64 val = Marshal::dedump<u64>(msg).value();
    ASSERT_EQ(val, i + 75);
  }
  RDMA_LOG(2) << "check replies done, ok";
}

} // namespace test
