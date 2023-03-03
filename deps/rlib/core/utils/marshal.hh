#pragma once

#include <string>

#include "../common.hh"

namespace rdmaio {

using ByteBuffer = std::string;

/*!
  A simple, basic use of helper methods for marshaling/unmarshaling data from
  byte buffer.
  A bytebuffer is basically a c++ string. Any alternative containiner is also
  suitable.

  Example:
  1. get a u8 buf initialized with 0.
    ` auto buf = Marshal::alloc(size);`
  2. Dump a struct to a buf, and dedump it to get it.
  ` struct __attribute__((packed)) A { // note struct must be packed
      u8 a;
      u64 b;
      };
    A temp;
    ByteBuffer buf = Marshal::dump(temp);
    assert(Marshal::dedump(buf).value() == temp); // note this is an option.
    `
 */
class Marshal {
public:
  static ByteBuffer alloc(usize size) { return ByteBuffer(size, '0'); }

  /*!
    Subtract the current buffer at offset.
    If offset is smaller than the buf.size(), then return an ByteBuffer.
    Otherwise, return {}.
   */
  static Option<ByteBuffer> forward(const ByteBuffer &buf, usize off) {
    if (off < buf.size()) {
      return Option<ByteBuffer>(buf.substr(off, buf.size() - off));
    }
    return {};
  }

  static bool safe_set_byte(ByteBuffer &buf, usize off, u8 data) {
    if (off < buf.size()) {
      *((u8 *)(buf.data() + off)) = data; // unsafe code
      return true;
    }
    return false;
  }

  // dump a type with its default constructor
  template <typename T> static ByteBuffer dump_null() {
    T t;
    return dump<T>(t);
  }

  template <typename T> static ByteBuffer dump(const T &t) {
    auto buf = alloc(sizeof(T));
    memcpy((void *)buf.data(), &t, sizeof(T)); // unsafe code
    return buf;
  }

  template <typename T> static Option<T> dedump(const ByteBuffer &b) {
    if (b.size() >= sizeof(T)) {
      T res;
      memcpy((char *)(&res), b.data(), sizeof(T));
      return Option<T>(res);
    }
    return {};
  }
};

} // namespace rdmaio
