#pragma once

#include "./cm.hh"
#include "./receiver.hh"
#include "./session.hh"

namespace r2 {

namespace ring_msg {

/*!
  Example usage of ring message.
  At server:
  `
  RCtrl ctrl(...);

  // the two manager are used to create rdma send/recv verbs
  // we additionally uses one manager for ring message

  // 128: the registered entry at each QP
  //Arc<RingManager<128>> rm = RingManager<128>::create(ctrl);
  RingManager<128> rm(ctrl);

  auto recv_cq = ...;
  Arc<AbsRecvAllocator> alloc = ...;

  Arc<Receiver<128>> ring_receiver = rm.create("my_ring_receiver",
  recv_cq,alloc).value();

  // after server receive the first message, using the recv_cq
  `

  At client:
  `
  const usize ring_sz = 1024;
  const usize max_ring_sz = 64;
  RingSession<128> rs(qp,1024,64);

  RingCM cm(some_addr); // use a specificed ring cm
  ASSERT(rs.connect(cm) == IOCode::Ok);

  // we need a bootstrap message, or whatever
  `
 */

template <usize R, usize kRingSz, usize kMaxMsg> class RecvFactory {
public:
  static Option<Arc<::r2::ring_msg::Receiver<R, kRingSz, kMaxMsg>>>
  create(RingManager<R> &manager, const std::string &name, ibv_cq *cq,
         Arc<AbsRecvAllocator> alloc) {
    // first we try to reg (cq, alloc) to factory
    if (manager.reg_comm.create_then_reg(name, cq, alloc)) {
      // then we create the receiver
      return std::make_shared<::r2::ring_msg::Receiver<R, kRingSz, kMaxMsg>>(
          name, cq, &manager);
    }
    return {};
  }
};

template <usize R, usize kRingSz, usize kMaxMsg> class RingRecvIter {
  Arc<Receiver<R, kRingSz, kMaxMsg>> receiver;

  int idx = 0;
  int total_msgs = -1;

  Session<R, kRingSz, kMaxMsg> *s_ptr = nullptr;

  usize msg_sz = 0;

public:
  RingRecvIter(Arc<Receiver<R, kRingSz, kMaxMsg>> r)
      : receiver(r),
        total_msgs(ibv_poll_cq(receiver->recv_cq, receiver->num_wcs(),
                               receiver->get_wcs_ptr())) {
    fill_cur_msg();
  }

  ~RingRecvIter() {
    //LOG(0) << "end";
  }

  inline void begin() {
    this->idx = 0;
    this->total_msgs = ibv_poll_cq(receiver->recv_cq, receiver->num_wcs(),
                                   receiver->get_wcs_ptr());
    this->fill_cur_msg();
  }

  inline void next() {
    // 2. then we update to the next message
    s_ptr->update_recv_meta();
    idx += 1;

    fill_cur_msg();
  }

  inline bool has_msgs() const { return idx < total_msgs; }

  inline MemBlock cur_msg() { return cur_session()->cur_msg(msg_sz).value(); }

  /*!
    Current session correspond to this message
   */
  inline Session<R, kRingSz, kMaxMsg> *cur_session() { return s_ptr; }

private:
  void fill_cur_msg() {
    if (!has_msgs())
      return;
    do {
      ASSERT(receiver->wcs[idx].status == IBV_WC_SUCCESS);
      auto val = receiver->wcs[idx].imm_data;
      auto decoded = IDDecoder::decode(val);

      auto session_id = std::get<0>(decoded);

      auto s = receiver->query_session(session_id, std::get<1>(decoded));

      if (unlikely(!s)) {
        // this is a session connect message, ignore
        // re-post the recv, into next message
        s_ptr = receiver->query_session(session_id, std::get<1>(decoded)).value();
        next();
      } else {
        // the message is filled
        s_ptr = s.value();
        msg_sz = std::get<1>(decoded);
        break;
      }

    } while (has_msgs());
  }
};

} // namespace ring_msg
} // namespace r2
