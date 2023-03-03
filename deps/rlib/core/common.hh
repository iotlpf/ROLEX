/**
 * This file provides common utilities and definiation of RLib
 */

#pragma once

#include <cstdint>
#include <infiniband/verbs.h>
#include <tuple>
#include <memory>

#include "./utils/logging.hh"
#include "./utils/option.hh"

#include "./result.hh"

namespace rdmaio {

// some handy integer defines
using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using i64 = int64_t;
using u8 = uint8_t;
using i8 = int8_t;
using usize = unsigned int;

// some handy alias for smart pointer
template <typename T>
using Arc = std::shared_ptr<T>;

#ifndef DISABLE_COPY_AND_ASSIGN
#define DISABLE_COPY_AND_ASSIGN(classname)                                     \
private:                                                                       \
  classname(const classname &) = delete;                                       \
  classname &operator=(const classname &) = delete
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

static inline void compile_fence(void) { asm volatile("" ::: "memory"); }

} // namespace rdmaio
