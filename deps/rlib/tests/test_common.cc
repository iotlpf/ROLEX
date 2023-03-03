#include <gtest/gtest.h>

#include "../core/common.hh"

TEST(RDMAIO, Common) {
  using namespace rdmaio;

  auto result = Ok();
  ASSERT_EQ(result.code.c, IOCode::Ok);
  ASSERT_EQ(result.code.name(), "Ok");
  auto res = (result.code == IOCode::Ok);
  ASSERT_TRUE(res);
}
