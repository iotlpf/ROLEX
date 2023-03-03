#include <gtest/gtest.h>

#include "../core/nicinfo.hh"
#include "../core/qps/mod.hh"
#include "../core/utils/marshal.hh"

#include "../core/qps/op.hh"

namespace test {

using namespace rdmaio::qp;
using namespace rdmaio;

class OpTest : public testing::Test{
 protected:
  virtual void SetUp() override {
    auto res = RNicInfo::query_dev_names();
    ASSERT_FALSE(res.empty());
    nic = std::make_shared<RNic>(res[0]);
    ASSERT_TRUE(nic->valid());

    auto config = QPConfig();
    qp = RC::create(nic, config).value();
    ASSERT_TRUE(qp->valid());

    auto res_c = qp->connect(qp->my_attr());
    RDMA_ASSERT(res_c == IOCode::Ok);
  }

  Arc<RNic> nic;
  Arc<RC> qp;
};

TEST_F(OpTest, basic) {
  // try send an RDMA request
  // init the memory
  auto mem = Arc<RMem>(new RMem(1024));  // allocate a memory with 1K bytes
  ASSERT_TRUE(mem->valid());

  // register it to the Nic
  RegHandler handler(mem, nic);
  ASSERT_TRUE(handler.valid());

  auto mr = handler.get_reg_attr().value();
  qp->bind_remote_mr(mr);
  qp->bind_local_mr(mr);

  // ideal: buf[73, 0] -> buf[73, 73]
  u64 *test_loc = reinterpret_cast<u64 *>(mr.buf);
  *test_loc = 73; // remote mem
  ASSERT_NE(73, test_loc[1]);  // use the next entry to store the read value

  Op<> op;
  op.set_rdma_rbuf(test_loc, mr.key).set_read().set_imm(0);
  ASSERT_TRUE(op.set_payload(test_loc + 1, sizeof(u64), mr.key));

  auto res_s = op.execute(qp, IBV_SEND_SIGNALED);

  RDMA_ASSERT(res_s == IOCode::Ok);
  auto res_p = qp->wait_one_comp();
  RDMA_ASSERT(res_p == IOCode::Ok);
  ASSERT_EQ(test_loc[1], 73);
}

TEST_F(OpTest, FetchAdd) {
  // try send an FetchAndAdd request
  // init the memory
  auto mem = Arc<RMem>(new RMem(1024));  // allocate a memory with 1K bytes
  ASSERT_TRUE(mem->valid());

  // register it to the Nic
  RegHandler handler(mem, nic);
  ASSERT_TRUE(handler.valid());

  auto mr = handler.get_reg_attr().value();
  qp->bind_remote_mr(mr);
  qp->bind_local_mr(mr);

  u64 origin_data = 73;
  // ideal: buf[73, 0] -> buf[73+73, 73]
  u64 *test_loc = reinterpret_cast<u64 *>(mr.buf);
  test_loc[0] = origin_data;  // remote mem
  test_loc[1] = 0;  // local mem
  ASSERT_NE(origin_data, test_loc[1]);  // use the next entry to store the read value

  Op<> op;
  op.set_atomic_rbuf(test_loc, mr.key).set_fetch_add(origin_data);
  ASSERT_TRUE(op.set_payload(test_loc + 1, sizeof(u64), mr.key));

  auto res_s = op.execute(qp, IBV_SEND_SIGNALED);

  RDMA_ASSERT(res_s == IOCode::Ok) << res_s.desc;
  auto res_p = qp->wait_one_comp();
  RDMA_ASSERT(res_p == IOCode::Ok);
  ASSERT_EQ(test_loc[1], origin_data);
  ASSERT_EQ(test_loc[0], origin_data * 2);
}

TEST_F(OpTest, CAS) {
  // try send an CompareAndAdd request
  // init the memory
  auto mem = Arc<RMem>(new RMem(1024));  // allocate a memory with 1K bytes
  ASSERT_TRUE(mem->valid());

  // register it to the Nic
  RegHandler handler(mem, nic);
  ASSERT_TRUE(handler.valid());

  auto mr = handler.get_reg_attr().value();
  qp->bind_remote_mr(mr);
  qp->bind_local_mr(mr);

  u64 compare_data = 73;
  u64 swap_data = 24;

  // ideal cas fail: buf[0, 24] -> buf[0, 0]
  // ideal cas success: buf[73, 0] -> buf[24, 73]
  u64 *test_loc = reinterpret_cast<u64 *>(mr.buf);
  test_loc[0] = 0;            // remote mem
  test_loc[1] = swap_data;    // local mem init with useless data

  Op<> op;
  op.set_atomic_rbuf(test_loc, mr.key).set_cas(compare_data, swap_data);
  ASSERT_TRUE(op.set_payload(test_loc + 1, sizeof(u64), mr.key));

  auto res_s = op.execute(qp, IBV_SEND_SIGNALED);

  RDMA_ASSERT(res_s == IOCode::Ok);
  auto res_p = qp->wait_one_comp();
  RDMA_ASSERT(res_p == IOCode::Ok);
  // compare fail, no data swap
  ASSERT_EQ(test_loc[0], 0);
  ASSERT_EQ(test_loc[1], 0);

  test_loc[0] = compare_data;  // remote mem init with compare data

  auto res_s2 = op.execute(qp, IBV_SEND_SIGNALED);

  RDMA_ASSERT(res_s2 == IOCode::Ok);
  auto res_p2 = qp->wait_one_comp();
  RDMA_ASSERT(res_p2 == IOCode::Ok);
  // compare success, data swap occur
  ASSERT_EQ(test_loc[0], swap_data);
  ASSERT_EQ(test_loc[1], compare_data);
}

}  // namespace test
