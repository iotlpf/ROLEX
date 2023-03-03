#pragma once

#include "../rmem/handler.hh"

#include "./mod.hh"
#include "./impl.hh"

namespace rdmaio {

using namespace rmem;

namespace qp {

/*!
  To use:
  Arc<RC> rc = RC::create(...).value();
  class Class_use_rc {
  Arc<RC> rc;

  Class_use_rc(Arc<RC> rr) : rc(std::move(rr) {
  // std::move here is to avoid copy from shared_ptr
  }
  };

  Class_use_rc cur(rc);
  // to use cur
  }

  // example to send an RC request to remote QP
  RC &qp = *rc; // some QP

  // note that send_normal is deprecated; plase check ./op.hh for other usage
  // read sizeof(u64) to (local_addr) at remote address (remote addr)
  auto res_s = qp.send_normal({.op = IBV_WR_RDMA_READ,
                               .flags = IBV_SEND_SIGNALED,
                               .len = sizeof(u64),
                               .wr_id = 0},
                              {
                                .local_addr =
                                reinterpret_cast<RMem::raw_ptr_t>(test_loc + 1),
                                .remote_addr = 0,
                                .imm_data = 0}
  );
*/
class RC : public Dummy, public std::enable_shared_from_this<RC> {
public:
  // default local MR used by this QP
  Option<RegAttr> local_mr;
  // default remote MR used by this QP
  Option<RegAttr> remote_mr;

  Result<> status = NotReady();

  // pending requests monitor
  Progress progress;
public:
  const QPConfig my_config;

  /* the only constructor
     make it private because it may failed to create
     use the factory method to create it:
     Option<Arc<RC>> qp = RC::create(nic,config);
     if(qp) {
     ...
     }
  */
private:
  RC(Arc<RNic> nic, const QPConfig &config,ibv_cq *recv_cq = nullptr) : Dummy(nic), my_config(config) {
    /*
      It takes 3 steps to create an RC QP during the initialization
      according to the RDMA programming mannal.
      First, we create the cq for completions of send request.
      Then, we create the qp.
      Finally, we change the qp to read_to_init status.
     */
    // 1 cq
    auto res = Impl::create_cq(nic, my_config.max_send_sz());
    if (res != IOCode::Ok) {
      RDMA_LOG(4) << "Error on creating CQ: " << std::get<1>(res.desc);
      return;
    }
    this->cq = std::get<0>(res.desc);

    // FIXME: we donot sanity check the the incoming recv_cq
    // The choice is that the recv cq could be shared among other QPs
    // shall we replace this with smart pointers ?
    this->recv_cq = recv_cq;

    // 2 qp
    auto res_qp =
        Impl::create_qp(nic, IBV_QPT_RC, my_config, this->cq, this->recv_cq);
    if (res_qp != IOCode::Ok) {
      RDMA_LOG(4) << "Error on creating QP: " << std::get<1>(res.desc);
      return;
    }
    this->qp = std::get<0>(res_qp.desc);

    // 3 -> init
    auto res_init =
        Impl::bring_qp_to_init(this->qp, this->my_config, this->nic);
    if (res_init != IOCode::Ok) {
      RDMA_LOG(4) << "failed to bring QP to init: " << res_init.desc;
    }
  }

public:
  static Option<Arc<RC>> create(Arc<RNic> nic,
                                const QPConfig &config = QPConfig(),
                                ibv_cq *recv_cq = nullptr) {
    auto res = Arc<RC>(new RC(nic, config,recv_cq));
    if (res->valid()) {
      return Option<Arc<RC>>(std::move(res));
    }
    return {};
  }

  /*!
    Get the attribute of this RC QP, so that others can connect to it.
    \note: this function would panic if the created context (nic) is not valid
   */
  QPAttr my_attr() const override {
    return {.addr = nic->addr.value(),
            .lid = nic->lid.value(),
            .psn = static_cast<u64>(my_config.rq_psn),
            .port_id = static_cast<u64>(nic->id.port_id),
            .qpn = static_cast<u64>(qp->qp_num),
            .qkey = static_cast<u64>(0)};
  }

  Result<> my_status() const { return status; }

  /*!
    bind a mr to remote/local mr(so that QP is able to access its memory)
   */
  void bind_remote_mr(const RegAttr &mr) { remote_mr = Option<RegAttr>(mr); }

  void bind_local_mr(const RegAttr &mr) { local_mr = Option<RegAttr>(mr); }

  /*!
    connect this QP to a remote qp attr
    \note: this connection is setup locally.
    The `attr` must be fetched from another machine.
   */
  Result<std::string> connect(const QPAttr &attr) {
    auto res = qp_status();
    if (res.code == IOCode::Ok) {
      auto status = res.desc;
      switch (status) {
      case IBV_QPS_RTS:
        return Ok(std::string("already connected"));
        // TODO: maybe we should check other status
      default:
        // really connect the QP
        {
          // first bring QP to ready to recv. note we bring it to ready to init
          // during class's construction.
          auto res =
              Impl::bring_rc_to_rcv(qp, my_config, attr, my_attr().port_id);
          if (res.code != IOCode::Ok)
            return res;
          // then we bring it to ready to send.
          res = Impl::bring_rc_to_send(qp, my_config);
          if (res.code != IOCode::Ok)
            return res;
        }
        this->status = Ok();
        return Ok(std::string(""));
      }
    } else {
      return Err(std::string("QP not valid"));
    }
  }

  /*!
    Post a send request to the QP.
    the request can be:
    - ibv_wr_send (send verbs)
    - ibv_wr_read (one-sided read)
    - ibv_wr_write (one-sided write)
   */
  struct ReqDesc {
    ibv_wr_opcode op;
    int flags;
    u32 len = 0;
    u64 wr_id = 0;
  };

  struct ReqPayload {
    rmem::RMem::raw_ptr_t local_addr = nullptr;
    u64 remote_addr = 0;
    u64 imm_data = 0;
  };

  /*!
    the version of send_normal without passing a MR.
    it will use the default MRs binded to this QP.
    \note: this function will panic if no MR is bind to this QP.
   */
  Result<std::string> send_normal(const ReqDesc &desc,
                                  const ReqPayload &payload) {
    return send_normal(desc, payload, local_mr.value(), remote_mr.value());
  }

  u64 encode_my_wr(const u64 &wr, int forward_num) {
    return (static_cast<u64>(wr) << Progress::num_progress_bits) |
           static_cast<u64>(progress.forward(forward_num));
  }

  Result<std::string> send_normal(const ReqDesc &desc,
                                  const ReqPayload &payload,
                                  const RegAttr &local_mr,
                                  const RegAttr &remote_mr) {
    RDMA_ASSERT(status == IOCode::Ok)
        << "a QP should be Ok to send, current status: " << status.code.name();

    struct ibv_sge sge {
      .addr = (u64)(payload.local_addr), .length = desc.len,
      .lkey = local_mr.lkey
    };

    struct ibv_send_wr sr, *bad_sr;

    sr.wr_id = encode_my_wr(desc.wr_id, 1);
    sr.opcode = desc.op;
    sr.num_sge = 1;
    sr.next = nullptr;
    sr.sg_list = &sge;
    sr.send_flags = desc.flags;
    sr.imm_data = payload.imm_data;

    sr.wr.rdma.remote_addr = remote_mr.buf + payload.remote_addr;
    sr.wr.rdma.rkey = remote_mr.key;

    if (desc.flags & IBV_SEND_SIGNALED)
      out_signaled += 1;

    auto rc = ibv_post_send(qp, &sr, &bad_sr);
    if (0 == rc) {
      return Ok(std::string(""));
    }
    return Err(std::string(strerror(errno)));
  }

  /*!
    A wrapper of poll_send_comp.
    It maintain the watermark of progress by decoding the watermark
    in the wc, and return the encoded user wr.
   */
  Option<std::pair<u64, ibv_wc>> poll_rc_comp() {
    auto num_wc = poll_send_comp();
    if (std::get<0>(num_wc) == 0)
      return {};
    auto &wc = std::get<1>(num_wc);
    u64 user_wr = wc.wr_id >> (Progress::num_progress_bits);

    const auto mask = bitmask<u64>(Progress::num_progress_bits);
    u64 water_mark = wc.wr_id & bitmask<u64>(Progress::num_progress_bits);
    progress.done(water_mark);

    return std::make_pair(user_wr, wc);
  }

  Result<std::pair<u64, ibv_wc>>
  wait_rc_comp(const double &timeout = ::rdmaio::Timer::no_timeout()) {
    Timer t;
    ibv_wc wc;
    Option<std::pair<u64, ibv_wc>> res = {};
    do {
      // poll one comp
      res = poll_rc_comp();
    } while (!res && // poll result is 0
             t.passed_msec() < timeout);
    if (!res)
      return Timeout(std::make_pair(0lu, wc));
    if (std::get<1>(res.value()).status != IBV_WC_SUCCESS)
      return Err(std::make_pair(0lu, wc));
    return Ok(res.value());
  }

  int max_send_sz() const { return my_config.max_send_size; }
};

} // namespace qp

} // namespace rdmaio
