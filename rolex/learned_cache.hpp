#pragma once 

#include "lib.hh"                               /// Arc
#include "r2/src/libroutine.hh"
#include "r2/src/rdma/async_op.hh"              /// AsyncOp
#include "rlib/core/qps/rc.hh"                  /// RC

#include "plr.hpp"
#include "submodel.hpp"
#include "leaf_table.hpp"
#include "rolex_util.hh"
#include "local_connection.hh"


using namespace r2;
using namespace r2::rdma;
using namespace rdma::qp;
using namespace xstore;


namespace rolex {


template<typename K, typename V, typename leaf_t, typename alloc_t, size_t Epsilon=16>
class LearnedCache {
  using model_t = SubModel<K, V, leaf_t, alloc_t, Epsilon>;
  using OptimalPLR = PLR<K, size_t>;

private:
  LocalConnection* LC;
  std::vector<K> model_keys;
  std::vector<u64> model_offs;
  std::vector<model_t> models;

public:
  explicit LearnedCache(LocalConnection* LC) : LC(LC), model_keys(), model_offs(), models() {
    read_remote_index();
    LC->alloc_reset_for_leaf(16*sizeof(leaf_t));
  }

  // synchronize models from memory nodes
  explicit LearnedCache(const std::string_view& seria) : model_keys(), models() {
    ASSERT(seria.size() > sizeof(i32));
    char* cur_ptr = (char *)seria.data();
    i32 model_total_size = ::xstore::util::Marshal<i32>::deserialize(cur_ptr, seria.size());
    cur_ptr += sizeof(i32);
    ASSERT(seria.size() == sizeof(i32) + model_total_size);
    while(cur_ptr - (char *)seria.data()<seria.size()) {
      // model_key
      K key = ::xstore::util::Marshal<K>::deserialize(cur_ptr, seria.size());
      this->model_keys.push_back(key);
      cur_ptr += sizeof(K);
      // model
      i32 mSeria_size = ::xstore::util::Marshal<i32>::deserialize(cur_ptr, seria.size());
      cur_ptr += sizeof(i32);
      std::string mSeria(cur_ptr, mSeria_size);
      this->models.emplace_back(mSeria);
      cur_ptr += mSeria_size;
    }
  }

  // ========= API functions to access remote data : search, update, insert, remove ===========
  /**
   * @brief Obtain val of key from the remote machines with RDMA read
   *            NOTE: this functions sequentially read data, which is inefficient
   * @param data_rc contains the remote memory region, and rkey, lkey
   * @param local_data_buf contains the obtained data, this buffer is registed with data_rc
   */
  template<typename rc_t>
  auto search(const K &key, V &val, rc_t& data_rc, char *local_data_buf) -> bool {
    std::vector<leaf_addr_t> leaves;
    models[model_for_key(key)].get_leaf_addr(key, leaves);
    Op<> leaf_op;
    for(int i=leaves.size()-1; i>=0; i--) {
      leaf_op.set_rdma_addr(remote_leaf_offsets(leaves[i].addr.leaf_num), data_rc->remote_mr.value())
             .set_read()
             .set_payload(local_data_buf, sizeof(leaf_t), data_rc->local_mr.value().lkey);
      RDMA_ASSERT(leaf_op.execute(data_rc, IBV_SEND_SIGNALED) == IOCode::Ok);
      RDMA_ASSERT(data_rc->wait_one_comp() == IOCode::Ok);
      leaf_t* leaf = reinterpret_cast<leaf_t*>(local_data_buf);
      if(leaf->search(key, val)) return true;
    }
    return false;
  }

  auto search_syn(const K &key, V &val) -> bool {
    std::vector<leaf_addr_t> leaves;
    models[model_for_key(key)].get_leaf_addr(key, leaves);
    // read remote leaves
    auto leaf_buf = LC->get_leaf_buf();
    
    
    return false;
  }

  auto search_asyn(const K &key, V &val, R2_ASYNC) -> bool {
    // get addr
    auto model_idx = model_for_key(key);
    std::vector<leaf_addr_t> leaves;
    models[model_idx].get_leaf_addr(key, leaves);
    // read remote leaves
    auto leaf_buf = LC->get_leaf_buf();
    LC->read_leaves_asyn(leaves, leaf_buf, sizeof(leaf_t), R2_ASYNC_WAIT);
    // search the leaves
    for(int i=0; i<leaves.size(); i++) {
      leaf_t* leaf = reinterpret_cast<leaf_t*>(leaf_buf+i*sizeof(leaf_t));
      if(leaf->search(key, val)) return true;
    }
    return false;
  }


  template<typename rc_t>
  auto insert(const K &key, const V &val, rc_t& data_rc, char *local_data_buf) -> bool {
    // 1. obtain remote addresses of leaves
    std::vector<leaf_addr_t> leaves;
    models[model_for_key(key)].get_leaf_addr(key, leaves);

    // 2. read leaves from memory node
    Op<> leaf_op;
    for(int i=leaves.size()-1; i>=0; i--) {
      leaf_op.set_rdma_addr(remote_leaf_offsets(leaves[i].addr.leaf_num), data_rc->remote_mr.value())
             .set_read()
             .set_payload(local_data_buf, sizeof(leaf_t), data_rc->local_mr.value().lkey);
      RDMA_ASSERT(leaf_op.execute(data_rc, IBV_SEND_SIGNALED) == IOCode::Ok);
      RDMA_ASSERT(data_rc->wait_one_comp() == IOCode::Ok);
      leaf_t* leaf = reinterpret_cast<leaf_t*>(local_data_buf);
      
      // 3. lock, insert and write the leaf
      if(leaf->insertHere(key)) {
        // 3.1 lock, fixme: use remote lock
        
        // insert and write back
        if(!leaf->isfull()) {
          leaf->insert_not_full(key, val);
          leaf_op.set_rdma_addr(remote_leaf_offsets(leaves[i].addr.leaf_num), data_rc->remote_mr.value())
                 .set_write()
                 .set_payload(local_data_buf, sizeof(leaf_t), data_rc->local_mr.value().lkey);
          RDMA_ASSERT(leaf_op.execute(data_rc, IBV_SEND_SIGNALED) == IOCode::Ok);
          RDMA_ASSERT(data_rc->wait_one_comp() == IOCode::Ok);
          return true;
        }
        
        // create a new synonym leaf
        LOG(2) << "Need to create a synonym leaf";
        Op<> atomic_op;
        atomic_op.set_atomic_rbuf(reinterpret_cast<u64 *>(data_rc->remote_mr.value().buf), data_rc->remote_mr.value().key)
                 .set_fetch_add(1)
                 .set_payload(local_data_buf, sizeof(u64), data_rc->local_mr.value().lkey);
        ASSERT(atomic_op.execute(data_rc, IBV_SEND_SIGNALED) == ::rdmaio::IOCode::Ok);
        ASSERT(data_rc->wait_one_comp() == IOCode::Ok);
        auto new_leaf_num = ::xstore::util::Marshal<size_t>::deserialize(local_data_buf, sizeof(u64));
        // fixme: read synonym leaf


        return false;
      }
    }
    return false;
  }

  auto insert_asyn(const K &key, const V &val, R2_ASYNC) -> bool {
    // 1. obtain remote addresses of leaves
    auto model_idx = model_for_key(key);
    std::vector<leaf_addr_t> leaves;
    models[model_idx].get_leaf_addr(key, leaves);

    // 2. read leaves from memory node
    auto leaf_buf = LC->get_leaf_buf();
    LC->read_leaves_asyn(leaves, leaf_buf, sizeof(leaf_t), R2_ASYNC_WAIT);

    // 3. lock, insert and write the leaf
    for(int i=leaves.size()-1; i>=0; i--) {
      leaf_t* leaf = reinterpret_cast<leaf_t*>(leaf_buf+i*sizeof(leaf_t));
      
      if(leaf->insertHere(key)) {
        // lock: fixme lock
        
        
      }
    }

    return false;
  }



  // ============== functions for debugging ================
  void print() {
    for(int i=0; i<models.size(); i++){
      LOG(3)<<"Submodel " << i <<", model_key: "<<model_keys[i];
      models[i].print();
    }
  }

private:
  auto read_remote_index() {
    ASSERT(LC) << "LocalConnection is nullptr.";
    // read the number of remote models
    auto model_size_buf = LC->get_buf(sizeof(u64));
    LC->read_syn(0, model_size_buf, sizeof(u64));
    u64 total_size;
    memcpy(&total_size, model_size_buf, sizeof(u64));
    LOG(4) << "Read the number of remote models: "<<total_size;

    // read model keys/offs
    auto model_keys_buf = LC->get_buf(sizeof(K)*total_size);
    auto model_offs_buf = LC->get_buf(sizeof(u64)*total_size);
    LC->read_syn(sizeof(u64), model_keys_buf, sizeof(K)*total_size);
    LC->read_syn(kUpperModel/2, model_offs_buf, sizeof(u64)*total_size);
    for(int i=0; i<total_size; i++) {
      K key;
      u64 off;
      memcpy(&key, model_keys_buf+sizeof(K)*i, sizeof(K));
      memcpy(&off, model_offs_buf+sizeof(u64)*i, sizeof(u64));
      model_keys.emplace_back(key);
      model_offs.emplace_back(off);
    }

    // read submodels
    char *model_buf;
    i32 model_buf_size = 0;
    for(int i=0; i<total_size; i++) {
      i32 cur_ms;
      LC->read_syn(model_offs[i], model_size_buf, sizeof(i32));
      memcpy(&cur_ms, model_size_buf, sizeof(i32));
      // LOG(2)<<"Model "<<i<<" size: "<<cur_ms<<", offset: "<<model_offs[i];

      if(model_buf_size < cur_ms) {
        model_buf_size = cur_ms;
        model_buf = LC->get_buf(model_buf_size);
      }
      // read model
      LC->read_syn(model_offs[i]+sizeof(i32), model_buf, cur_ms);
      std::string rSeria(model_buf, cur_ms);
      model_t model(rSeria);
      models.emplace_back(model);
    }
  }

  auto model_for_key(const K &key) -> usize {
    auto idx = binary_search_branchless(&model_keys[0], model_keys.size(), key);
    return idx<models.size()? idx:(models.size()-1);
  }

  inline auto remote_leaf_offsets(u64 num) -> u64 { return sizeof(u64)*2 + num*sizeof(leaf_t); }

};



} // namespace rolex