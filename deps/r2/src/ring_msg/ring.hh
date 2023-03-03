#pragma once

#include "rlib/core/rmem/handler.hh"

#include "../mem_block.hh"

namespace r2 {

namespace ring_msg {

using namespace rdmaio::rmem;

/*!
  Remote ring store remote ring buffer information
 */
template <usize kRingSz> struct RemoteRing {
  usize tailer = 0;
  usize base_addr = 0;
  u32 mem_key = 0;

  explicit RemoteRing(const usize &base_addr, const u32 &key)
      : base_addr(base_addr), mem_key(key) {}

  RemoteRing() = default;

  usize next_addr(const usize &sz) {
    auto ret = tailer;
    tailer = (tailer + sz) % kRingSz;
    return ret + base_addr;
  }
};

/*!
  Local ring store local ring information
  R: The size of the ring buffer
 */
template <usize R>
struct LocalRing {
  MemBlock local_mem;
  usize tailer = 0;
  usize header = 0;

  explicit LocalRing(const MemBlock &mem) : local_mem(mem) {
    ASSERT(mem.sz >= R);
  }

  LocalRing() : LocalRing(MemBlock(nullptr, 0)) {}
  ~LocalRing() = default;

  inline Option<MemBlock> cur_msg(const usize &sz) {
    if (unlikely(sz + tailer > local_mem.sz)) {
      return {};
    }
    auto temp = increment_tailer(sz);
    return MemBlock((char *)(local_mem.mem_ptr) + temp, sz);
  }

  u64 convert_to_rdma_addr(const RegAttr &attr) const {
    // invariant checks
    ASSERT((u64)attr.buf <= (u64)local_mem.mem_ptr);
    ASSERT((u64)local_mem.mem_ptr + local_mem.sz <=
           (u64)attr.buf + (u64)attr.sz);
    return (u64)local_mem.mem_ptr - (u64)attr.buf;
  }

private:
  // wo check, so it's private
  usize increment_tailer(const usize &sz) {
    auto cur_tailer = tailer;
    tailer = (tailer + sz) % R;
    return cur_tailer;
  }
};
} // namespace ring_msg

} // namespace r2
