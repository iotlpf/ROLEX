#pragma once

#include "../libroutine.hh"
#include "rlib/core/qps/op.hh"

namespace r2 {

namespace rdma {

using namespace rdmaio;
using namespace rdmaio::qp;

template <usize NSGE = 1>
struct AsyncOp : Op<NSGE> {
  double timeout_usec = 1000000;

 public:
  AsyncOp() = default;

  inline AsyncOp &set_timeout_usec(const double &tu) {
    this->timeout_usec = tu;
    return *this;
  }

  inline auto execute_async(const Arc<RC> &qp, int flags, R2_ASYNC)
      -> Result<ibv_wc> {
    ibv_wc wc;
    auto ret_s = this->execute(qp, flags, R2_COR_ID());
    if (unlikely(ret_s != IOCode::Ok)) return ::rdmaio::transfer(ret_s, wc);

    if (flags & IBV_SEND_SIGNALED) {
      return wait_one(qp, R2_ASYNC_WAIT);
    }
    return ::rdmaio::Ok(wc);
  }

  inline auto wait_one(const Arc<RC> &qp, R2_ASYNC) -> Result<ibv_wc> {
    // to avoid performance overhead of Arc, we first extract QP's raw pointer
    // out
    RC *qp_ptr = ({  // unsafe code
      RC *temp = qp.get();
      temp;
    });

    ibv_wc wc;
    auto id = R2_COR_ID();

    poll_func_t poll_future =
        [qp_ptr, id]() -> Result<std::pair<::r2::Routine::id_t, usize>> {
      auto wr_wc = qp_ptr->poll_rc_comp();
      if (wr_wc) {
        ::r2::Routine::id_t polled_cid =
            static_cast<::r2::Routine::id_t>(std::get<0>(wr_wc.value()));
        // wc = std::get<1>(wr_wc.value());
        auto wc = std::get<1>(wr_wc.value());

        if (wc.status == IBV_WC_SUCCESS) {
          auto ret = std::make_pair(polled_cid, 1u);
          return ::rdmaio::Ok(ret);
        } else {
          return ::rdmaio::Err(std::make_pair<::r2::Routine::id_t>(id, 1u));
        }
      }
      // we still need to poll this future
      return NotReady(std::make_pair<::r2::Routine::id_t>(0u, 0u));
    };

    // end spawning future
    auto ret = R2_PAUSE_WAIT(poll_future, 1);
    return ::rdmaio::transfer(ret, wc);
  }
};  // namespace rdma
}  // namespace rdma

}  // namespace r2
