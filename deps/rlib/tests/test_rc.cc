#include <gtest/gtest.h>

#include "../core/qps/mod.hh"
#include "../core/nicinfo.hh"

#include "../core/utils/marshal.hh"

namespace test {

using namespace rdmaio::qp;
using namespace rdmaio;

TEST(RRC, basic) {

  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty());
  auto nic = std::make_shared<RNic>(res[0]);
  ASSERT_TRUE(nic->valid());

  auto config = QPConfig();
  auto qpp = RC::create(nic,config).value();
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
  ASSERT_NE(73,test_loc[1]); // use the next entry to store the read value

  auto res_s = qp.send_normal({.op = IBV_WR_RDMA_READ,
                               .flags = IBV_SEND_SIGNALED,
                               .len = sizeof(u64),
                               .wr_id = 0},
                              {
                                .local_addr = reinterpret_cast<RMem::raw_ptr_t>(test_loc + 1),
                                .remote_addr = 0,
                                .imm_data = 0
                              });
  RDMA_ASSERT(res_s == IOCode::Ok);
  auto res_p = qp.wait_one_comp();
  RDMA_ASSERT(res_p == IOCode::Ok);
  ASSERT_EQ(test_loc[1],73);
}

TEST(RRC, Factory) {

  ::rdmaio::qp::RCFactory factory;

  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty());

  auto nic = RNic::create(res[0]).value();

  auto qp_res = factory.create_then_reg(12,nic,QPConfig()).value();
  ASSERT_TRUE(std::get<0>(qp_res)->valid());
}

} // namespace test
