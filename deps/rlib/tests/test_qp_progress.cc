#include <gtest/gtest.h>

#include "../core/qps/mod.hh"
#include "./fast_random.hh"

namespace test {

using namespace rdmaio;
using namespace rdmaio::qp;

TEST(QP, Progress) {

  auto limit = std::numeric_limits<u32>::max();

  Progress progress;

  progress.forward(12);
  ASSERT_EQ(progress.pending_reqs(), 12);

  progress.done(12);
  ASSERT_EQ(progress.pending_reqs(), 0);

  // now we run a bunch of whole tests
  FastRandom rand(0xdeadbeaf);
  u64 sent = 0;
  u64 done = 0;

  u64 temp_done = 0;
#if 0 // usually we donot do this test, since it's so time consuming
  Progress p2;

  while(sent <=
        static_cast<u64>(std::numeric_limits<uint32_t>::max()) * 64) {
    int to_send = rand.rand_number(12, 4096);
    p2.forward(to_send);

    sent += to_send;
    ASSERT_EQ(p2.pending_reqs(),to_send);

    int to_recv = rand.rand_number(0,to_send);
    while(to_send > 0) {
      temp_done += to_recv;
      p2.done(temp_done);
      ASSERT(to_send >= to_recv) << "send: " << to_send <<";"
                                 << "recv: " << to_recv;
      ASSERT_EQ(p2.pending_reqs(),to_send - to_recv);
      to_send -= to_recv;

      to_recv = rand.rand_number(0, to_send);
    }
  }
  ASSERT_EQ(p2.pending_reqs(),0);
#endif
}

} // namespace test
