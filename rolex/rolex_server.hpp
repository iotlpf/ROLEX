#pragma once


#include "rlib/core/lib.hh"                    /// logging, RNicInfo
#include "r2/src/thread.hh"                   /// Thread
#include "xcomm/tests/transport_util.hh"      /// SimpleAllocator
#include "xcomm/src/transport/rdma_ud_t.hh"   /// UDTransport, UDRecvTransport, UDSessionManager
#include "xcomm/src/rpc/mod.hh"               /// RPCCore
#include "xutils/local_barrier.hh"            /// PBarrier

#include "rolex/trait.hpp"
#include "../benchs/rolex_util_back.hh"


using namespace test;
using namespace xstore::transport;
using namespace xstore::rpc;

extern volatile bool running;
extern ::rdmaio::RCtrl* ctrl;
extern volatile bool init;
extern ::xstore::util::PBarrier* bar;
extern rolex::rolex_t *rolex_index;


namespace rolex {



#define RECV_NUM 2048

using SendTrait = UDTransport;
using RecvTrait = UDRecvTransport<RECV_NUM>;
using SManager = UDSessionManager<RECV_NUM>;
using XThread = ::r2::Thread<usize>;   // <usize> represents the return type of a function

thread_local char* rpc_large_reply_buf = nullptr;
thread_local u32 rpc_large_reply_key;

void rolex_get_callback(const Header& rpc_header, const MemBlock& args, SendTrait* replyc);
void rolex_put_callback(const Header& rpc_header, const MemBlock& args, SendTrait* replyc);
void rolex_update_callback(const Header& rpc_header, const MemBlock& args, SendTrait* replyc);
void rolex_remove_callback(const Header& rpc_header, const MemBlock& args, SendTrait* replyc);
void rolex_scan_callback(const Header& rpc_header, const MemBlock& args, SendTrait* replyc);


auto rolex_server_workers(const usize& nthreads) -> std::vector<std::unique_ptr<XThread>>{
  std::vector<std::unique_ptr<XThread>> res;
  for(int i=0; i<nthreads; i++) {
    res.push_back(std::move(std::make_unique<XThread>([i]()->usize{
      /**
       * @brief Constuct UD qp and register in the RCtrl
       * 
       */
      // create NIC and QP
      auto thread_id = i;
      auto nic_for_recv = RNic::create(RNicInfo::query_dev_names().at(0)).value();
      auto qp_recv = UD::create(nic_for_recv, QPConfig()).value();
      // prepare UD recv buffer
      auto mem_region = HugeRegion::create(64 * 1024 * 1024).value();
      auto mem = mem_region->convert_to_rmem().value();
      auto handler = RegHandler::create(mem, nic_for_recv).value();
      // Post receive buffers to QP and transition QP to RTR state
      SimpleAllocator alloc(mem, handler->get_reg_attr().value());
      auto recv_rs_at_recv =
        RecvEntriesFactory<SimpleAllocator, RECV_NUM, 4096>::create(alloc);
      {
        auto res = qp_recv->post_recvs(*recv_rs_at_recv, RECV_NUM);
        RDMA_ASSERT(res == IOCode::Ok);
      }
      // register the UD for connection
      ctrl->registered_qps.reg("b" + std::to_string(thread_id), qp_recv);
      // LOG(4) << "server thread #" << thread_id << " started!";

      /**
       * @brief Construct RPC
       * 
       */
      RPCCore<SendTrait, RecvTrait, SManager> rpc(12);
      {
        auto large_buf = alloc.alloc_one(1024).value();
        rpc_large_reply_buf = static_cast<char*>(std::get<0>(large_buf));
        rpc_large_reply_key = std::get<1>(large_buf);
      }
      UDRecvTransport<RECV_NUM> recv(qp_recv, recv_rs_at_recv);
      // register the callbacks before enter the main loop
      ASSERT(rpc.reg_callback(rolex_get_callback) == GET);
      ASSERT(rpc.reg_callback(rolex_put_callback) == PUT);
      ASSERT(rpc.reg_callback(rolex_update_callback) == UPDATE);
      ASSERT(rpc.reg_callback(rolex_remove_callback) == DELETE);
      ASSERT(rpc.reg_callback(rolex_scan_callback) == SCAN);
      r2::compile_fence();

      bar->wait();
      while (!init) {
        r2::compile_fence();
      }
      while (running) {
        r2::compile_fence();
        rpc.recv_event_loop(&recv);
      }

      return 0;
    })));
  }
  return std::move(res);
}



void rolex_get_callback(const Header& rpc_header, const MemBlock& args, SendTrait* replyc) {
	// sanity check the requests
  ASSERT(args.sz == sizeof(KeyType));
  KeyType key = *(reinterpret_cast<KeyType*>(args.mem_ptr));
	// GET
	ValType dummy_value = 1234;    //store  the obtained value
	bool res = rolex_index->search(key, dummy_value);
	ReplyValue reply;
  if(res) {
    reply = { .status = true, .val = dummy_value };
  } else {
    reply = { .status = false, .val = dummy_value };
  }
  // send
  char reply_buf[64];
  RPCOp op;
  ASSERT(op.set_msg(MemBlock(reply_buf, 64)).set_reply().add_arg(reply));
  op.set_corid(rpc_header.cor_id);
  // LOG(3)<<"GET: " << *(args.interpret_as<u64>());
  ASSERT(op.execute(replyc) == IOCode::Ok);
}

void rolex_put_callback(const Header& rpc_header, const MemBlock& args, SendTrait* replyc) {
	// sanity check the requests
  ASSERT(args.sz == sizeof(KeyType)+sizeof(ValType));
	KeyType key = *args.interpret_as<KeyType>();
  ValType val = *args.interpret_as<ValType>(sizeof(KeyType));
	// insert
	rolex_index->insert(key, val);
	ReplyValue reply;
	// send
  char reply_buf[64];
  RPCOp op;
  ASSERT(op.set_msg(MemBlock(reply_buf, 64)).set_reply().add_arg(reply));
  op.set_corid(rpc_header.cor_id);
  //LOG(3) << "Put key:" << key;
  ASSERT(op.execute(replyc) == IOCode::Ok);
}


void rolex_update_callback(const Header& rpc_header, const MemBlock& args, SendTrait* replyc){
	// sanity check the requests
  ASSERT(args.sz == sizeof(KeyType)+sizeof(ValType));
	KeyType key = *args.interpret_as<KeyType>();
  ValType val = *args.interpret_as<ValType>(sizeof(KeyType));
	// UPDATE
	bool res = rolex_index->update(key, val);
	ReplyValue reply;
  if(res) {
    reply = { .status = true, .val = val };
  } else {
    reply = { .status = false, .val = val };
  }
  // send
  char reply_buf[64];
  RPCOp op;
  ASSERT(op.set_msg(MemBlock(reply_buf, 64)).set_reply().add_arg(reply));
  op.set_corid(rpc_header.cor_id);
  ASSERT(op.execute(replyc) == IOCode::Ok);
}


void rolex_remove_callback(const Header& rpc_header, const MemBlock& args, SendTrait* replyc){
	// sanity check the requests
  ASSERT(args.sz == sizeof(KeyType));
	KeyType key = *args.interpret_as<KeyType>();
	// UPDATE
	bool res = rolex_index->remove(key);
	ReplyValue reply;
  if(res) {
    reply = { .status = true, .val = 0 };
  } else {
    reply = { .status = false, .val = 0 };
  }
  // send
  char reply_buf[64];
  RPCOp op;
  ASSERT(op.set_msg(MemBlock(reply_buf, 64)).set_reply().add_arg(reply));
  op.set_corid(rpc_header.cor_id);
  ASSERT(op.execute(replyc) == IOCode::Ok);
}


void rolex_scan_callback(const Header& rpc_header, const MemBlock& args, SendTrait* replyc){
	// sanity check the requests
  ASSERT(args.sz == sizeof(KeyType)+sizeof(u64));
	KeyType key = *args.interpret_as<KeyType>();
  ValType n = *args.interpret_as<ValType>(sizeof(KeyType));
	// UPDATE
  std::vector<V> result;
	rolex_index->range(key, n, result);
	ReplyValue reply;
  // send
  char reply_buf[64];
  RPCOp op;
  ASSERT(op.set_msg(MemBlock(reply_buf, 64)).set_reply().add_arg(reply));
  op.set_corid(rpc_header.cor_id);
  ASSERT(op.execute(replyc) == IOCode::Ok);
}
  

}