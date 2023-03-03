#pragma once

#include "rlib/core/bootstrap/proto.hh"

namespace r2 {

namespace ring_msg {

using namespace rdmaio;
using namespace rdmaio::qp;
using namespace rdmaio::proto;
using namespace rdmaio::rmem;

const u32 kMagic = 73;

struct __attribute__((packed)) RingReply {
  RCReply rc_reply;
  u64 base_off;
  RegAttr mr;
};

struct __attribute__((packed)) RingBootstrap {
  u64 base_off;
  RegAttr mr;
  u32     magic_key = kMagic;
};

} // namespace ring_msg

} // namespace r2
