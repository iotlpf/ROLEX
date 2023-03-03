#pragma once

#include "./msg_session.hh"

#include "rlib/core/qps/mod.hh"
#include "rlib/core/qps/recv_helper.hh"

namespace r2 {

using namespace rdmaio;
using namespace rdmaio::qp;

class RCSession : public Session {
public:

  RC *qp = nullptr;
  const usize send_depth; // configured per QP

  usize pending_sends = 0;

  const int id;

  RCSession(const int &id, Arc<RC> &rc)
      : id(id), qp(rc.get()), send_depth(rc->my_config.max_send_sz() / 4) {}

  Result<std::string> send(const MemBlock &msg, const double timeout,
                           R2_ASYNC) override {
    return ::rdmaio::Err(std::string("not implemented"));
  }

  Result<std::string>
  send_blocking(const MemBlock &msg,
                const double timeout = no_timeout) override {
    return ::rdmaio::Err(std::string("not implemented"));
  }

  Result<std::string> send_unsignaled(const MemBlock &msg) {

    int write_flag = msg.sz <= ::rdmaio::qp::kMaxInlinSz ? IBV_SEND_INLINE : 0;
    auto res_s = qp->send_normal(
        {.op = IBV_WR_SEND_WITH_IMM,
         .flags = write_flag | ((pending_sends == 0) ? (IBV_SEND_SIGNALED) : 0),
         .len = msg.sz,
         .wr_id = 0},
        {.local_addr = reinterpret_cast<RMem::raw_ptr_t>(msg.mem_ptr),
         .remote_addr = 0,
         .imm_data = id});

    if (pending_sends >= send_depth) {
      auto res_p = qp->wait_one_comp();
      RDMA_ASSERT(res_p == IOCode::Ok)
          << "wait completion error: " << RC::wc_status(res_p.desc);
      pending_sends = 0;
    } else {
      pending_sends += 1;
    }

    return res_s;
  }

  Result<std::string> send_pending(const MemBlock &msg) override {
    return ::rdmaio::Err(std::string("not implemented"));
  }

  Result<std::string> send_pending(const MemBlock &msg, R2_ASYNC) override {
    return ::rdmaio::Err(std::string("not implemented"));
  }
};

/*!
  Unlike UD, per RC need to manage per-QP recv data structure
  We use a separate class for helper
 */
template <usize R> class RCRecvSession {
  Arc<RecvEntries<R>> recv_entries;
  Arc<RC> qp;

  usize idle_recv_entries = 0;
  const usize poll_recv_step = R / 2;

public:
  RCSession end_point;

  RCRecvSession(Arc<RC> qp, Arc<RecvEntries<R>> e)
      : qp(qp), recv_entries(e), end_point(123, qp) {}

  void consume_one() {
    idle_recv_entries += 1;
    if (idle_recv_entries >= poll_recv_step) {
      qp->post_recvs(*recv_entries, idle_recv_entries);
      idle_recv_entries = 0;
    }
  }
};

} // namespace r2
