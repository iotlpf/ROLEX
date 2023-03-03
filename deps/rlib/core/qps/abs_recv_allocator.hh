#pragma once

#include "../rmem/handler.hh"

namespace rdmaio {

namespace qp {

class AbsRecvAllocator {
public:
  /*!
    allocate an RDMA recv buffer,
    which has (ptr, key)
   */
  virtual Option<std::pair<rmem::RMem::raw_ptr_t, rmem::mr_key_t>>
  alloc_one(const usize &sz) = 0;

  /*!
    allocate an memory for the remote; which needs to register this memory
   */
  virtual Option<std::pair<rmem::RMem::raw_ptr_t, rmem::RegAttr>>
  alloc_one_for_remote(const usize &sz) = 0;
};

} // namespace qp
} // namespace rdmaio
