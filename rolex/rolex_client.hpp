#pragma once 


#include <gflags/gflags.h>
#include "r2/src/logging.hh"                  /// logging
#include "r2/src/thread.hh"                   /// Thread
#include "r2/src/libroutine.hh"               /// coroutine
#include "rlib/core/nicinfo.hh"               /// RNicInfo
#include "xcomm/tests/transport_util.hh"      /// SimpleAllocator
#include "xcomm/src/transport/rdma_ud_t.hh"   /// UDTranstrant, UDRecvTransport, UDSessionManager
#include "xcomm/src/rpc/mod.hh"               /// RPCCore
#include "xutils/local_barrier.hh"            /// PBarrier


extern volatile bool running;
extern ::xstore::util::PBarrier* bar;

namespace rolex {

using namespace r2;
using namespace rdmaio;
using namespace test;
using namespace xstore::util;
using namespace xstore::rpc;
using namespace xstore::transport;

using XThread = ::r2::Thread<usize>;   // <usize> represents the return type of a function
using SendTrait = UDTransport;
using RecvTrait = UDRecvTransport<2048>;
using SManager = UDSessionManager<2048>;

using RPC = RPCCore<SendTrait, RecvTrait, SManager>;


auto remote_search(const KeyType& key, RPC& rpc, UDTransport& sender, R2_ASYNC) -> ::r2::Option<ValType>;
void remote_put(const KeyType& key, const ValType& val, RPC& rpc, UDTransport& sender, R2_ASYNC);
void remote_update(const KeyType& key, const ValType& val, RPC& rpc, UDTransport& sender, R2_ASYNC);
void remote_remove(const KeyType& key, RPC& rpc, UDTransport& sender, R2_ASYNC);
void remote_scan(const KeyType& key, const u64& n, RPC& rpc, UDTransport& sender, R2_ASYNC);


auto rolex_client_worker(const usize& nthreads) -> std::vector<std::unique_ptr<XThread>> {
  std::vector<std::unique_ptr<XThread>> workers;
  for(uint thread_id = 0; thread_id < nthreads; ++thread_id) {
    workers.push_back(std::move(std::make_unique<XThread>([thread_id, nthreads]() -> usize {
      /**
       * Prepare UD qp: create NIC, UD, allocator, post
       */ 
      // create NIC and qps
      usize nic_idx = 0;
      auto nic_for_sender = RNic::create(RNicInfo::query_dev_names().at(nic_idx)).value();
      auto ud_qp = UD::create(nic_for_sender, QPConfig()).value();
      // Register the memory
      auto mem_region1 = HugeRegion::create(16 * 1024 * 1024).value();
      auto mem1 = mem_region1->convert_to_rmem().value();
      auto handler1 = RegHandler::create(mem1, nic_for_sender).value();
      SimpleAllocator alloc1(mem1, handler1->get_reg_attr().value());
      auto recv_rs_at_send = RecvEntriesFactory<SimpleAllocator, 2048, 1024>::create(alloc1);
      {
        auto res = ud_qp->post_recvs(*recv_rs_at_send, 2048);
        RDMA_ASSERT(res == IOCode::Ok);
      }
      /**
       * @brief connect with the remote machine with UD
       * 
       */
      //std::string server_addr = "192.168.3.101:8888";
      std::string server_addr = "10.0.0.1:8899";
      int ud_id = thread_id;
      UDTransport sender;
      {
        r2::Timer t;
        do {
          auto res = sender.connect(
            server_addr, "b" + std::to_string(ud_id), thread_id, ud_qp);
          if (res == IOCode::Ok) {
            LOG(2) << "Thread " << thread_id << " connect to remote server";
            break;
          }
          if (t.passed_sec() >= 10) {
            ASSERT(false) << "conn failed at thread:" << thread_id;
          }
        } while (t.passed_sec() < 10);
      }
      /**
       * @brief Construct rpc for communication
       * 
       */
      RPCCore<SendTrait, RecvTrait, SManager> rpc(12);
      auto send_buf = std::get<0>(alloc1.alloc_one(4096).value());
      ASSERT(send_buf != nullptr);
      auto lkey = handler1->get_reg_attr().value().key;
      memset(send_buf, 0, 4096);
      // 0. connect the RPC
      // first we send the connect transport
      auto conn_op = RPCOp::get_connect_op(MemBlock(send_buf, 2048),
                                           sender.get_connect_data().value());
      ASSERT(conn_op.execute_w_key(&sender, lkey) == IOCode::Ok);
      UDRecvTransport<2048> recv_s(ud_qp, recv_rs_at_send);
      /**
       * @brief Generate test data
       *        Send RPC requests
       * 
       */
      // used for other schemes
      size_t non_exist_key_n_per_thread = nonexist_keys.size() / nthreads;
      size_t non_exist_key_start = thread_id * non_exist_key_n_per_thread;
      size_t non_exist_key_end = (thread_id + 1) * non_exist_key_n_per_thread;
      std::vector<u64> op_keys(nonexist_keys.begin() + non_exist_key_start,
                               nonexist_keys.begin() + non_exist_key_end);
      size_t query_i = 0, insert_i = 0, remove_i = 0, update_i = 0;

      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_real_distribution<> ratio_dis(0, 1);

      SScheduler ssched;
      rpc.reg_poll_future(ssched, &recv_s);
      bar->wait();

      /**
       * @brief using coroutines for testing
       * 
       */ 
      if(bench::BenConfig.workloads >= NORMAL) {
        for(int i=0; i<BenConfig.coros; i++) {
          ssched.spawn([send_buf, &rpc, &sender, &recv_s, lkey, 
                        thread_id, 
                        &ratio_dis, &gen,
                        &query_i, &insert_i, &remove_i, &update_i](R2_ASYNC) {
            char reply_buf[1024];
            RPCOp op;

            while(running) {
              double d = ratio_dis(gen);
              if(d <= BenConfig.read_ratio) {    // search
                KeyType dummy_key = exist_keys[query_i % exist_keys.size()];
                auto res = remote_search(dummy_key, rpc, sender, R2_ASYNC_WAIT);
                query_i++;
                if (unlikely(query_i == exist_keys.size())) {
                  query_i = 0;
                }
              } else if(d <= BenConfig.read_ratio+BenConfig.insert_ratio) {                      // insert
                KeyType dummy_key = nonexist_keys[insert_i % nonexist_keys.size()];
                remote_put(dummy_key, dummy_key, rpc, sender, R2_ASYNC_WAIT);
                insert_i++;
                if (unlikely(insert_i == nonexist_keys.size())) {
                    insert_i = 0;
                }
              } else if(d<=BenConfig.read_ratio+BenConfig.insert_ratio+BenConfig.update_ratio) {      // update
                KeyType dummy_key = exist_keys[update_i % exist_keys.size()];
                remote_update(dummy_key, dummy_key, rpc, sender, R2_ASYNC_WAIT);
                update_i++;
                if (unlikely(update_i == exist_keys.size())) {
                    update_i = 0;
                }
              } else {
                KeyType dummy_key = exist_keys[remove_i % exist_keys.size()];
                remote_remove(dummy_key, rpc, sender, R2_ASYNC_WAIT);
                remove_i++;
                if (unlikely(remove_i == exist_keys.size())) {
                    remove_i = 0;
                }
              }
            }
            if (R2_COR_ID() == BenConfig.coros) {
              R2_STOP();
            }
            R2_RET;
          });
        }
      }
      ssched.run();
      return 0;
    })));
  };
  return std::move(workers);
}



auto remote_search(const KeyType& key,
          RPC& rpc,
          UDTransport& sender,
          R2_ASYNC) -> ::r2::Option<ValType>
{

  char send_buf[64];
  char reply_buf[sizeof(ReplyValue)];

  RPCOp op;
  op.set_msg(MemBlock(send_buf, 64))
    .set_req()
    .set_rpc_id(GET)
    .set_corid(R2_COR_ID())
    .add_one_reply(rpc.reply_station,
                   { .mem_ptr = reply_buf, .sz = sizeof(ReplyValue) })
    .add_arg<KeyType>(key);
  ASSERT(rpc.reply_station.cor_ready(R2_COR_ID()) == false);
  auto ret = op.execute_w_key(&sender, 0);
  ASSERT(ret == IOCode::Ok);

  // yield the coroutine to wait for reply
  R2_PAUSE_AND_YIELD;

  // check the rest
  ReplyValue r = *(reinterpret_cast<ReplyValue*>(reply_buf));
  if(r.status) {
    return (ValType)r.val;
  }
  return {};
}



void remote_put(const KeyType& key, const ValType& val, RPC& rpc, UDTransport& sender, R2_ASYNC)
{
  std::string data;
  data += ::xstore::util::Marshal<KeyType>::serialize_to(key);
  data += ::xstore::util::Marshal<ValType>::serialize_to(val);

  char send_buf[64];
  char reply_buf[sizeof(ReplyValue)];
  RPCOp op;
  op.set_msg(MemBlock(send_buf, 64))
    .set_req()
    .set_rpc_id(PUT)
    .set_corid(R2_COR_ID())
    .add_one_reply(rpc.reply_station,
                   { .mem_ptr = reply_buf, .sz = sizeof(ReplyValue) })
    .add_opaque(data);
  ASSERT(op.execute_w_key(&sender, 0) == IOCode::Ok);

  // yield to the next coroutine
  R2_PAUSE_AND_YIELD;
}


void remote_update(const KeyType& key, const ValType& val, RPC& rpc, UDTransport& sender, R2_ASYNC)
{
  std::string data;
  data += ::xstore::util::Marshal<KeyType>::serialize_to(key);
  data += ::xstore::util::Marshal<ValType>::serialize_to(val);

  char send_buf[64];
  char reply_buf[sizeof(ReplyValue)];
  RPCOp op;
  op.set_msg(MemBlock(send_buf, 64))
    .set_req()
    .set_rpc_id(UPDATE)
    .set_corid(R2_COR_ID())
    .add_one_reply(rpc.reply_station,
                   { .mem_ptr = reply_buf, .sz = sizeof(ReplyValue) })
    .add_opaque(data);
  ASSERT(op.execute_w_key(&sender, 0) == IOCode::Ok);

  // yield to the next coroutine
  R2_PAUSE_AND_YIELD;
}


void remote_remove(const KeyType& key, RPC& rpc, UDTransport& sender, R2_ASYNC)
{
  std::string data;
  data += ::xstore::util::Marshal<KeyType>::serialize_to(key);

  char send_buf[64];
  char reply_buf[sizeof(ReplyValue)];
  RPCOp op;
  op.set_msg(MemBlock(send_buf, 64))
    .set_req()
    .set_rpc_id(DELETE)
    .set_corid(R2_COR_ID())
    .add_one_reply(rpc.reply_station,
                   { .mem_ptr = reply_buf, .sz = sizeof(ReplyValue) })
    .add_opaque(data);
  ASSERT(op.execute_w_key(&sender, 0) == IOCode::Ok);

  // yield to the next coroutine
  R2_PAUSE_AND_YIELD;
}

void remote_scan(const KeyType& key, const u64& n, RPC& rpc, UDTransport& sender, R2_ASYNC)
{
  std::string data;
  data += ::xstore::util::Marshal<KeyType>::serialize_to(key);
  data += ::xstore::util::Marshal<u64>::serialize_to(n);

  char send_buf[64];
  char reply_buf[sizeof(ReplyValue)];
  RPCOp op;
  op.set_msg(MemBlock(send_buf, 64))
    .set_req()
    .set_rpc_id(SCAN)
    .set_corid(R2_COR_ID())
    .add_one_reply(rpc.reply_station,
                   { .mem_ptr = reply_buf, .sz = sizeof(ReplyValue) })
    .add_opaque(data);
  ASSERT(op.execute_w_key(&sender, 0) == IOCode::Ok);

  // yield to the next coroutine
  R2_PAUSE_AND_YIELD;
}


}