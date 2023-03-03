#include <gtest/gtest.h>

#include "../src/linked_list.hh"
#include "../src/logging.hh"

using namespace r2;

namespace test {

TEST(List, Basic) {

  LinkedList<int> test;
  for(uint i = 0;i < 12;++i){
    test.add(new Node<int>(i));
    //LOG(2) << "tailer's value " << test.tailer_p->val << "; prev: " << test.tailer_p->prev_p->val;
  }

  for (uint i = 0;i < 12;++i) {
    auto n = test.peek().value();
    ASSERT_EQ(n->val,i);
    delete n;
  }
}

} // namespace test
