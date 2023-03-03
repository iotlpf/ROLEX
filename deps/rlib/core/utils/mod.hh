#pragma once

#include "./timer.hh"
#include <limits.h>

namespace rdmaio {

/*!
  A set of number utilities.
 */
/**
 * This nice code comes from
 * https://stackoverflow.com/questions/1392059/algorithm-to-generate-bit-mask
 */
template <typename R> static constexpr R bitmask(unsigned int const onecount) {
  return static_cast<R>(-(onecount != 0)) &
         (static_cast<R>(-1) >> ((sizeof(R) * CHAR_BIT) - onecount));
}
} // namespace rdmaio
