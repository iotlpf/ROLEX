#pragma once

#include <unordered_map>

#include <string>

#include "./manager.hh"
#include "./session.hh"

namespace r2 {

namespace ring_msg {

using namespace rdmaio;
using namespace rdmaio::qp;

template <usize X, usize kR, usize kM> class RingRecvIter;

/*!
  A ring receiver is responsible for receiving message from *n* QPs using a
  shared recv_cq
 */
template <usize R, usize kRingSz, usize kMaxMsg> class Receiver {
  friend class RingRecvIter<R, kRingSz, kMaxMsg>;

public:
  const std::string name;
  // main structure for handling requests
  ibv_cq *recv_cq = nullptr;
  std::vector<ibv_wc> wcs;

  RingManager<R> *manager;

  std::unordered_map<id_t, Arc<::r2::ring_msg::Session<R, kRingSz, kMaxMsg>>>
      incoming_sessions;

public:
  Receiver(const std::string &n, ibv_cq *cq, RingManager<R> *m = nullptr)
      : name(n), recv_cq(cq), wcs(cq->cqe), manager(m) {
    // the overall recv_cq depth should be larger than per QP depth (R)
    ASSERT(cq->cqe >= R);
  }

  inline ibv_wc *get_wcs_ptr() { return &wcs[0]; }

  inline usize num_wcs() const { return wcs.size(); }

  bool reg_channel(Arc<::r2::ring_msg::Session<R, kRingSz, kMaxMsg>> msg) {
    if (incoming_sessions.find(msg->id) != incoming_sessions.end())
      return false;
    incoming_sessions.insert(std::make_pair(msg->id, msg));
    return true;
  }

private:
  Option<Session<R, kRingSz, kMaxMsg> *> query_session(const id_t &id,
                                                       const sz_t &sz) {
    auto it = incoming_sessions.find(id);
    if (unlikely(it == incoming_sessions.end())) {
      ASSERT(sz == sizeof(RingBootstrap)) << "recv session id: " << id;

      // we create the session from the manager

      // 1. we extract the local ring, for extracting remote data
      using RingType = LocalRing<kRingSz>;
      auto temp_ring = RingType(manager->query_ring(std::to_string(id)).value());

      auto cur_msg = temp_ring.cur_msg(sz).value();
      RingBootstrap *bootstrap_meta = reinterpret_cast<RingBootstrap *>(cur_msg.mem_ptr);

      // sanity check the bootstrap meta
      ASSERT(bootstrap_meta->magic_key == kMagic);

      auto qp = manager->query_ring_qp(std::to_string(id)).value();
      auto recv_entries =
          manager->reg_recv_entries.query(std::to_string(id)).value();
      auto bundler = std::make_shared<RecvBundler<R>>(recv_entries);

      // 2. create and insert the session
      auto s =
          Arc<Session<R, kRingSz, kMaxMsg>>(new Session<R, kRingSz, kMaxMsg>(
              id, qp, bundler, bootstrap_meta->base_off, bootstrap_meta->mr,
              temp_ring));
      incoming_sessions.insert(std::make_pair(id, s));

      // should notify the upper iterator to ignore this message
      return {};
    }
    return it->second.get();
  }
};

} // namespace ring_msg

} // namespace r2
