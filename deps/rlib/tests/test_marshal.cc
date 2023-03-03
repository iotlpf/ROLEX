#include <gtest/gtest.h>

#include "../core/utils/marshal.hh"

using namespace rdmaio;

TEST(Marshal,basic) {

  // init the test buffer
  usize test_sz = 12;

  ByteBuffer test_buf = Marshal::alloc(test_sz);
  for(u8 i = 0;i < test_sz;++i) {
    //Marshal::safe_set_byte(test_buf,i,i);
    test_buf[i] = i;
  }

  auto res = Marshal::forward(test_buf, 0);
  usize count = 0;
  while(res && count < test_sz) {

    ASSERT_EQ(res.value()[0],count);
    count += 1;

    res = Marshal::forward(res.value(), 1);
  }
  ASSERT_EQ(count, test_sz);
}


TEST(Marshal,dedump) {
  u64 test_val = 73;
  ByteBuffer buf = Marshal::dump(test_val);
  auto dedumped_val = Marshal::dedump<u64>(buf).value();
  ASSERT_EQ(dedumped_val,test_val);
}
