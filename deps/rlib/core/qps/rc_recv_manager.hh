#pragma once

#include "../rctrl.hh"

#include "./recv_helper.hh"

namespace rdmaio {

namespace qp {
/*!
  Common structure shared by a recv endpoint
 */
struct RecvCommon {
  ibv_cq *cq;
  Arc<AbsRecvAllocator> allocator;

  RecvCommon(ibv_cq *cq, Arc<AbsRecvAllocator> alloc)
      : cq(cq), allocator(alloc) {}

  static Option<Arc<RecvCommon>> create(ibv_cq *cq, Arc<AbsRecvAllocator> alloc) {
    return std::make_shared<RecvCommon>(cq,alloc);
  }
};

/*!
  T: reserved
  R: recv cq depth for a QP
*/
template <usize R = 128, usize T = 4096> class RecvManager {
  static_assert(R <= 2048, "");

public:
  Factory<std::string, RecvCommon> reg_recv_cqs;
  Factory<std::string, RecvEntries<R>> reg_recv_entries;

  /*!
    we assume RCtrl is a global static variable which never freed.
   */
  RCtrl *rctrl_p = nullptr;

  explicit RecvManager(RCtrl &ctr) : rctrl_p(&ctr) {
    RDMA_ASSERT(ctr.rpc.register_handler(
        proto::CreateRCM,
        std::bind(&RecvManager::msg_rc_handler, this, std::placeholders::_1)));
  }

  /*!
    The handler for creating a QP which is ready for recv.
    This handler should register with RCtrl (defined in ../rctrl.hh).
    \note: the implementation is similar to the RCtrl's rc_handler,
    but additionally allocate a RecvEntries<R> for the QP.
  */
  ByteBuffer msg_rc_handler(const ByteBuffer &b) {
    auto rc_req_o = ::rdmaio::Marshal::dedump<proto::RCReq>(b);
    if (!rc_req_o)
      goto WA;
    {
      auto rc_req = rc_req_o.value();

      // 1. sanity check the request
      if (!(rc_req.whether_create == static_cast<u8>(1) ||
            rc_req.whether_create != static_cast<u8>(0)))
        goto WA;

      // 1. check whether we need to create the QP
      u64 key = 0;
      if (rc_req.whether_create == 1) {
        // 1.0 find the Nic to create this QP
        auto nic = rctrl_p->opened_nics.query(rc_req.nic_id);
        if (!nic)
          goto WA; // failed to find Nic

        // 1.0 check whether we are able to use the registered recv_cq
        ibv_cq *recv_cq = nullptr;
        if (rc_req.whether_recv == 1) {
          auto recv_c_res = reg_recv_cqs.query(rc_req.name_recv);
          if (!recv_c_res)
            recv_cq = nullptr;
          else
            recv_cq = recv_c_res.value()->cq;
        }

        // 1.1 try to create and register this QP
        auto rc = qp::RC::create(nic.value(), rc_req.config, recv_cq).value();
        auto rc_status = rctrl_p->registered_qps.reg(rc_req.name, rc);

        if (!rc_status) {
          // clean up
          goto WA;
        }

        // 1.2 finally we connect the QP
        if (rc->connect(rc_req.attr) != IOCode::Ok) {
          // in connect error
          rctrl_p->registered_qps.dereg(rc_req.name, rc_status.value());
          goto WA;
        }
        key = rc_status.value();

        // 1.3 this QP is done, alloc the recv; entries
        auto recv_c_res = reg_recv_cqs.query(rc_req.name_recv); // must exsist, because we have checked in step 1.0
        auto recv_entries = RecvEntriesFactoryv2<R>::create(recv_c_res.value()->allocator, rc_req.max_recv_sz);
        RDMA_ASSERT(reg_recv_entries.reg(rc_req.name, recv_entries));

        // 1.4 we post_recvs
        auto res = rc->post_recvs(*recv_entries, R);
        RDMA_ASSERT(res == IOCode::Ok); // FIXME: now assert false if failed
      }

      // 2. fetch the QP result
      return rctrl_p->fetch_qp_attr(rc_req, key);
    }
    // Error handling cases:
  WA: // wrong arg
    return ::rdmaio::Marshal::dump<proto::RCReply>(
        {.status = proto::CallbackStatus::WrongArg});
  Err:
    return ::rdmaio::Marshal::dump<proto::RCReply>(
        {.status = proto::CallbackStatus::ConnectErr});
  }
};

}; // namespace qp

} // namespace rdmaio
