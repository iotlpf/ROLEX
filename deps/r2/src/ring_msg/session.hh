#pragma once

#include "rlib/core/qps/mod.hh"

#include "./id_helper.hh"
#include "./recv_bundler.hh"
#include "./ring.hh"

#include "./cm.hh"

namespace r2 {

namespace ring_msg {

using namespace rdmaio;
using namespace rdmaio::qp;

/*!
  R: max post recv entries
 */
template <usize R, usize kRingSz, usize kMaxMsg> class Session {
  static_assert(kRingSz >= kMaxMsg,
                "The ring should be larger than the max msg supported");

  static_assert(kMaxMsg < std::numeric_limits<u16>::max(), "");

  Arc<RC> qp;

  Arc<RecvBundler<R>> recv_meta;
  RemoteRing<kRingSz> remote_ring;
  LocalRing<kRingSz> local_ring;

  const usize send_depth; // configured per QP
  usize pending_sends = 0;

public:
  /*!
    \note: we use u16 for recording the message
    This is an iternal limits of RDMA_WRITE_WITH_IMM,
    since the imm_data is only u32.
    Because we must encode (msg_sz,id) into one u32,
    so we must limit its sz.

    In principle, 65536 bytes message are supported,
    and 65536 connections are supported on one machine.
   */
  const u16 id;

  /*!
    This constructor is used on sender
    This is the most common constructor used.
    Most parametner is similar to the ones in creating a QP (nic,config,cq)
   */
  Session(const u16 &id, Arc<RNic> nic, const QPConfig &config, ibv_cq *cq,
          Arc<AbsRecvAllocator> alloc)
      : qp(RC::create(nic, config, cq).value()), id(id),
        recv_meta(std::make_shared<RecvBundler<R>>(alloc)),
        send_depth(qp->my_config.max_send_sz() / 2),
        local_ring(MemBlock(
            std::get<0>(alloc->alloc_one_for_remote(kRingSz + kMaxMsg).value()),
            kRingSz + kMaxMsg)) {
    // TODO: allocate a local ring for remote to send back
    auto local_mr = std::get<1>(alloc->alloc_one_for_remote(0).value());
    qp->bind_local_mr(local_mr);
  }

  /*!
    This constructor is constructed on the receiver
    - qp is created
    - recv_meta is created
    - remote_ring is passed
    - local_ring is created
    - id is known
   */
  Session(const u16 &id, Arc<RC> qp, Arc<RecvBundler<R>> meta,
          u64 addr /* used for create remote_ring */, const RegAttr &remote_mr,
          const LocalRing<kRingSz> &local_mem)
      : id(id), qp(qp), recv_meta(meta), local_ring(local_mem),
        remote_ring(addr, remote_mr.key),
        send_depth(qp->my_config.max_send_sz() / 2) {
    qp->bind_remote_mr(remote_mr);
  }

  ~Session() {
    // FIXME: should release local ring
  }

  Result<int> update_recv_meta() { return recv_meta->consume_one(qp); }

  inline Option<MemBlock> cur_msg(const usize &sz) {
    return local_ring.cur_msg(sz);
  }

  /*!
    Connect for this ring session
  */
  /*!
    Session is bootstrap with 2 message. The #1 uses UDP, and the second use
    RDMA The #1 ensures that this session is able to connect to remote
    server; the #2 ensures that the server's session can send message back
    to myself.
   */
  Result<> connect(RingCM &cm, const std::string &c_name,
                   const ::rdmaio::nic_id_t &nic_id,
                   const QPConfig &remote_qp_config,
                   const double &timeout_usec = 1000000) {
    // #1
    auto res =
        cm.connect_for_ring(std::to_string(id), c_name, kRingSz + kMaxMsg, qp,
                            nic_id, remote_qp_config, timeout_usec);
    if (res != IOCode::Ok) {
      return ::rdmaio::transfer(res, DummyDesc());
    }

    auto qp_reply = res.desc;

    // 1. change QP to ready status
    auto ret = qp->connect(qp_reply.rc_reply.attr);
    if (ret != IOCode::Ok) {
      LOG(4) << "connect QP err: " << ret.desc;
      return ::rdmaio::transfer(ret, DummyDesc());
    }

    // 2. copy the remote MR attr
    ASSERT(qp_reply.base_off >= qp_reply.mr.buf)
        << "base off: " << qp_reply.base_off << "; mr buf: " << qp_reply.mr.buf;
    ASSERT(qp_reply.base_off + kRingSz + kMaxMsg <=
           qp_reply.mr.buf + qp_reply.mr.sz);
    auto base_addr = qp_reply.base_off - qp_reply.mr.buf;
    remote_ring =
        RemoteRing<kRingSz>(static_cast<usize>(base_addr), qp_reply.mr.key);

    qp->bind_remote_mr(qp_reply.mr);

    // 3. post my recvs
    auto res_p = qp->post_recvs(*(recv_meta->recv_entries), R);

    if (res_p != IOCode::Ok) {
      RDMA_LOG(4) << "post my recv error: " << strerror(res_p.desc);
      return ::rdmaio::transfer(res_p, DummyDesc());
    }

    //#2 send the bootstrap message
    return send_bootstrap();
  }

  Result<std::string> send_blocking(const MemBlock &msg,
                                    const double &timeout = 1000000) {
    // 1. calculate proper flag for sending
    int write_flag = msg.sz <= ::rdmaio::qp::kMaxInlinSz ? IBV_SEND_INLINE : 0;

    // 2. calculate the imm msg
    auto imm_data =
        IDEncoder::encode_id_sz(this->id, static_cast<sz_t>(msg.sz));

    // 3. calculate the remote offset
    auto remote_addr = remote_ring.next_addr(msg.sz);

    // 4. use post_send to write this message
    auto res_s = qp->send_normal(
        {.op = IBV_WR_RDMA_WRITE_WITH_IMM,
         .flags = write_flag | IBV_SEND_SIGNALED,
         .len = msg.sz,
         .wr_id = 0},
        {.local_addr = reinterpret_cast<RMem::raw_ptr_t>(msg.mem_ptr),
         .remote_addr = remote_addr,
         .imm_data = imm_data});

    if (unlikely(res_s != IOCode::Ok))
      return res_s;
    // 5. wait for the completion
    auto ret = qp->wait_one_comp(timeout);
    if (unlikely(ret != IOCode::Ok)) {
      return ::rdmaio::transfer(ret, UD::wc_status(ret.desc));
    }
    return ::rdmaio::Ok(std::string(""));
  }

  // following are sender methods
  Result<std::string> send_unsignaled(const MemBlock &msg) {

    // 1. calculate proper flag for sending
    int write_flag = msg.sz <= ::rdmaio::qp::kMaxInlinSz ? IBV_SEND_INLINE : 0;

    // 2. calculate the imm msg
    auto imm_data =
        IDEncoder::encode_id_sz(this->id, static_cast<sz_t>(msg.sz));

    // 3. calculate the remote offset
    auto remote_addr = remote_ring.next_addr(msg.sz);

    // 4. use post_send to write this message
    auto res_s = qp->send_normal(
        {.op = IBV_WR_RDMA_WRITE_WITH_IMM,
         .flags = write_flag | ((pending_sends == 0) ? (IBV_SEND_SIGNALED) : 0),
         .len = msg.sz,
         .wr_id = 0},
        {.local_addr = reinterpret_cast<RMem::raw_ptr_t>(msg.mem_ptr),
         .remote_addr = remote_addr,
         .imm_data = imm_data});

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

private:
  // send the bootstrap message
  Result<> send_bootstrap() {
    auto local_mr = qp->local_mr.value();
    auto base_addr = local_ring.convert_to_rdma_addr(local_mr);

    // send the message
    auto msg = ::rdmaio::Marshal::dump<RingBootstrap>(
        {.base_off = base_addr, .mr = local_mr});
    static_assert(sizeof(RingBootstrap) <= kMaxInlinSz, "");

    auto res_s = send_blocking(
        {.mem_ptr = (void *)(msg.data()), .sz = sizeof(RingBootstrap)});
    return ::rdmaio::transfer(res_s, DummyDesc());
  }
};

} // namespace ring_msg
} // namespace r2
