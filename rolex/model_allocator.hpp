#pragma once

#include <iostream>
#include <optional>

#include "r2/src/common.hh"
#include "rolex_util.hh"

using namespace r2;

namespace rolex {



template<typename K>
class ModelAllocator{
  char *mem_pool = nullptr;                /// the start memory of the allocated data leaves 
  const u64 total_sz = 0;                  /// the total size of the register memory that we can allocate
  u64 cur_alloc_sz = kUpperModel;          /// the size that has been allocated
  u64 upper_alloc_num = 0;                 /// the number of upper models
  u64 max_upper_num = 0;
public:

  /**
   * @brief Construct ModelAllocator with region m
   * 
   * @param m the start address of the pool
   * @param t the total size of the pool
   */
  explicit ModelAllocator(char *m, const u64 &t) : mem_pool(m), total_sz(t) {
    ASSERT(total_sz > kUpperModel) << "Too small size to store models!";
    cur_alloc_sz = kUpperModel;
    // preserve the first 8byte to indicate how many models are stored
    u64 cur_num = 0;
    memcpy(mem_pool, &cur_num, sizeof(u64));
    upper_alloc_num = 0;

    max_upper_num = std::min((kUpperModel/2-sizeof(u64))/sizeof(K), kUpperModel/2/sizeof(u64));
  }

  // ============ functions for alloc upper/sub models ================
  /**
   * @brief the first kUpperModel/2 store model_keys, the second store model_offsets
   * 
   * @return <ptr of model_key, ptr of model_offsets>
   */
  auto alloc_upper() -> std::pair<char *, char *> {
    if(upper_alloc_num>max_upper_num) {
      ASSERT(false) << "Too small size to store upper models!";
    }
    auto alloc_num = upper_alloc_num++;

    return std::make_pair(mem_pool+sizeof(u64)+sizeof(K)*alloc_num, 
                          mem_pool+sizeof(u64)*alloc_num+kUpperModel/2);
  }

  /**
   * @brief provide an offset for the submodel
   * 
   * @return <ptr, offset> of submodel
   */
  auto alloc_submodel(usize alloc_size) -> std::pair<char*, u64> {
    if (cur_alloc_sz + alloc_size > total_sz) {
      ASSERT(false) << "Too small size to store submodels!";
    }
    auto res = cur_alloc_sz;
    cur_alloc_sz += alloc_size;
    return std::make_pair(mem_pool+res, res);
  }

  // =============== functions to access upper/sub models ==============
  auto get_total_ptr() -> char* { return mem_pool; }

  /**
   * @return <key, off> ptrs of the upper model
   */
  auto get_upper(u64 idx) -> std::pair<char *, char *> {
    return std::make_pair(mem_pool+sizeof(u64)+idx*sizeof(K), 
                          mem_pool+kUpperModel/2+sizeof(u64)*idx);
  }

  auto get_submodel(u64 off) -> char * {
    return mem_pool+off;
  }

};


} // rolex