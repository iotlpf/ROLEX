#include <gtest/gtest.h>

#include "rlib/core/lib.hh"
#include "../src/rdma/sop.hh"

namespace test
{

using namespace rdmaio;
using namespace r2::rdma;
using namespace r2;

TEST(RDMA, sop)
{
  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty());
  auto nic = std::make_shared<RNic>(res[0]);
  ASSERT_TRUE(nic->valid());

  auto config = QPConfig();
  auto qpp = RC::create(nic, config).value();
  ASSERT_TRUE(qpp->valid());

  // try send an RDMA request
  // init the memory
  auto mem = Arc<RMem>(new RMem(1024)); // allocate a memory with 1K bytes
  ASSERT_TRUE(mem->valid());

  // register it to the Nic
  RegHandler handler(mem, nic);
  ASSERT_TRUE(handler.valid());

  auto mr = handler.get_reg_attr().value();

  RC &qp = *qpp;
  qp.bind_remote_mr(mr);
  qp.bind_local_mr(mr);

  // finally connect to myself
  auto res_c = qp.connect(qp.my_attr());
  RDMA_ASSERT(res_c == IOCode::Ok);

  u64 *test_loc = reinterpret_cast<u64 *>(mem->raw_ptr);
  *test_loc = 73;
  ASSERT_NE(73, test_loc[1]); // use the next entry to store the read value

  SROp op;
  op.set_payload(&test_loc[1],sizeof(u64)).set_remote_addr(0x0).set_read();

  auto ret = op.execute_sync(qpp,IBV_SEND_SIGNALED);
  ASSERT(ret == IOCode::Ok);
  ASSERT_EQ(test_loc[1],73);

  // now we test async op
  bool runned = false;
  SScheduler ssched;
  ssched.spawn([qpp, test_loc, &op, &runned](R2_ASYNC) {
    op.set_write();
    test_loc[1] = 52;
    ASSERT_NE(test_loc[0], 52);
    auto ret = op.execute(qpp, IBV_SEND_SIGNALED, R2_ASYNC_WAIT);
    R2_YIELD;
    ASSERT(ret == IOCode::Ok);
    ASSERT_EQ(test_loc[0], 52);
    runned = true;

    R2_STOP();
    R2_RET;
  });
  ssched.run();
  ASSERT_TRUE(runned);
}
} // namespace test
