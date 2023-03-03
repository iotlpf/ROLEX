#include <gtest/gtest.h>

#include "../src/utils/rdtsc.hh"

using namespace r2;

namespace test {

TEST(Util, Rdtsc) {
  RDTSC rdtsc;
  sleep(1);
  ASSERT_GE(rdtsc.passed(), 0);
}
} // namespace test
