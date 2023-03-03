#include <gtest/gtest.h>

#include "../core/nicinfo.hh"
#include "../core/rmem/handler.hh"

namespace test {

using namespace rdmaio;
using namespace rdmaio::rmem;

TEST(RMEM, can_reg) {
  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty()); // there has to be NIC on the host machine

  Arc<RNic> nic = Arc<RNic>(new RNic(res[0]));
  {
    // use RMem to allocate and manage a region of memory on the heap,
    // with size 1024
    auto mem = Arc<RMem>(new RMem(1024));
    // the allocation must be succesful
    ASSERT_TRUE(mem->valid());

    // register this rmem to the RDMA Nic specificed by the nic
    RegHandler handler(mem, nic);
    ASSERT_TRUE(handler.valid());
    ASSERT_TRUE(handler.get_reg_attr());

    auto mem_not_valid = Arc<RMem>(new RMem(
        1024, [](u64 sz) { return nullptr; }, [](RMem::raw_ptr_t p) {}));
    ASSERT_FALSE(mem_not_valid->valid());

    RegHandler handler_not_valid(mem_not_valid, nic);
    ASSERT_FALSE(handler_not_valid.valid());
  }
}

TEST(RMEM, factory) {
  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty()); // there has to be NIC on the host machine

  MRFactory factory;
  {
    // the nic's ownership will be passed to factory
    // with the registered mr.
    // so it will not free RDMA resource (ctx,pd) even it goes out of scope
    Arc<RNic> nic = Arc<RNic>(new RNic(res[0]));
    {
      auto mr = Arc<RegHandler>(new RegHandler(Arc<RMem>(new RMem(1024)), nic));
      auto res = factory.reg(73, mr);
      RDMA_ASSERT(res);

      // test that we filter out duplicate registeration.
      auto res1 = factory.reg(73, mr);
      RDMA_ASSERT(!res1);

      // test that an invalid MR should not be registered
      auto mem_not_valid = Arc<RMem>(new RMem(
          1024, [](u64 sz) { return nullptr; }, [](RMem::raw_ptr_t p) {}));
      ASSERT_FALSE(mem_not_valid->valid());

      // finally, test create and register the MR
      auto mr3_res = factory.create_then_reg(12,Arc<RMem>(new RMem(1024)),nic);
      RDMA_ASSERT(mr3_res);
      auto mr3 = std::get<0>(mr3_res.value());
      ASSERT_TRUE(mr3->valid());
    }
  }
}

TEST(RMEM, Err) {
#if 0
  // an example to show the error case of not using Arc
  auto res = RNicInfo::query_dev_names();
  RNic nic(res[0]);
  char *buffer = new char[1024];
  auto mr = ibv_reg_mr(nic.get_pd(), buffer, 1024, MemoryFlags().get_value());
  ASSERT_NE(mr,nullptr);
  delete buffer;
#endif
}

} // namespace test
