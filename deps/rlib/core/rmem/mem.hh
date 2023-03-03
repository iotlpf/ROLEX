#pragma once

#include <functional>
#include <memory>

#include "../common.hh"

namespace rdmaio {

namespace rmem {

/*!
  An abstract memory handler of registered RDMA memory
  usage:
  auto mem = Arc<RMem>(1024);
  assert(mem->valid());

  RNic rnic(...);
  // then use it to register a handler
  RegHandler reg(mem,rnic);
  Option<RegAttr> attr = reg.get_reg_attr();

  Note, we recommend to use Arc for RegHandler and RNic,
  so that resources can be automatically freed.
 */
struct RMem : public std::enable_shared_from_this<RMem>  { // state for "R"egistered "Mem"ory
  using raw_ptr_t = void *;
  using alloc_fn_t = std::function<raw_ptr_t(u64)>;
  using dealloc_fn_t = std::function<void(raw_ptr_t)>;

  /*!
    Fixme: hwo to automatically dealloc the memory ?
   */
  raw_ptr_t raw_ptr;
  const u64 sz;

  dealloc_fn_t dealloc_fn;

  explicit RMem(const u64 &s,
                alloc_fn_t af = [](u64 s) -> raw_ptr_t {
                  return (raw_ptr_t)malloc(s);
                },
                dealloc_fn_t df = [](raw_ptr_t p) { free(p); })
      : raw_ptr(af(s)), sz(s), dealloc_fn(df) {}

  bool valid() const { return raw_ptr != nullptr; }

  ~RMem() {
    dealloc_fn(raw_ptr);
  }
};

} // namespace rmem

} // namespace rdmaio
