#pragma once

#include "../common.hh"

namespace r2 {

namespace ring_msg {

using id_t = u16;
using sz_t = u16;

class IDEncoder {
public:
  static u32 encode_id_sz(const id_t &id, const sz_t &sz) {
    u32 base = static_cast<u32>(id);
    return base << 16 | sz;
  }
};

/**
 * This nice piece of code comes from
 * https://stackoverflow.com/questions/1392059/algorithm-to-generate-bit-mask
 */
template <typename R> static constexpr R bitmask(unsigned int const onecount) {
  return static_cast<R>(-(onecount != 0)) &
         (static_cast<R>(-1) >> ((sizeof(R) * CHAR_BIT) - onecount));
}

class IDDecoder {
public:
  static std::pair<const id_t, const sz_t> decode(const usize &val) {
    usize sz_mask = bitmask<usize>(16);
    return std::make_pair(val >> 16, sz_mask & val);
  }
};

} // namespace ring_msg
} // namespace r2
