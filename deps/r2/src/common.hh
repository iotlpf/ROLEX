#pragma once

#include <cinttypes>

#include "./utils/option.hh"
#include "./logging.hh"

namespace r2 {

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using i64 = int64_t;
using i32 = int32_t;
using u8 = uint8_t;
using i8 = int8_t;
using usize = unsigned int;

constexpr usize kCacheLineSize = 128;

} // end namespace r2

#pragma once

namespace r2 {

#define DISABLE_COPY_AND_ASSIGN(classname)                                     \
private:                                                                       \
  classname(const classname &) = delete;                                       \
  classname &operator=(const classname &) = delete

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)

#define NOT_INLE __attribute__((noinline))
#define ALWAYS_INLINE __attribute__((always_inline))

static inline void compile_fence(void) { asm volatile("" ::: "memory"); }

static inline void lfence(void) { asm volatile("lfence" ::: "memory"); }

static inline void store_fence(void) { asm volatile("lfence" : : : "memory"); }

static inline void mfence(void) { asm volatile("mfence" : : : "memory"); }

static inline void relax_fence(void) { asm volatile("pause" : : : "memory"); }

#ifndef NO_OPT
/*!
  usage:
  `
  origin func: void xx() { ...}
  no_opt origin func: void NO_OPT xx() {...}
  `
 */
#define NO_OPT  __attribute__((optimize("O0")))
#endif

} // end namespace r2
