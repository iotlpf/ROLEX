#pragma once

#include <limits>

#include "../common.hh"
#include "./channel.hh"

#include "../utils/marshal.hh"

namespace rdmaio {

namespace bootstrap {

/*!
  MultiMsg ecnodes multiple msgs into one, merged msg.

  \note: MaxMsgSz supported is std::numeric_limits<u16>::max()
  \note: MAx number of msg supported ius defiend in kMaxmultiMsg
  ---

  Usage:
  `
  MultiMsg<1024> mss; // create a MultioMsg with total 1024-bytes capacity.

  ByteBuffer one_msg = ...;
  ByteBuffer another_msg = ...;

  assert(mss.append(one_msg)); // false if there is no capacity
  assert(mss.append(another_msg)); //

  ByteBuffer &total_msg = mss.buf;

  // do something about the total_msg;
  `
 */

// max encoded msg per MultiMsg
const usize kMaxMultiMsg = 8;

struct __attribute__((packed)) MsgEntry {
  u16 offset = 0;
  u16 sz = 0;

  static usize max_entry_sz() { return std::numeric_limits<u16>::max(); }
};

struct __attribute__((packed)) MsgsHeader {
  u8 num = 0;
  MsgEntry entries[kMaxMultiMsg];

  bool has_free_entry() const { return num < kMaxMultiMsg; }

  /*!
    append one msg to the header, return its current offset
   */
  bool append_one(u16 sz) {
    if (!has_free_entry())
      return false;

    u16 off = sizeof(MsgsHeader);
    if (num != 0) {
      off = entries[num - 1].offset + entries[num - 1].sz;
    } else {
      // handles nothing, off should be zero
    }
    entries[num++] = {.offset = off, .sz = sz};
    return true;
  }

  // sanity check that the header content is consistent
  bool sanity_check(usize sz) const {

    if (num > kMaxMultiMsg) {
      return false;
    }

    u16 cur_off = sizeof(MsgsHeader);

    for (uint i = 0; i < num; ++i) {
      if (entries[i].offset != cur_off) {
        return false;
      }
      cur_off += entries[i].sz;
    }
    return (static_cast<usize>(cur_off) <= sz);
  }
};

/*!
  MultiMsg encodes several ByteBuffer into one single one,
  and we can decode these buffers separately.
  Note that at most *kMaxMultiMsg* can be encoded.
 */
template <usize MAXSZ = kMaxMsgSz> struct MultiMsg {
  MsgsHeader *header;
  ByteBuffer *buf = nullptr;

  // whether this msg alloc from the heap
  bool        alloc = false;

  static_assert(MAXSZ > sizeof(MsgsHeader), "MultiMsg reserved sz too small");
  explicit MultiMsg(const usize &reserve_sz = MAXSZ)
    : buf(new ByteBuffer(::rdmaio::Marshal::dump_null<MsgsHeader>())) {
    init(reserve_sz);
  }

  ~MultiMsg() {
    if (alloc)
      delete buf;
  }

  /*!
    Create from an existing MultiMsg
   */
  static Option<MultiMsg<MAXSZ>> create_from(ByteBuffer &b) {
    if (b.size() > MAXSZ) {
      RDMA_LOG(2) << "size mis match";
      return {};
    }
    auto res = MultiMsg<MAXSZ>(b);

    // do sanity checks, if one failes, then return false
    if (!res.header->sanity_check(res.buf->size())) {
      return {};
    }
    return res;
  }

  /*!
    Create a MultiMsg with exact payload (sz)
    Failes if sz + sizeof(MsgsHeader) > MAXSZ
   */
  static Option<MultiMsg<MAXSZ>> create_exact(const usize &sz) {
    if (sz + sizeof(MsgsHeader) > MAXSZ)
      return {};
    return MultiMsg<MAXSZ>(sz + sizeof(MsgsHeader));
  }

  /*!
    \ret:
     - true: a msg has been appended to the MultiMsg
     - false: this can because:
        + there is no free space for the msg (current msgs has occupied more
    than MAXSZ sz)
        + there is no free entry for the msg (there already be kMaxMultimsg
    emplaced)
   */
  bool append(const ByteBuffer &msg) {
    if (buf->size() + msg.size() > MAXSZ) {
      return false;
    }
    if (!this->header->append_one(static_cast<u16>(msg.size())))
      return false;
    buf->append(msg);
    return true;
  }

  // the following is the querier for the msg
  usize num_msg() const { return static_cast<usize>(header->num); }

  /*!
    Get one msg from the multimsg
    \note: performance may be bad
   */
  Option<ByteBuffer> query_one(const usize &idx) const {
    if (idx >= num_msg())
      return {};
    MsgEntry &entry = header->entries[idx];
    return ByteBuffer(buf->data() + entry.offset, entry.sz);
  }

private:
  /*!
    create a multimsg from a buffer
    fill the header and the buf
   */
  explicit MultiMsg(ByteBuffer &total_msg) : buf(&total_msg) {
    //init(total_msg.size());
    { // unsafe code
      this->header = (MsgsHeader *)(buf->data());
    }
  }

  MultiMsg(const ByteBuffer &m, usize reserve_sz) : buf(&m) { init(reserve_sz); }

  void init(const usize &reserve_sz) {
    usize true_reserve_sz = std::min(
        std::max(reserve_sz, static_cast<usize>(sizeof(MsgsHeader))), MAXSZ);
    buf->reserve(true_reserve_sz);
    { // unsafe code
      this->header = (MsgsHeader *)(buf->data());
    }
  }
};

} // namespace bootstrap
} // namespace rdmaio
