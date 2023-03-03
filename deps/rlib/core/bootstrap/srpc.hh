#pragma once

#include <mutex>   // lock
#include <utility> // std::pair

#include "./channel.hh"
#include "./multi_msg.hh"
#include "./proto.hh"

namespace rdmaio {

namespace bootstrap {

using namespace proto;

enum CallStatus : u8 {
  Ok = 0,
  Nop,
  WrongReply,
  NotMatch,
  FatalErr,
};

struct __attribute__((packed)) SRpcHeader {
  rpc_id_t id;
  u64 checksum;
};

struct __attribute__((packed)) SReplyHeader {
  u8 callstatus;
  u64 checksum;
  // if dummy is euqal to 1,
  // then we will omit the checksum check at client
  // because it is a heartbeat reply message
  u8 dummy = 0;
};

/*!
  A simple RPC used for establish connection for RDMA.
 */
class SRpc {
public:
  static const u64 invalid_checksum = 0;

private:
  Arc<SendChannel> channel;
  u64 checksum = invalid_checksum + 1;

public:
  using MMsg = MultiMsg<kMaxMsgSz>;
  explicit SRpc(const std::string &addr)
      : channel(SendChannel::create(addr).value()) {}

  /*!
    Send an RPC with id "id", using a specificed parameter.
    */
  Result<std::string> call(const rpc_id_t &id, const ByteBuffer &parameter) {
    auto mmsg_o = MMsg::create_exact(sizeof(rpc_id_t) + parameter.size());
    if (mmsg_o) {
      auto &mmsg = mmsg_o.value();
      RDMA_ASSERT(mmsg.append(::rdmaio::Marshal::dump<SRpcHeader>(
          {.id = id, .checksum = checksum})));
      RDMA_ASSERT(mmsg.append(parameter));
      return channel->send(*mmsg.buf);
    } else {
      return ::rdmaio::Err(
          std::string("Msg too large!, only kMaxMsgSz supported"));
    }
  }

  /*!
    Recv a reply from the server ysing the timeout specified.
    \Note: this call must follow from a "call"
    */
  Result<ByteBuffer> receive_reply(const double &timeout_usec = 1000000,
                                   bool heartbeat = false) {
  retry:
    auto res = channel->recv(timeout_usec);
    if (res == IOCode::Ok) {

      // further we decode the header for check
      try {

        auto decoded_reply = MultiMsg<kMaxMsgSz>::create_from(res.desc).value();

        auto header = ::rdmaio::Marshal::dedump<SReplyHeader>(
                          decoded_reply.query_one(0).value())
                          .value();

        // first we handle heartbeat reply
        if (header.dummy) {
          if (!heartbeat)
            goto retry; // ignore the heartbeat reply

          return ::rdmaio::Ok(ByteBuffer(""));
        }

        // then we handle normal reply
        if (header.checksum == checksum) {
          checksum += 1;
          switch (header.callstatus) {
          case CallStatus::Ok:
            return ::rdmaio::Ok(decoded_reply.query_one(1).value());
          case CallStatus::Nop:
            return ::rdmaio::Err(ByteBuffer("Not ready"));
          default:
            return ::rdmaio::Err(ByteBuffer("unknown error"));
          }
        } else
          return ::rdmaio::Err(ByteBuffer("Fatal checksum error"));
      } catch (std::exception &e) {

        return ::rdmaio::Err(ByteBuffer("decode reply error"));
      }
    } else {
      // the receive has error, just return
      return res;
    }
  }
};

class SRpcHandler;
class RPCFactory {
  friend class SRpcHandler;
  /*!
  A simple RPC function:
  handle(const ByteBuffer &req) -> ByteBuffer
   */
  using req_handler_f = std::function<ByteBuffer(const ByteBuffer &req)>;
  std::map<rpc_id_t, req_handler_f> registered_handlers;

  std::mutex lock;

  RPCFactory() {
    // register a default heartbeat handler
    register_handler(RCtrlBinderIdType::HeartBeat,
                     &RPCFactory::heartbeat_handler);
  };

public:
  bool register_handler(rpc_id_t id, req_handler_f val) {
    std::lock_guard<std::mutex> guard(lock);
    if (registered_handlers.find(id) == registered_handlers.end()) {
      registered_handlers.insert(std::make_pair(id, val));
      return true;
    }
    return false;
  }

  ByteBuffer call_one(rpc_id_t id, const ByteBuffer &parameter) {
    auto fn = registered_handlers.find(id)->second;
    return fn(parameter);
  }

private:
  static ByteBuffer heartbeat_handler(const ByteBuffer &b) {
    return ByteBuffer("1"); // a null reply is ok
  }
};

class SRpcHandler {
  Arc<RecvChannel> channel;
  RPCFactory factory;

public:
  explicit SRpcHandler(const usize &port, const std::string &h = "localhost")
      : channel(RecvChannel::create(port, h).value()) {}

  bool register_handler(rpc_id_t id, RPCFactory::req_handler_f val) {
    return factory.register_handler(id, val);
  }

  /*!
    Run a event loop to call received RPC calls
    \ret: number of PRCs served
   */
  usize run_one_event_loop() {
    usize count = 0;
    for (channel->start(1000000); channel->has_msg(); channel->next(), count += 1) {

      auto &msg = channel->cur();

      u64 checksum = SRpc::invalid_checksum;
      try {
        MultiMsg<kMaxMsgSz> segmeneted_msg;
        SRpcHeader header;
        try {
          // create from move the cur_msg to a MuiltiMsg
          segmeneted_msg = MultiMsg<kMaxMsgSz>::create_from(msg).value();

          // query the RPC call id
          header = ::rdmaio::Marshal::dedump<SRpcHeader>(
                       segmeneted_msg.query_one(0).value())
                       .value();

          checksum = header.checksum;
        } catch (std::exception &e) {
          // some error happens, which is fatal because we cannot decode the
          // checksum

          MultiMsg<kMaxMsgSz> coded_reply =
            MultiMsg<kMaxMsgSz>::create_exact(sizeof(SReplyHeader)).value();

          coded_reply.append(::rdmaio::Marshal::dump<SReplyHeader>(
              {.callstatus = CallStatus::FatalErr,
               .checksum = checksum,
               .dummy = 0}));
          channel->reply_cur(*coded_reply.buf);

          continue;
        }

        // really handles the request
        rpc_id_t id = header.id;

        ByteBuffer parameter = segmeneted_msg.query_one(1).value();

        // call the RPC
        ByteBuffer reply = factory.call_one(id, parameter);

        MultiMsg<kMaxMsgSz> coded_reply =
            MultiMsg<kMaxMsgSz>::create_exact(sizeof(SReplyHeader) +
                                              reply.size())
                .value();
        coded_reply.append(::rdmaio::Marshal::dump<SReplyHeader>(
            {.callstatus = CallStatus::Ok,
             .checksum = checksum,
             .dummy = (id == RCtrlBinderIdType::HeartBeat)
                          ? static_cast<u8>(1)
                          : static_cast<u8>(0)}));
        coded_reply.append(reply);

        // send the reply to the client
        channel->reply_cur(*coded_reply.buf);

      } catch (std::exception &e) {
        MultiMsg<kMaxMsgSz> coded_reply =
          MultiMsg<kMaxMsgSz>::create_exact(sizeof(SReplyHeader)).value();

        // some error happens
        coded_reply.append(::rdmaio::Marshal::dump<SReplyHeader>(
            {.callstatus = CallStatus::Nop, .checksum = checksum}));
        channel->reply_cur(*coded_reply.buf);
      }
    }

    return count;
  }
};

} // namespace bootstrap

} // namespace rdmaio
