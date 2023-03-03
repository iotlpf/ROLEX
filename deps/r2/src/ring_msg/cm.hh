#pragma once

#include "rlib/core/lib.hh"

#include "./proto.hh"

namespace r2 {

namespace ring_msg {

using namespace rdmaio;

// add a connect ring message for CM
class RingCM : public ConnectManager {
public:
  // FIXME: I remember there is a simpler form
  explicit RingCM(const std::string &addr) : ConnectManager(addr) {}

  // FIXME: maybe handle error more transparently
  Result<RingReply> connect_for_ring(const std::string &qp_name,
                                     const std::string &channel_name,
                                     const usize &ring_sz,
                                     const Arc<::rdmaio::qp::RC> rc,
                                     const ::rdmaio::nic_id_t &nic_id,
                                     const ::rdmaio::qp::QPConfig &config,
                                     const double &timeout_usec = 1000000) {

    RingReply dummy_reply;

    u64 temp_key = 0;

    if (unlikely(qp_name.size() > ::rdmaio::qp::kMaxQPNameLen)) {
      goto ErrCase;
    }

    {
      proto::RCReq req = {};
      memcpy(req.name, qp_name.data(), qp_name.size());
      memcpy(req.name_recv, channel_name.data(), channel_name.size());

      req.whether_create = 1;
      req.whether_recv = 1;

      req.nic_id = nic_id;
      req.config = config;
      req.attr = rc->my_attr();
      req.max_recv_sz = ring_sz;

      auto res = rpc.call(proto::CreateRCM,
                          ::rdmaio::Marshal::dump<proto::RCReq>(req));

      // FIXME: below are the same as cc_rc(); maybe refine in a more elegant
      // form
      if (unlikely(res != IOCode::Ok)) {
        goto ErrCase;
      }

      auto res_reply = rpc.receive_reply(timeout_usec);

      if (res_reply == IOCode::Ok) {
        try {
          auto qp_reply =
              ::rdmaio::Marshal::dedump<RingReply>(res_reply.desc).value();
          switch (qp_reply.rc_reply.status) {
          case proto::CallbackStatus::Ok: {
            auto ret = rc->connect(qp_reply.rc_reply.attr);
            if (ret != IOCode::Ok) {
              goto ErrCase;
            }
            return ::rdmaio::Ok(qp_reply);
          } break;
          default:
            goto ErrCase;
          }
        } catch (std::exception &e) {
        }
      }
    }

  ErrCase:
    return ::rdmaio::Err(dummy_reply);
  }
};

} // namespace ring_msg

} // namespace r2
