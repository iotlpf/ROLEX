#pragma once


#include "rlib/core/nicinfo.hh"               /// RNicInfo
#include "rlib/core/lib.hh"                   /// ConnectManager
#include "rlib/core/qps/rc.hh"                /// RC
#include "r2/src/libroutine.hh"               /// R2_ASYNC
#include "r2/src/rdma/async_op.hh"            /// AsyncOP
#include "rolex/local_allocator.hh"

using namespace r2;
using namespace r2::rdma;
using namespace rdmaio;
using namespace rdmaio::qp;                  /// RC



namespace rolex {

class LC_config {
public:
  std::string server_addr;
  int client_id;
  int thread_id;
  u32 nic_idx;
  u64 alloc_mem_size;

  u64 reg_model_region;
  u64 reg_leaf_region;

  explicit LC_config(std::string addr, int thread_id, u64 rmr, u64 rlr, u64 ams, u32 ni = 0, int client_id = 0) 
    : server_addr(addr), client_id(client_id), reg_model_region(rmr), reg_leaf_region(rlr),
      thread_id(thread_id), alloc_mem_size(ams), nic_idx(ni) {}
};

class LocalConnection {

public:
  explicit LocalConnection(const LC_config &conf) 
      : conf(conf), _nic(RNic::create(RNicInfo::query_dev_names().at(conf.nic_idx)).value()) {
    local_memory_allocator();
    connect_remote();
    model_rc = create_rc(conf.client_id + " model-qp" + std::to_string(conf.thread_id), conf.reg_model_region);
    data_rc  = create_rc(conf.client_id + " data-qp" + std::to_string(conf.thread_id), conf.reg_leaf_region);
  }

  std::shared_ptr<RC> get_model_rc() { return this->model_rc; }

  std::shared_ptr<RC> get_data_rc() { return this->data_rc; }

  // ===== functions for allocator ===========
  auto get_buf(const usize &sz) -> char * {
    return reinterpret_cast<char*>(localAlloc->alloc(sz));
  }

  auto get_leaf_buf() -> char* {
    return reinterpret_cast<char*>(localAlloc->get_leaf_buf());
  }

  void alloc_reset_for_leaf(const usize sz) { localAlloc->reset_for_leaf(sz); }

  // ============ functions for remote read ===================
  // using for model_rc
  void read_syn(const u64 &remote_off, char *local_buf, const u32 &length) {
    Op<> op;
    op.set_rdma_addr(remote_off, model_rc->remote_mr.value())
      .set_read()
      .set_payload(local_buf, length, model_rc->local_mr.value().lkey);
    ASSERT(op.execute(model_rc, IBV_SEND_SIGNALED) == IOCode::Ok);
    ASSERT(model_rc->wait_one_comp() == IOCode::Ok);
  }

  // using for data_rc
  void read_leaves_asyn(std::vector<leaf_addr_t> leaves, char *local_buf, const u32 &each_len, R2_ASYNC) {
    AsyncOp<1> op;
    op.set_read()
      .set_rdma_addr(sizeof(u64)*2 + leaves[0].addr.leaf_num*each_len, data_rc->remote_mr.value())
      .set_payload(
        local_buf, each_len, data_rc->local_mr.value().lkey);
    auto ret = op.execute_async(data_rc, IBV_SEND_SIGNALED, R2_ASYNC_WAIT);
    ASSERT(ret == ::rdmaio::IOCode::Ok);
  }

  void read_leaves_syn(std::vector<leaf_addr_t> leaves, char *local_buf, const u32 &each_len) {
    
  }

  // =========== functions for remote write and locks ================
  // lock and unlock
  bool cas(char *local_buf, uint64_t equal, uint64_t val, u64 off) {
    Op<> atomic_op;
    // cas
    auto rbuf = reinterpret_cast<char *>(data_rc->remote_mr.value().buf) + off;
    atomic_op.set_atomic_rbuf(reinterpret_cast<u64 *>(rbuf), data_rc->remote_mr.value().key)
             .set_cas(equal, val)
             .set_payload(local_buf, sizeof(u64), data_rc->local_mr.value().lkey);
    ASSERT(atomic_op.execute(data_rc, IBV_SEND_SIGNALED) == ::rdmaio::IOCode::Ok);
    ASSERT(data_rc->wait_one_comp() == IOCode::Ok);
    auto r_data = ::xstore::util::Marshal<size_t>::deserialize(local_buf, sizeof(u64));
    
    return equal == r_data;
  }

private:
  LC_config conf;
  std::shared_ptr<RNic> _nic;
  std::shared_ptr<RegHandler> handler;
  LocalAllocator* localAlloc;
  ConnectManager* _cm;
  std::shared_ptr<RC> model_rc;
  std::shared_ptr<RC> data_rc;

  void local_memory_allocator() {
    auto mem_region1 = rolex::HugeRegion::create(conf.alloc_mem_size).value();
    auto mem = mem_region1->convert_to_rmem().value();
    handler = RegHandler::create(mem, _nic).value();
    localAlloc = new LocalAllocator(mem, handler->get_reg_attr().value());
  }

  void connect_remote() {
    _cm = new ConnectManager(conf.server_addr);
    if (_cm->wait_ready(1000000, 2) ==
          IOCode::Timeout) // wait 1 second for server to ready, retry 2
                           // times
        RDMA_ASSERT(false) << "cm connect to server timeout";
  }

  auto create_rc(std::string qp_name, const u64 &reg_mr) -> std::shared_ptr<RC> {
    auto rc = RC::create(_nic, QPConfig()).value();
    auto qp_res =
      _cm->cc_rc(qp_name,
                 rc,
                 conf.nic_idx,
                 QPConfig());
    RDMA_ASSERT(qp_res == IOCode::Ok) << std::get<0>(qp_res.desc);
    auto fetch_res = _cm->fetch_remote_mr(reg_mr);
    RDMA_ASSERT(fetch_res == IOCode::Ok) << std::get<0>(fetch_res.desc);
    rmem::RegAttr remote_attr = std::get<1>(fetch_res.desc);
    // bind the remote Memory and local Memory
    rc->bind_remote_mr(remote_attr);
    rc->bind_local_mr(handler->get_reg_attr().value());
    return rc;
  }
};



}