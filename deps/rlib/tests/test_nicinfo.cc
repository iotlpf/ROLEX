#include <gtest/gtest.h>

#include "../core/nicinfo.hh"

using namespace rdmaio;

TEST(RNic,Exists) {
  auto res = RNicInfo::query_dev_names();
  ASSERT_FALSE(res.empty()); // there has to be NIC on the host machine
}

TEST(RNic, State) {
  // check the NIC at this server is activate,
  // which means that the call to `ibstatus` will result in ACTIVE
  auto nic =
      RNic::create(RNicInfo::query_dev_names().at(0)).value();
  ASSERT_TRUE(nic->is_active()==IOCode::Ok);
}
