#pragma once

#include <sys/mman.h>

#include "memory_region.hh"

namespace rolex {

using namespace rdmaio;

/*!
  Malloc huge pages in 2M huge pages, align_sz = 2M
 */
class HugeRegion : public MemoryRegion {
  static u64 align_to_sz(const u64 &x, const usize &align_sz) {
    return (((x) + align_sz - 1) / align_sz * align_sz);
  }

public:
  static ::rdmaio::Option<rdmaio::Arc<HugeRegion>>
  create(const u64 &sz, const usize &align_sz = (2 << 20)) {
    auto region = std::make_shared<HugeRegion>(sz, align_sz);
    if (region->valid())
      return region;
    return {};
  }

  explicit HugeRegion(const u64 &sz, const usize &align_sz = (2 << 20)) {

    this->sz = align_to_sz(sz, align_sz);
    char *ptr = (char *)mmap(
        nullptr, this->sz, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB, -1, 0);

    if (ptr == MAP_FAILED) {
      this->addr = nullptr;
      RDMA_LOG(4) << "error allocating huge page wiht sz: " << this->sz
                  << " aligned with: " << align_sz
                  << "; with error: " << strerror(errno);
    } else {
      RDMA_LOG(4) << "alloc huge page size: " << this->sz << ", "<<this->sz/align_sz<<" * 2M";
      this->addr = ptr;
    }
  }
};

} // namespace rolex

