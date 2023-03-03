#pragma once

#include <memory>

#include "./config.hh"
#include "./mod.hh"
#include "./impl.hh"
#include "./recv_helper.hh"

namespace rdmaio {

namespace qp {

/*!
  40 bytes reserved for GRH, found in
  https://www.rdmamojo.com/2013/02/15/ibv_poll_cq/
*/
const usize kGRHSz = 40;

/*!
  an abstraction of unreliable datagram
  example usage:
  `
  // TODO
  `
  // check tests/test_ud.cc
 */
class UD : public Dummy, public std::enable_shared_from_this<UD> {
public:
  /*!
    a msg should fill in one packet (4096 bytes); some bytes are reserved
    for header (GRH size)
  */
  const usize kMaxMsgSz = 4000;

  const usize kMaxUdRecvEntries = 2048;

  usize pending_reqs = 0;

  const QPConfig my_config;

  static Option<Arc<UD>> create(Arc<RNic> nic, const QPConfig &config) {
    auto ud_ptr = Arc<UD>(new UD(nic, config));
    if (ud_ptr->valid())
      return ud_ptr;
    return {};
  }

  QPAttr my_attr() const override {
    return {.addr = nic->addr.value(),
            .lid = nic->lid.value(),
            .psn = static_cast<u64>(my_config.rq_psn),
            .port_id = static_cast<u64>(nic->id.port_id),
            .qpn = static_cast<u64>(qp->qp_num),
            .qkey = static_cast<u64>(my_config.qkey)};
  }

  /*!
    create address handler from a QP attribute
   */
  ibv_ah *create_ah(const QPAttr &attr) {
    struct ibv_ah_attr ah_attr = {};
#if 1
    ah_attr.is_global = 1;
    ah_attr.dlid = attr.lid;
    ah_attr.sl = 0;
    ah_attr.src_path_bits = 0;
    ah_attr.port_num = nic->id.port_id; //attr.port_id;

    ah_attr.grh.dgid.global.subnet_prefix = attr.addr.subnet_prefix;
    ah_attr.grh.dgid.global.interface_id = attr.addr.interface_id;
    ah_attr.grh.flow_label = 0;
    ah_attr.grh.hop_limit = 255;
    ah_attr.grh.sgid_index = nic->addr.value().local_id;
#else
    ah_attr.is_global = 0;
    ah_attr.dlid = attr.lid;
    ah_attr.sl = 0;
    ah_attr.src_path_bits = 0;
    ah_attr.port_num = nic->id.port_id;
#endif
    return ibv_create_ah(nic->get_pd(), &ah_attr);
  }

private:
  UD(Arc<RNic> nic, const QPConfig &config) : Dummy(nic), my_config(config) {

    // create qp, cq, recv_cq
    auto res = Impl::create_cq(nic, my_config.max_send_sz());

    if (res != IOCode::Ok) {
      RDMA_LOG(4) << "Error on creating CQ: " << std::get<1>(res.desc);
      return;
    }
    this->cq = std::get<0>(res.desc);

    auto res_recv = Impl::create_cq(nic, my_config.max_recv_sz());
    if (res_recv != IOCode::Ok) {
      RDMA_LOG(4) << "Error on creating recv CQ: " << std::get<1>(res_recv.desc);
      return;
    }

    this->recv_cq = std::get<0>(res_recv.desc);

    auto res_qp =
        Impl::create_qp(nic, IBV_QPT_UD, my_config, this->cq, this->recv_cq);
    if (res_qp != IOCode::Ok) {
      RDMA_LOG(4) << "Error on creating UD QP: " << std::get<1>(res_qp.desc);
      return;
    }
    this->qp = std::get<0>(res_qp.desc);

    // finally, change it to ready_to_recv & ready_to_send
    if (valid()) {

      // 1. change to ready to init
      auto res_init =
          Impl::bring_qp_to_init(this->qp, this->my_config, this->nic);
      if (res_init != IOCode::Ok) {
        // shall we assert false? this is rarely unlikely the case
        RDMA_ASSERT(false)
            << "bring ud to init error, not handlered failure case: "
            << res_init.desc;
      } else {
      }

      // 2. change to ready_to_recv
      if (!bring_ud_to_recv(this->qp) ||
          !bring_ud_to_send(this->qp, this->my_config.rq_psn)) {
        RDMA_ASSERT(false);
        this->cq = nullptr; // make my status invalid
      }
    }
    // done, UD create done
  }

  //
  static bool bring_ud_to_recv(ibv_qp *qp) {
    int rc, flags = IBV_QP_STATE;
    struct ibv_qp_attr qp_attr = {};
    qp_attr.qp_state = IBV_QPS_RTR;

    rc = ibv_modify_qp(qp, &qp_attr, flags);
    return rc == 0;
  }

  static bool bring_ud_to_send(ibv_qp *qp, int psn) {
    int rc, flags = 0;
    struct ibv_qp_attr qp_attr = {};
    qp_attr.qp_state = IBV_QPS_RTS;
    qp_attr.sq_psn = psn;

    flags = IBV_QP_STATE | IBV_QP_SQ_PSN;
    rc = ibv_modify_qp(qp, &qp_attr, flags);
    return rc == 0;
  }
};
} // namespace qp

} // namespace rdmaio
