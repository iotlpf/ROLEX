#pragma once

#include "../common.hh"

namespace r2 {

class RDTSC {
public:
  static inline u64 read_tsc(void) {
    u32 a, d;
    __asm __volatile("rdtsc" : "=a"(a), "=d"(d));
    return ((u64)a) | (((u64)d) << 32);
  }

  RDTSC() : start(read_tsc()) {}
  u64 passed() const { return read_tsc() - start; }

private:
  u64 start;
};

} // namespace r2
