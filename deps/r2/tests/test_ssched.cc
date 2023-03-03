#include <gtest/gtest.h>

#include "../src/libroutine.hh"
#include "../src/logging.hh"

using namespace r2;

namespace test {

TEST(SSched, Basic) {

  usize counter = 0;
  SScheduler ssched;
#if 1
  for (uint i = 0; i < 12; ++i)
    ssched.spawn([&counter,i](R2_ASYNC) {

      counter += 1;
      if (i == 11)
        R2_STOP();
      //LOG(2) << "inc done";
      R2_RET;
    });
#endif

  ssched.run();
  ASSERT_EQ(12,counter);
}

} // namespace test
