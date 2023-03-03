#pragma once

// the ring version of manager is very similar to the send/recv verbs in RC
#include "rlib/core/qps/rc_recv_manager.hh"
#include "rlib/core/utils/marshal.hh"

#include "../mem_block.hh"
#include "./proto.hh"

namespace r2 {

namespace ring_msg {

using namespace rdmaio;
using namespace rdmaio::qp;
using namespace rdmaio::proto;
using namespace rdmaio::rmem;

struct RingRecvCommon {
  RecvCommon recv_common;
  MemBlock ring_area;
  RegAttr reg_attr; // XD: this may not be necessary

public:
  RingRecvCommon(ibv_cq *cq, Arc<AbsRecvAllocator> alloc, const MemBlock &mem,
                 const RegAttr &reg)
      : recv_common(cq, alloc), ring_area(mem), reg_attr(reg) {}

  RingRecvCommon(ibv_cq *cq, Arc<AbsRecvAllocator> alloc)
      : recv_common(cq, alloc) {}

  // this function is a wrapper over the upper constructor
  // used for factory construction

  static Option<Arc<RingRecvCommon>> create(ibv_cq *cq,
                                            Arc<AbsRecvAllocator> alloc) {
    return std::make_shared<RingRecvCommon>(cq, alloc);
  }
};

template <usize single_recv_entry> class RingManager {
public:
  Factory<std::string, RingRecvCommon> reg_comm;
  Factory<std::string, RecvEntries<single_recv_entry>> reg_recv_entries;
  Factory<std::string, MemBlock> reg_rings;

  /*!
    we assume RCtrl is a global static variable which never freed.
   */
  RCtrl *rctrl_p;

  explicit RingManager(RCtrl &ctrl) : rctrl_p(&ctrl) {
    RDMA_ASSERT(ctrl.rpc.register_handler(
        proto::CreateRCM, std::bind(&RingManager::msg_ring_handler, this,
                                    std::placeholders::_1)));
  }

  ~RingManager() = default;

  Option<MemBlock> query_ring(const std::string &name) {
    auto res = reg_rings.query(name);
    if (res) {
      return *(res.value());
    }
    return {};
  }

  Option<Arc<RC>> query_ring_qp(const std::string &name){
    auto res = rctrl_p->registered_qps.query(name);
    if (!res)
      return {};
    return std::dynamic_pointer_cast<RC>(res.value());
  }

  /*!
    The handler for creating a QP which is ready for recv.
    This handler should register with RCtrl (defined in ../rctrl.hh).
    \note: the implementation is similar to the RCtrl's rc_handler,
    but additionally allocate a RecvEntries<R> for the QP, and allocate a
    ring"
  */
  ByteBuffer msg_ring_handler(const ByteBuffer &b) {
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

      // 1.0 find the Nic to create this QP
      LOG(0) << "try find nic";
      auto nic = rctrl_p->opened_nics.query(rc_req.nic_id);
      if (!nic)
        goto WA; // failed to find Nic

      if (!(rc_req.whether_recv == 1 && rc_req.whether_create == 1))
        goto WA;

      // 1.0 check whether we are able to use the registered recv_cq
      ibv_cq *recv_cq = nullptr;
      auto recv_c_res = reg_comm.query(rc_req.name_recv);
      if (!recv_c_res)
        recv_cq = nullptr;
      else
        recv_cq = recv_c_res.value()->recv_common.cq;

      // 1.1 try to create and register this QP
      auto rc = qp::RC::create(nic.value(), rc_req.config, recv_cq).value();
      auto rc_status = rctrl_p->registered_qps.reg(rc_req.name, rc);
      LOG(0) << "try to createqp";
      if (!rc_status) {
        // failed to create register this QP
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
      auto recv_entries = RecvEntriesFactoryv2<single_recv_entry>::create(
          recv_c_res.value()->recv_common.allocator,
          0); // it's a ring message, so no need for a two-sided recv buffer
      RDMA_ASSERT(reg_recv_entries.reg(rc_req.name, recv_entries));

      // 1.4 we post_recvs
      auto res = rc->post_recvs(*recv_entries, single_recv_entry);
      RDMA_ASSERT(res == IOCode::Ok); // FIXME: now assert false if failed

      // 2. we allocate a ring buffer for the QP
      auto ring =
          recv_c_res.value()->recv_common.allocator->alloc_one_for_remote(
              rc_req.max_recv_sz);
      if (!ring) {
        // FIXME: we should clean up
        ASSERT(false) << "not implemented";
        goto WA;
      } else {
        // fill in the registered area
        auto mem = Arc<MemBlock>(
            new MemBlock(std::get<0>(ring.value()), rc_req.max_recv_sz));
        ASSERT(reg_rings.reg(rc_req.name, mem));

        rc->bind_local_mr(std::get<1>(ring.value())); // we set a default local MR for the created QP
      }

      // 3. fetch the QP result
      auto rc_reply = ::rdmaio::Marshal::dedump<RCReply>(
                          rctrl_p->fetch_qp_attr(rc_req, key))
                          .value();

      // 4. prepare our reply
      RingReply reply;
      reply.rc_reply = rc_reply;
      reply.base_off = (u64)(std::get<0>(ring.value()));
      reply.mr = std::get<1>(ring.value());

      return Marshal::dump<RingReply>(reply);
    }
    // Error handling cases:
  WA: // wrong arg
    return Marshal::dump<RingReply>(
        {.rc_reply = {.status = CallbackStatus::WrongArg}});
  Err:
    return Marshal::dump<RingReply>(
        {.rc_reply = {.status = CallbackStatus::ConnectErr}});
  };
};

} // namespace ring_msg

} // namespace r2
