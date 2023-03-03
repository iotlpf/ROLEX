#include <gtest/gtest.h>

#include "../core/bootstrap/srpc.hh"

namespace test {

using namespace rdmaio;
using namespace rdmaio::bootstrap;

TEST(RPC, Basic) {

  SRpc rpc("localhost:1111");
  SRpcHandler handler(1111);

  for (uint i = 0; i < 12; ++i) {
    handler.register_handler(73 + i, [i](const ByteBuffer &b) -> ByteBuffer {
      RDMA_ASSERT(b.size() == i + 233)
          << "recv sz: " << b.size() << "; compare:" << 233 + i;
      RDMA_ASSERT(b.compare(ByteBuffer(i + 233, '3' + i)) == 0);
      return ByteBuffer(73 + i, '1' + i);
    });
  }

  for (uint i = 0; i < 12; ++i) {

    // send the rpc
    auto res = rpc.call(73 + i, ByteBuffer(233 + i, '3' + i));
    RDMA_ASSERT(res == IOCode::Ok) << "call error: " << res.desc;

    // wait for enough time so the msg must arrive at the target
    sleep(1);

    auto rpc_calls = handler.run_one_event_loop();
    ASSERT_EQ(rpc_calls, 1);

    // wait for enough time so the reply must arrive at the sender
    sleep(1);

    auto res_reply = rpc.receive_reply();
    RDMA_ASSERT(res_reply == IOCode::Ok);

    ASSERT_EQ(res_reply.desc.size(), 73 + i);
  }
}

} // namespace test
