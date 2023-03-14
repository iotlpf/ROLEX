#include <gtest/gtest.h>

#include "rolex/leaf_table.hpp"
#include "rolex/trait.hpp"
#include "rolex/remote_memory.hh"

using namespace rolex;

namespace test {

TEST(LeafTable, leaf_table) {
  leaf_table_t ltable;
  ltable.train_emplace_back(0);
  ltable.train_emplace_back(1);
  ltable.lock_leaf(1);

  ASSERT_EQ(ltable.table[1].lock, 1);
  ASSERT_EQ(ltable.table[0].lock, 0);

  ltable.synonym_emplace_back(true, 1, 3);
  ASSERT_EQ(ltable.table[1].synonym_leaf, 1);
  ASSERT_EQ(ltable.SynonymTable[1].leaf_num, 3);

  ltable.synonym_emplace_back(false, 1, 5);
  ASSERT_EQ(ltable.SynonymTable[1].synonym_leaf, 2);
  ASSERT_EQ(ltable.SynonymTable[2].leaf_num, 5);

  ltable.unlock_leaf(1);
  ASSERT_EQ(ltable.table[1].lock, 0);
}


TEST(LeafTable, leaf_data) {
  const usize MB = 1024 * 1024;
  const usize leaf_num = 100;
  RCtrl* ctrl = new RCtrl(8888);
  RM_config conf(ctrl, 1024 * MB, leaf_num*sizeof(leaf_t), 0, leaf_num);
  remote_memory_t* RM = new remote_memory_t(conf);
  auto alloc = RM->leaf_allocator();

  auto res = alloc->fetch_new_leaf();
  leaf_table_t ltable;
  ltable.train_emplace_back(res.second);
  leaf_t* cur_leaf = reinterpret_cast<leaf_t*>(res.first);
  for(int i=0; i<5; i++) cur_leaf->insert_not_full(i, i);
  for(int i=30; i<39; i++) {
    if(cur_leaf->isfull()){
      res = alloc->fetch_new_leaf();
      ltable.train_emplace_back(res.second);
      cur_leaf = reinterpret_cast<leaf_t*>(res.first);
    }
    cur_leaf->insert_not_full(i, i);
  }
  for(int i=10; i<21; i++){
    ltable.insert(i, i, alloc, 0, 0);
  }
  
  ltable.print(alloc);
}




}
