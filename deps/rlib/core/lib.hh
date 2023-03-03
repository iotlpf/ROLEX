#pragma once

#include "./nicinfo.hh"
/*!
  Utilities for query RDMA NIC on this machine.

  Example usage:
  1) query all avaliable NICs, and return a vector of RLib's internal naming of
  these nics
  `
  std::vector<DevIdx> nics = RNicInfo::query_dev_names();
  `

  One can open the nic using the queried names:
  `
  auto nic =
  RNic::create(RNicInfo::query_dev_names().at(0).value();
  // using the first queried nic name
  `
 */

#include "./rctrl.hh"

#include "./bootstrap/proto.hh"
#include "./bootstrap/srpc.hh"

namespace rdmaio {

/*!
  A connect manager tailed for Making RDMA establishments
  The functions including:
  - Fetch remote MR
  - Create and Fetch a QP for connect

  RCtrl (defined in rctrl.hh) provides the end-point support for calling these
  functions at the server. User can also register their specified callbacks, see
  rctrl.hh for detailes.

  Example usage (fetch a remote registered MR, identified by the id)
  \note: we must first make sure that server must **started** and the handler
  must be **registered**. Otherwise, all calls will timeout

  `
  ConnectManager cm("localhost:1111");
  if(cm.wait_ready(1000) == IOCode::Timeout) // wait 1 second for server to
  ready assert(false);

  auto mr_res = cm.fetch_remote_mr(1,attr); // fetch "localhost:1111"'s MR with
  id **1**
  // mr_res = Ok,Err,...(err_str, mr_attr)
  if (mr_res == IOCode::Ok) {
  // use attr ...
     auto mr_attr = std::get<1>(mr_res.desc);
  }
  `
 */
using namespace bootstrap;

class ConnectManager {
protected:
  SRpc rpc;

  const std::string err_name_to_long = "Name to long";
  const std::string err_decode_reply = "Decode reply error";
  const std::string err_not_found = "attribute not found";
  const std::string err_unknown_status = "Unknown status code";

public:
  explicit ConnectManager(const std::string &addr) : rpc(addr) {}

  Result<std::string> wait_ready(const double &timeout_usec,
                                 const usize &retry = 1) {
    for (uint i = 0; i < retry; ++i) {
      // send a dummy request to the RPC
      auto res =
          rpc.call(proto::RCtrlBinderIdType::HeartBeat, ByteBuffer(1, '0'));
      if (res != IOCode::Ok && res != IOCode::Timeout)
        // some error occurs, abort
        return res;
      auto res_reply =
          rpc.receive_reply(timeout_usec, true); // receive as hearbeat message
      if (res_reply == IOCode::Ok || res_reply == IOCode::Err)
        return res_reply;
    }
    return Timeout(std::string("retry exceeds num"));
  }

  Result<std::string> delete_remote_rc(const std::string &name, const u64 &key,
                                       const double &timeout_usec = 1000000) {

    if (unlikely(name.size() > ::rdmaio::qp::kMaxQPNameLen))
      return ::rdmaio::Err(std::string(err_name_to_long));

    proto::DelRCReq req = {};
    memcpy(req.name, name.data(), name.size());
    req.key = key;

    auto res = rpc.call(proto::DeleteRC,
                        ::rdmaio::Marshal::dump<proto::DelRCReq>(req));
    if (unlikely(res != IOCode::Ok))
      return res;

    auto res_reply = rpc.receive_reply(timeout_usec);

    if (res_reply == IOCode::Ok) {
      try {
        auto qp_reply =
            ::rdmaio::Marshal::dedump<RCReply>(res_reply.desc).value();
        switch (qp_reply.status) {
        case proto::CallbackStatus::Ok:
          return ::rdmaio::Ok(std::string(""));
        case proto::CallbackStatus::WrongArg:
          return ::rdmaio::Err(std::string("wrong arg"));
        case proto::CallbackStatus::AuthErr:
          return ::rdmaio::Err(std::string("auth failure"));
        }
      } catch (std::exception &e) {
      }
    }
    ::rdmaio::Err(std::string("fatal error"));
  }

  /*!
    *C*reate and *C*onnect an RC QP at remote end.
    This function first creates an RC QP at local,
    and then create an RC QP at remote host to connect with the local
    created QP.

    \param:
    - id: the remote naming of this QP
    - nic_id: the remote NIC used for connect the pair QP
   */
  using cc_rc_ret_t = std::pair<std::string, u64>;
  Result<cc_rc_ret_t> cc_rc(const std::string &name,
                            const Arc<::rdmaio::qp::RC> rc,
                            const ::rdmaio::nic_id_t &nic_id,
                            const ::rdmaio::qp::QPConfig &config,
                            const double &timeout_usec = 1000000) {

    auto err_str = std::string("unknown error");
    u64 temp_key = 0;

    if (unlikely(name.size() > ::rdmaio::qp::kMaxQPNameLen)) {
      err_str = err_name_to_long;
      goto ErrCase;
    }

    {
      proto::RCReq req = {};
      memcpy(req.name, name.data(), name.size());
      req.whether_create = 1;
      req.whether_recv = 0;
      req.nic_id = nic_id;
      req.config = config;
      req.attr = rc->my_attr();

      auto res =
          rpc.call(proto::CreateRC, ::rdmaio::Marshal::dump<proto::RCReq>(req));

      if (unlikely(res != IOCode::Ok)) {
        err_str = res.desc;
        goto ErrCase;
      }

      auto res_reply = rpc.receive_reply(timeout_usec);

      if (res_reply == IOCode::Ok) {
        try {
          auto qp_reply =
              ::rdmaio::Marshal::dedump<RCReply>(res_reply.desc).value();
          switch (qp_reply.status) {
          case proto::CallbackStatus::Ok: {
            auto ret = rc->connect(qp_reply.attr);
            if (ret != IOCode::Ok) {
              err_str = ret.desc;
              goto ErrCase;
            }
            auto key = qp_reply.key;
            return ::rdmaio::Ok(std::make_pair(std::string(""), key));
          }
          case proto::CallbackStatus::ConnectErr:
            err_str = "Remote connect error";
            goto ErrCase;
          case proto::CallbackStatus::WrongArg:
            err_str = "Wrong arguments, possible the QP has exsists";
            goto ErrCase;
          default:
            err_str = err_unknown_status;
          }
        } catch (std::exception &e) {
          err_str = err_decode_reply;
        }
      }
    }

  ErrCase:
    return ::rdmaio::Err(std::make_pair(err_str, temp_key));
  }

  Result<cc_rc_ret_t> cc_rc_msg(const std::string &qp_name,
                                const std::string &channel_name,
                                const usize &msg_sz,
                                const Arc<::rdmaio::qp::RC> rc,
                                const ::rdmaio::nic_id_t &nic_id,
                                const ::rdmaio::qp::QPConfig &config,
                                const double &timeout_usec = 1000000) {

    auto err_str = std::string("unknown error");
    u64 temp_key = 0;

    if (unlikely(qp_name.size() > ::rdmaio::qp::kMaxQPNameLen)) {
      err_str = err_name_to_long;
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
      req.max_recv_sz = msg_sz;

      auto res = rpc.call(proto::CreateRCM,
                          ::rdmaio::Marshal::dump<proto::RCReq>(req));

      // FIXME: below are the same as cc_rc(); maybe refine in a more elegant
      // form
      if (unlikely(res != IOCode::Ok)) {
        err_str = res.desc;
        goto ErrCase;
      }

      auto res_reply = rpc.receive_reply(timeout_usec);

      if (res_reply == IOCode::Ok) {
        try {
          auto qp_reply =
              ::rdmaio::Marshal::dedump<RCReply>(res_reply.desc).value();
          switch (qp_reply.status) {
          case proto::CallbackStatus::Ok: {
            auto ret = rc->connect(qp_reply.attr);
            if (ret != IOCode::Ok) {
              err_str = ret.desc;
              goto ErrCase;
            }
            auto key = qp_reply.key;
            return ::rdmaio::Ok(std::make_pair(std::string(""), key));
          }
          case proto::CallbackStatus::ConnectErr:
            err_str = "Remote connect error";
            goto ErrCase;
          case proto::CallbackStatus::WrongArg:
            err_str = "Wrong arguments, possible the QP has exsists";
            goto ErrCase;
          default:
            err_str = err_unknown_status;
          }
        } catch (std::exception &e) {
          err_str = err_decode_reply;
        }
      }
    }

  ErrCase:
    return ::rdmaio::Err(std::make_pair(err_str, temp_key));
  }

  /*!
    Fetch remote MR identified with "id" at remote machine of this
    connection manager, store the result in the "attr".
   */
  using mr_res_t = std::pair<std::string, rmem::RegAttr>;
  Result<mr_res_t> fetch_remote_mr(const rmem::register_id_t &id,
                                   const double &timeout_usec = 1000000) {
    auto res = rpc.call(proto::FetchMr,
                        ::rdmaio::Marshal::dump<proto::MRReq>({.id = id}));
    auto res_reply = rpc.receive_reply(timeout_usec);
    if (res_reply == IOCode::Ok) {
      // further we check the status by decoding the reply
      try {
        auto mr_reply =
            ::rdmaio::Marshal::dedump<proto::MRReply>(res_reply.desc).value();
        switch (mr_reply.status) {
        case proto::CallbackStatus::Ok:
          return ::rdmaio::Ok(std::make_pair(std::string(""), mr_reply.attr));
        case proto::CallbackStatus::NotFound:
          return NotReady(std::make_pair(std::string(""), rmem::RegAttr()));
        default:
          return ::rdmaio::Err(
              std::make_pair(err_unknown_status, rmem::RegAttr()));
        }

      } catch (std::exception &e) {
        return ::rdmaio::Err(std::make_pair(err_decode_reply, rmem::RegAttr()));
      }
    }
    return ::rdmaio::transfer(res_reply,
                              std::make_pair(res_reply.desc, rmem::RegAttr()));
  }

  /*!
    Fetch remote QP attr, this qp can be either UD PQ or RC QP.
   */
  using qp_attr_ret_t = std::pair<std::string, ::rdmaio::qp::QPAttr>;
  Result<qp_attr_ret_t> fetch_qp_attr(const std::string &name,
                                      const double &timeout_usec = 1000000) {
    auto err_str = std::string("unknown error");
    {
      // 1. first, sanity check the arg
      if (unlikely(name.size() > ::rdmaio::qp::kMaxQPNameLen)) {
        err_str = err_name_to_long;
        goto ErrCase;
      }

      auto req = QPReq();
      memcpy(req.name, name.data(), name.size());

      auto res = rpc.call(proto::RCtrlBinderIdType::FetchQPAttr,
                          ::rdmaio::Marshal::dump<proto::QPReq>(req));

      if (unlikely(res != IOCode::Ok)) {
        err_str = res.desc;
        goto ErrCase;
      }

      auto res_reply = rpc.receive_reply(timeout_usec);
      if (res_reply == IOCode::Ok) {
        try {
          auto qp_reply =
              ::rdmaio::Marshal::dedump<proto::RCReply>(res_reply.desc).value();
          switch (qp_reply.status) {
          case proto::CallbackStatus::Ok:
            return ::rdmaio::Ok(std::make_pair(std::string(""), qp_reply.attr));
          case proto::CallbackStatus::NotFound:
            return NotReady(
                std::make_pair(err_not_found, ::rdmaio::qp::QPAttr()));
          default:
            err_str = err_unknown_status;
          }

        } catch (std::exception &e) {
          err_str = err_decode_reply;
        }

      } else
        return ::rdmaio::transfer(
            res_reply, std::make_pair(res_reply.desc, ::rdmaio::qp::QPAttr()));
    }
  ErrCase:
    return ::rdmaio::Err(std::make_pair(err_str, ::rdmaio::qp::QPAttr()));
  }
};

// a helper for hide wait_ready process
template <class CM = ConnectManager> class CMFactory {
public:
  static Result<Arc<CM>> create(const std::string &addr,
                                const double &timeout_usec,
                                const usize &retry = 1) {
    auto cm = std::make_shared<CM>(addr);
    auto res = cm->wait_ready(timeout_usec, retry);
    if (unlikely(res != IOCode::Ok)) {
      RDMA_LOG(4) << "CM create error with: " << res.code.name() << " "
                  << res.desc;
      return ::rdmaio::transfer(res, Arc<CM>(nullptr));
    }
    return ::rdmaio::Ok(cm);
  }

  /*
   * Future work: currently we assume that CM cm(addr);
   * we could use varargs for creating the CM.
   */
};

} // namespace rdmaio
