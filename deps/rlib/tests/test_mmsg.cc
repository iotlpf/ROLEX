#include <gtest/gtest.h>

#include "../core/bootstrap/multi_msg.hh"
#include "../core/bootstrap/multi_msg_iter.hh"

#include "./random.hh"

using namespace rdmaio;
using namespace rdmaio::bootstrap;

namespace test {

TEST(BootMsg, Basic) {

  using TestMsg = MultiMsg<1024>;

  TestMsg mss; // create a MultioMsg with total 1024-bytes capacity.

  FastRandom rand(0xdeadbeaf);

  std::vector<ByteBuffer> ground_truth;

  usize cur_sz = 0;
  for (uint i = 0; i < kMaxMultiMsg; ++i) {

    // todo: google test random string ?
    ByteBuffer msg = rand.next_string(rand.rand_number<usize>(50, 300));

    auto res = mss.append(msg);
    cur_sz += msg.size();

    if (!res) {
      ASSERT_TRUE(cur_sz + sizeof(MsgsHeader) > 1024);
      break;
    } else {
      // we really push the message, so add it to the ground-truth
      ground_truth.push_back(msg);
    }
  }

  RDMA_LOG(4) << "total " << mss.num_msg() << " test msgs added";

  // now we parse the msg content
  auto &copied_msg = *(mss.buf);
  RDMA_LOG(4) << "copied msg's sz: " << copied_msg.size();
  auto mss_2 = TestMsg::create_from(copied_msg).value();
  ASSERT_EQ(mss_2.num_msg(),mss.num_msg());

  // iterate through the msgs to check contents

  // finally we check the msg content
  usize iter_count = 0;
  for (MsgsIter<TestMsg> iter(mss_2); iter.valid(); iter.next()) {
    ASSERT_TRUE(iter_count < ground_truth.size());
    ASSERT_EQ(iter.cur_msg().compare(ground_truth[iter_count]),0);
    ASSERT_EQ(iter.cur_msg().compare(mss_2.query_one(iter_count).value()), 0);
    iter_count += 1;
  }

  ASSERT_EQ(iter_count,ground_truth.size());
}

} // namespace test
