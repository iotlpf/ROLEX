#pragma once

#include <time.h>
#include <sched.h>


namespace rolex {



// preserve 2M space for upper models, which accommodates 131,072 models
constexpr uint64_t kUpperModel = 32 * 1024 * 1024;


// ====== for binary search =========
#define FORCEINLINE __attribute__((always_inline)) inline
// power of 2 at most x, undefined for x == 0
FORCEINLINE uint32_t bsr(uint32_t x) {
  return 31 - __builtin_clz(x);
}

template<typename KEY_TYPE>
static int binary_search_branchless(const KEY_TYPE *arr, int n, KEY_TYPE key) {
//static int binary_search_branchless(const int *arr, int n, int key) {
  intptr_t pos = -1;
  intptr_t logstep = bsr(n - 1);
  intptr_t step = intptr_t(1) << logstep;

  pos = (arr[pos + n - step] < key ? pos + n - step : pos);
  step >>= 1;

  while (step > 0) {
    pos = (arr[pos + step] < key ? pos + step : pos);
    step >>= 1;
  }
  pos += 1;

  return (int) (arr[pos] >= key ? pos : n);
}



} // namespace rolex