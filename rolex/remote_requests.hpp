#pragma once

#include "lib.hh"                               /// Arc
#include "r2/src/libroutine.hh"
#include "r2/src/rdma/async_op.hh"              /// AsyncOp
#include "rlib/core/qps/rc.hh"                  /// RC
#include "xcomm/src/batch_rw_op.hh"             /// BatchOp

#include "trait.hpp"


using namespace r2;
using namespace r2::rdma;
using namespace rdma::qp;
using namespace xstore::xcomm;
using namespace xstore;

namespace rolex {

// calculate the offsets of the leaf
inline auto remote_leaf_offsets(u64 num) -> u64 { return sizeof(u64)*2 + num*sizeof(leaf_t); }

void get_remote_leaf(const u64 &leaf_num, const ::xstore::Arc<RC>& data_rc, 
                     char* local_data_buf) 
{
  Op<> leaf_op;
  leaf_op.set_rdma_addr(remote_leaf_offsets(leaf_num), data_rc->remote_mr.value())
         .set_read()
         .set_payload(local_data_buf, sizeof(leaf_t), data_rc->local_mr.value().lkey);
  RDMA_ASSERT(leaf_op.execute(data_rc, IBV_SEND_SIGNALED) == IOCode::Ok);
  RDMA_ASSERT(data_rc->wait_one_comp() == IOCode::Ok);

  leaf_t* leaf = reinterpret_cast<leaf_t*>(local_data_buf);
  leaf->print();
  leaf->insert_not_full(3, 3);
  leaf->print();

  leaf_op.set_rdma_addr(remote_leaf_offsets(leaf_num), data_rc->remote_mr.value())
         .set_write()
         .set_payload(local_data_buf, sizeof(leaf_t), data_rc->local_mr.value().lkey);
  RDMA_ASSERT(leaf_op.execute(data_rc, IBV_SEND_SIGNALED) == IOCode::Ok);
  RDMA_ASSERT(data_rc->wait_one_comp() == IOCode::Ok);
}


void write_remote_leaf(const u64 &leaf_num, const ::xstore::Arc<RC>& data_rc, 
                       char* local_data_buf) 
{
  Op<> leaf_op;
  leaf_op.set_rdma_addr(remote_leaf_offsets(leaf_num), data_rc->remote_mr.value())
         .set_write()
         .set_payload(local_data_buf, sizeof(leaf_t), data_rc->local_mr.value().lkey);
  RDMA_ASSERT(leaf_op.execute(data_rc, IBV_SEND_SIGNALED) == IOCode::Ok);
  RDMA_ASSERT(data_rc->wait_one_comp() == IOCode::Ok);
}


} // namespace rolex