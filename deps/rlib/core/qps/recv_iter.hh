#pragma once

#include "./recv_helper.hh"

namespace rdmaio {

namespace qp {

/*!
  RecvIter helps traversing two-sided messages, using recv_cq.

  \note:  we donot store the smart pointer for the performance reason

  Example:
  `
  Arc<UD> ud; // we have an UD
  RecvEntries<12> entries;

  for(RecvIter iter(ud, entries); iter.has_msgs(); iter.next()) {
    auto imm_data, msg_buf = iter.cur_msg(); // relax some syntax to fetch pair
    // do with imm_data and msg_buf
  }
  `
 */
template <typename QP, usize es> class RecvIter {
  QP *qp = nullptr;
  RecvEntries<es> *entries = nullptr;
  ibv_wc *wcs;

  int idx = 0;
  int total_msgs = -1;
  static int total;

public:
  RecvIter() = default;

  RecvIter(ibv_cq *cq, ibv_wc *wcs)
      : wcs(wcs), total_msgs(ibv_poll_cq(cq, es, wcs)) {}

  RecvIter(Arc<QP> &qp, ibv_wc *wcs)
      : qp(qp.get()), wcs(wcs), total_msgs(ibv_poll_cq(qp->recv_cq, es, wcs)) {}

  RecvIter(Arc<QP> &qp, Arc<RecvEntries<es>> &e) : RecvIter(qp, e->wcs) {
    entries = e.get();
  }

  auto set_meta(Arc<QP> &qp, Arc<RecvEntries<es>> &e) {
    this->entries = e.get();
    this->qp  = qp.get();
    this->wcs = e->wcs;
  }

  void begin(Arc<QP> &qp, ibv_wc *wcs) {
    this->total_msgs = ibv_poll_cq(qp->recv_cq, es, wcs);
    this->idx = 0;
  }

  /*!
    \ret (imm_data, recv_buffer)
    */
  Option<std::pair<u32, rmem::RMem::raw_ptr_t>> cur_msg() const {
    if (has_msgs()) {
      auto buf = reinterpret_cast<rmem::RMem::raw_ptr_t>(wcs[idx].wr_id);
      return std::make_pair(wcs[idx].imm_data, buf);
    }
    return {};
  }

  inline void next() { idx += 1; }

  inline bool has_msgs() const { return idx < total_msgs; }

  void clear() {
    if (total_msgs > 0 && qp != nullptr && entries != nullptr) {
      auto res = qp->post_recvs(*entries, total_msgs);
      if (unlikely(res != IOCode::Ok))
        RDMA_LOG(4) << "post recv error: " << strerror(res.desc);
    }
    this->total_msgs = -1;
  }

  ~RecvIter() {
    // post recvs
    this->clear();
  }
};

} // namespace qp
} // namespace rdmaio
