#pragma once

#include "./abs_recv_allocator.hh"

namespace rdmaio {

namespace qp {

/*!
  helper data struture for two-sided QP recv
 */
template <usize N> struct RecvEntries {
  /*!
   internal data structure used for send/recv verbs
  */
  struct ibv_recv_wr rs[N];
  struct ibv_sge sges[N];
  struct ibv_wc wcs[N];

  /*!
    current recv entry which points to one position in *rs*
  */
  usize header = 0;

  ibv_recv_wr *wr_ptr(const usize &idx) {
    // idx should be in [0,N)
    return rs + idx;
  }

  ibv_recv_wr *header_ptr() { return wr_ptr(header); }

  void sanity_check() {
    for (uint i = 0; i < N - 1; ++i) {
      RDMA_ASSERT((u64)(wr_ptr(i)->next) == (u64)(wr_ptr(i + 1)));
      RDMA_ASSERT((u64)(wr_ptr(i)->sg_list) == (u64)(&sges[i]));
      RDMA_ASSERT(wr_ptr(i)->num_sge == 1);
    }
  }
};

// AbsAllocator must inherit from *AbsRecvAllocator* defined in
// abs_recv_allocator.hh
template <class AbsAllocator, usize N, usize entry_sz>
class RecvEntriesFactory {
public:
  static Arc<RecvEntries<N>> create(AbsAllocator &allocator) {

    Arc<RecvEntries<N>> ret(new RecvEntries<N>);

    for (uint i = 0; i < N; ++i) {
      auto recv_buf = allocator.alloc_one(entry_sz).value();

      struct ibv_sge sge = {
          .addr = reinterpret_cast<uintptr_t>(std::get<0>(recv_buf)),
          .length = static_cast<u32>(entry_sz),
          .lkey = std::get<1>(recv_buf)};

      { // unsafe code
        ret->rs[i].wr_id = sge.addr;
        ret->rs[i].sg_list = &(ret->sges[i]);
        ret->rs[i].num_sge = 1;
        ret->rs[i].next = (i < N - 1) ? (&(ret->rs[i + 1])) : (&(ret->rs[0]));

        ret->sges[i] = sge;
      }
    }
    return ret;
  }
};

/*!
  This version only uses one template: N
 */
template <usize N> class RecvEntriesFactoryv2 {
public:
  static Arc<RecvEntries<N>> create(Arc<AbsRecvAllocator> &alloc_p,
                                    const usize &msg_sz) {

    Arc<RecvEntries<N>> ret(new RecvEntries<N>);

    for (uint i = 0; i < N; ++i) {
      auto recv_buf = alloc_p->alloc_one(msg_sz).value();

      struct ibv_sge sge = {
          .addr = reinterpret_cast<uintptr_t>(std::get<0>(recv_buf)),
          .length = static_cast<u32>(msg_sz),
          .lkey = std::get<1>(recv_buf)};

      { // unsafe code
        ret->rs[i].wr_id = sge.addr;
        ret->rs[i].sg_list = &(ret->sges[i]);
        ret->rs[i].num_sge = 1;
        ret->rs[i].next = (i < N - 1) ? (&(ret->rs[i + 1])) : (&(ret->rs[0]));

        ret->sges[i] = sge;
      }
    }
    return ret;
  }
};

} // namespace qp
} // namespace rdmaio
