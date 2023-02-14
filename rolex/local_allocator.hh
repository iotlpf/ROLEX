#pragma once 

#include "rlib/core/rmem/handler.hh"
#include "rlib/core/common.hh"



using namespace rdmaio;
using namespace rdmaio::rmem;

namespace rolex {


class LocalAllocator {

private:
  RMem::raw_ptr_t buf = nullptr;
  usize total_mem = 0;
  mr_key_t key;
  RegAttr mr;

  usize cur_alloc_off = 0;

  // preserve for leaf reading
  usize leaf_buf_off = 0;

public:
  LocalAllocator(rdmaio::Arc<RMem> mem, const RegAttr &mr)
      : buf(mem->raw_ptr), total_mem(mem->sz), mr(mr), key(mr.key), cur_alloc_off(0) {
    // RDMA_LOG(4) << "simple allocator use key: " << key;
  }

  void reset_for_leaf(const usize leaf_buf_size) {
    leaf_buf_off = 0;
    cur_alloc_off = leaf_buf_size;
  }

  auto get_leaf_buf() -> rmem::RMem::raw_ptr_t { 
    return static_cast<char *>(buf) + leaf_buf_off;
  }

  auto alloc(const usize &sz) -> rmem::RMem::raw_ptr_t {
    if(cur_alloc_off+sz > total_mem) {
      ASSERT(false) << "Local Allocator too small.";
    }
    auto ret = static_cast<char *>(buf) + cur_alloc_off;
    cur_alloc_off += sz;
    return ret;
  }

  

};


} // namespace rolex