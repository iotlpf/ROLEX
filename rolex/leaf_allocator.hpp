#pragma once

#include <iostream>
#include <optional>


#include "xutils/marshal.hh"
#include "xutils/spin_lock.hh"
#include "r2/src/common.hh"

using namespace r2;


namespace rolex {


/**
 * @brief Currently, it incurs high overhead when the compute nodes allcate memory on memory nodes
 *        So, we preallocate the data leave
 * 
 * @tparam Leaf the type that we allocate
 * @tparam S the size of a data leaf
 */
template <typename Leaf, usize S>
class LeafAllocator {
  ::xstore::util::SpinLock lock;
  char *mem_pool = nullptr;      /// the start memory of the allocated data leaves 
  const u64 total_sz = 0;        /// the total size of the register memory that we can allocate
  u64 cur_alloc_sz = 0;          /// the size that has been allocated

public:
  usize cur_alloc_num = 0;

  /**
   * @brief Construct LeafAllocator with region m;
   *           preallcote leaves and preserve 2*sizeof(u64) for [used, total]
   * 
   * @param m the start address of the pool
   * @param t the total size of the pool
   */
  explicit LeafAllocator(char *m, const u64 &t) : mem_pool(m), total_sz(t) {
    prealloc_leaves((total_sz-sizeof(u64))/S);
  }

  /**
   * @brief Construct LeafAllocator with region m; preallocate leaves with the given leaf_num
   *  
   * @param leaf_num the predefined leaf_num
   */
  explicit LeafAllocator(char *m, const u64 &t, const u64 &leaf_num) : mem_pool(m), total_sz(t) {
    LOG(3) << "leaf_num: "<<leaf_num<<" , (total_sz-sizeof(u64))/S): "<<(total_sz-sizeof(u64))/S;
    ASSERT(leaf_num<(total_sz-sizeof(u64))/S) << "Small leaf region for allocating "<<(total_sz-sizeof(u64))/S;
    prealloc_leaves(leaf_num);
  }

  inline auto used_num() -> u64 {
    return ::xstore::util::Marshal<u64>::deserialize(mem_pool, sizeof(u64));
  }

  inline auto allocated_num() -> u64 {
    return ::xstore::util::Marshal<u64>::deserialize(mem_pool+sizeof(u64), sizeof(u64));
  }

  // =========== access the leaf ============
  auto get_leaf(u64 num) -> char * {
    ASSERT(num <= used_num()) << "Exceed the Preallocated leaves. [used_num:num] " << used_num() <<" " << num;
    return mem_pool + 2*sizeof(u64) + num*S;
  }

  // ============ allocate leaves ===============
  auto fetch_new_leaf() -> std::pair<char *, u64> {
    u64 num = fetch_and_add();
    // if(num>=allocated_num()) 
    //   LOG(2) <<"Preallocated " <<allocated_num()<< " leaves are insufficient for num: "<<num << ", key "<<key;
    ASSERT(num < allocated_num()) << "Preallocated " <<allocated_num()<< " leaves are insufficient for num: "<<num;
    return {mem_pool + 2*sizeof(u64) + num*S, num};
  }
  

private:
  auto alloc() -> ::r2::Option<char *> {
    lock.lock();
    if (cur_alloc_sz + S <= total_sz) {
      // has free space
      auto res = mem_pool + cur_alloc_sz;
      cur_alloc_sz += S;
      cur_alloc_num += 1;
      lock.unlock();
      return res;
    }
    lock.unlock();
    LOG(4) << cur_alloc_sz << " " << total_sz;
    return {};
  }

  auto dealloc(char *data) { ASSERT(false) << "not implemented"; }  


  //  ======= Preallocate leaves to store data ============
  void prealloc_leaves(u64 leaf_num) {
    // 1.init the metadata (current number of the leaf, total number of allocated leaves)
    u64 cur_num = 0;
    ASSERT(cur_alloc_sz==0) << "LeafAllocator has been used since cur_alloc_sz != 0.";
    memcpy(mem_pool, &cur_num, sizeof(u64));
    cur_alloc_sz += sizeof(u64);
    memcpy(mem_pool+cur_alloc_sz, &leaf_num, sizeof(u64));
    cur_alloc_sz += sizeof(u64);
    // 2.preallocate the data leaves
    for(int i=0; i<leaf_num; i++) {
      Leaf* cur_leaf = new (reinterpret_cast<Leaf*>(alloc().value())) Leaf();
    }
    LOG(3) << "Preallocate leaf number--> [used, total]: [" 
           << ::xstore::util::Marshal<u64>::deserialize(mem_pool, sizeof(u64)) << ", "
           <<::xstore::util::Marshal<u64>::deserialize(mem_pool+sizeof(u64), sizeof(u64))<<"]";
  }

  /**
   * @brief Obtain the number of current ideal leaves and add the number with 1
   *          this function is used for memory node, rather than the compute node
   * 
   * @return u64 the number of current ideal leaves
   */
  auto fetch_and_add() -> u64 {
    lock.lock();
    auto res = ::xstore::util::Marshal<u64>::deserialize(mem_pool, sizeof(u64));
    auto res_add = res+1;
    // LOG(2) << "now used "<<res_add;
    // ASSERT(res_add != 0) << "fetch_and_add, before add 1: "<<res<< ", add 1: "<<res_add;
    memcpy(mem_pool, &(res_add), sizeof(u64));
    auto read_res = ::xstore::util::Marshal<u64>::deserialize(mem_pool, sizeof(u64));
    ASSERT(res_add == read_res) << "fetch_and_add, affer add 1: "<<res_add<< ", read_res: "<<read_res;
    lock.unlock();
    return res;
  }

};



} // namespace rolex