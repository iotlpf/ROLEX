#pragma once

#include "../common.hh"
#include "../naming.hh"
#include "../nic.hh"

#include "../utils/mod.hh"
#include "../utils/abs_factory.hh"

#include "./recv_helper.hh"

namespace rdmaio {

namespace qp {

const usize kMaxInlinSz = 64;

using ProgressMark_t = u16;
// Track the out-going and acknowledged reqs
struct Progress {
  static constexpr const u32 num_progress_bits = sizeof(ProgressMark_t) * 8;

  ProgressMark_t high_watermark = 0;
  ProgressMark_t low_watermark = 0;

  ProgressMark_t forward(ProgressMark_t num) {
    high_watermark += num;
    return high_watermark;
  }

  void done(int num) { low_watermark = num; }

  ProgressMark_t pending_reqs() const {
    if (high_watermark >= low_watermark)
      return high_watermark - low_watermark;
    return std::numeric_limits<ProgressMark_t>::max() -
           (low_watermark - high_watermark) + 1;
  }
};

/*!
  Below structures are make packed to allow communicating
  between servers
 */
struct __attribute__((packed)) QPAttr {
  RAddress addr;
  u64 lid;
  u64 psn;
  u64 port_id;
  u64 qpn;
  u64 qkey;
};

class Dummy {
public:
  struct ibv_qp *qp = nullptr;
  struct ibv_cq *cq = nullptr;
  struct ibv_cq *recv_cq = nullptr;

  // #of outsignaled RDMA requests
  usize out_signaled = 0;

  Arc<RNic> nic;

  ~Dummy() {
    // some clean ups
    if(qp) {
      int rc = ibv_destroy_qp(qp);
      RDMA_VERIFY(WARNING, rc == 0)
          << "Failed to destroy QP " << strerror(errno);
    }
  }

  virtual QPAttr my_attr() const = 0;

  explicit Dummy(Arc<RNic> nic) : nic(nic) {}

  bool valid() const { return qp != nullptr && cq != nullptr; }

  inline usize ongoing_signaled() const { return out_signaled; }

  /*!
    Send the requests specificed by the sr
    \ret: errno

    \note: this message do **nothing** to check the parameters
   */
  Result<int> send(ibv_send_wr &sr, const usize &num,ibv_send_wr **bad_sr) {
    auto rc = ibv_post_send(qp, &sr, bad_sr);
    if (rc == 0) {
      // ok
      return Ok(0);
    }
    return Err(errno);
  }

  /*!
    Poll one completion from the send_cq
   */
  inline std::pair<int,ibv_wc> poll_send_comp() {
    ibv_wc wc;
    auto poll_result = ibv_poll_cq(cq, 1, &wc);
    if (poll_result > 0)
      out_signaled -= 1;
    return std::make_pair(poll_result,wc);
  }

  static std::string wc_status(const ibv_wc &wc) {
    return std::string(ibv_wc_status_str(wc.status));
  }

  /*!
    do a loop to poll one completion from the send_cq.
    \note timeout is measured in microseconds
   */
  Result<ibv_wc> wait_one_comp(const double &timeout = ::rdmaio::Timer::no_timeout()) {
    Timer t;
    std::pair<int,ibv_wc> res;
    do {
      // poll one comp
      res = poll_send_comp();
    } while (res.first == 0 && // poll result is 0
             t.passed_msec() <= timeout);
    if(res.first == 0)
      return Timeout(res.second);
    if(unlikely(res.first < 0 || res.second.status != IBV_WC_SUCCESS))
      return Err(res.second);
    return Ok(res.second);
  }

  // below is handy helper functions for common QP operations
  /**
   * return whether qp is in {INIT,READ_TO_RECV,READY_TO_SEND} states
   */
  Result<ibv_qp_state> qp_status() const {
    if (!valid()) {
      return Err(IBV_QPS_RTS); // note it is just a dummy value, because there
                               // is already an error
    }
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    RDMA_ASSERT(ibv_query_qp(qp, &attr, IBV_QP_STATE, &init_attr) == 0);
    return Ok(attr.qp_state);
  }

  /*!
  Post *num* recv entries to the QP, start at entries.header

  \ret
  - Err: errono
  - Ok: -
 */
  template <usize entries>
  Result<int> post_recvs(RecvEntries<entries> &r, int num) {

    auto tail = r.header + num - 1;
    if (tail >= entries)
      tail -= entries;
    auto temp = std::exchange((r.rs + tail)->next, nullptr);

    // really post the recvs
    struct ibv_recv_wr *bad_rr;
    auto rc = ibv_post_recv(this->qp, r.header_ptr(), &bad_rr);

    if (rc != 0)
      return Err(errno);

    // re-set the header, tailer
    r.wr_ptr(tail)->next = temp;
    r.header = (tail + 1) % entries;

    

    return Ok(0);
  }
};

} // namespace qp

} // namespace rdmaio

#include "rc.hh"
#include "ud.hh"
//#include "op.hh"

namespace rdmaio {
namespace qp {

class RC;

// shall we use a string to identify QPs?
using register_id_t = u64;
using RCFactory = Factory<register_id_t, RC>;

using QPFactory = Factory<std::string, Dummy>;

const usize kMaxQPNameLen = 63; // the name to index a QP in Factory should be less than 63 bytes

} // namespace qp
} // namespace rdmaio
