#pragma once

#include "rlib/core/qps/rc.hh"
#include "rlib/core/qps/recv_helper.hh"

#include "../mem_block.hh"

#include "./session.hh"

namespace r2 {

namespace ring_msg {

using namespace rdmaio;
using namespace rdmaio::qp;

template <usize R> class RecvBundler {
  // structure for post_recvs
  usize idle_recv_entries = 0;

public:
  Arc<RecvEntries<R>> recv_entries;

  explicit RecvBundler(Arc<AbsRecvAllocator> &alloc_p)
      : recv_entries(RecvEntriesFactoryv2<R>::create(alloc_p, 0)) {}

  explicit RecvBundler(Arc<RecvEntries<R>> ent) : recv_entries(ent) {}

  Result<int> consume_one(Arc<RC> &qp) {

    const usize poll_recv_step = R / 2;
    //const usize poll_recv_step = 32;
    static_assert(poll_recv_step > 0, "");

    idle_recv_entries += 1;
    if (idle_recv_entries >= poll_recv_step) {
      auto ret = qp->post_recvs(*recv_entries, idle_recv_entries);
      ASSERT(ret == IOCode::Ok);
      idle_recv_entries = 0;
      return ret;
    }
    return ::rdmaio::Ok(0);
  }
};

} // namespace ring_msg
} // namespace r2
