#include <gtest/gtest.h>

#include "../core/lib.hh"
#include "../core/qps/mod.hh"

namespace test {

using namespace rdmaio;
using namespace rdmaio::qp;

TEST(CM, MR) {
  RCtrl ctrl(8889);
  ctrl.start_daemon();

  // now we register the MR to the CM

  // 1. open an RNic handler
  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty()); // there has to be NIC on the host machine

  Arc<RNic> nic = Arc<RNic>(new RNic(res[0]));

  // 2. allocate a buffer, and register it
  // allocate a memory with 1024 bytes
  auto mr = Arc<RegHandler>(new RegHandler(Arc<RMem>(new RMem(1024)), nic));
  ctrl.registered_mrs.reg(73,mr);

  // 3. fetch it through ethernet
  ConnectManager cm("localhost:8889");
  if (cm.wait_ready(1000000,2) ==
      IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
    assert(false);

  auto fetch_res = cm.fetch_remote_mr(73);
  RDMA_ASSERT(fetch_res == IOCode::Ok) << std::get<0>(fetch_res.desc);
  auto attr = std::get<1>(fetch_res.desc);

  // 4. check the remote fetched one is the same as the local copy
  auto local_mr = mr->get_reg_attr().value();
  ASSERT_EQ(local_mr.buf,attr.buf);
  ASSERT_EQ(local_mr.sz, attr.sz);
  ASSERT_EQ(local_mr.key,attr.key);

  ctrl.stop_daemon();
}

TEST(CM, QP) {
  RCtrl ctrl(6666);
  ctrl.start_daemon();

  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty()); // there has to be NIC on the host machine

  auto nic = RNic::create(res[0]).value();
  auto ud  = UD::create(nic,QPConfig().set_qkey(73)).value();
  auto test_attr = ud->my_attr();

  ctrl.registered_qps.reg("test_ud_qp", ud).value();

  ConnectManager cm("localhost:6666");
  if (cm.wait_ready(1000000, 2) ==
      IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
    assert(false);

  auto fetch_qp_attr_res = cm.fetch_qp_attr("test_ud_qp");
  RDMA_ASSERT(fetch_qp_attr_res == IOCode::Ok)
      << "fetch qp attr error: " << std::get<0>(fetch_qp_attr_res.desc);
  auto fetched_attr = std::get<1>(fetch_qp_attr_res.desc);

  // check the fetched attr matches the test_attr
  ASSERT_EQ(test_attr.lid,fetched_attr.lid);
  ASSERT_EQ(test_attr.psn, fetched_attr.psn);
  ASSERT_EQ(test_attr.port_id, fetched_attr.port_id);
  ASSERT_EQ(test_attr.qpn, fetched_attr.qpn);
  ASSERT_EQ(test_attr.qkey, fetched_attr.qkey);
  ASSERT_EQ(fetched_attr.qkey,73);
  RDMA_LOG(2) << "qkey: " << 73;

  ctrl.stop_daemon();
}

}// namespace test
