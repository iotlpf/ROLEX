#pragma once

#include "../core/qps/op.hh"
#include "../tests/random.hh"

namespace rdmaio {

using namespace rdmaio;
using namespace rdmaio::qp;

template <usize NSGE = 1>
struct BenchOp : Op<NSGE> {
  op_type type;
  u64 *lbuf_base;
  u64 *rbuf_base;

  u64 rbuf_random;
  u64 lbuf_random;

  test::FastRandom rand;

 public:
  BenchOp<NSGE>(int type = 0) : Op<NSGE>() {
    lbuf_base = nullptr;
    rbuf_base = nullptr;
    rand = test::FastRandom();
    this->set_type(type);
  }

  inline auto valid() -> bool {
    return (lbuf_base != nullptr) && (rbuf_base != nullptr);
  }

  inline BenchOp &set_type(int type) {
    this->type = (op_type)type;
    switch (this->type) {
      case RDMA_READ:
        this->set_read();
        break;

      case RDMA_WRITE:
        this->set_write();
        break;

      case ATOMIC_CAS:
        this->set_cas(0, 0);
        break;
      case ATOMIC_FAA:
        this->set_fetch_add(0);
        break;

      default:
        break;
    }
    return *this;
  }

  inline BenchOp &init_rbuf(u64 *ra, const u32 &rk, u64 random = 10) {
    this->rbuf_base = ra;
    this->rbuf_random = random;
    switch (type) {
      case RDMA_READ:
      case RDMA_WRITE:
        this->set_rdma_rbuf(ra, rk);
        break;

      case ATOMIC_CAS:
      case ATOMIC_FAA:
        this->set_atomic_rbuf(ra, rk);
        break;

      default:
        break;
    }
    return *this;
  }

  inline BenchOp &init_lbuf(u64 *la, const u32 &length, const u32 &lk,
                            u64 random = 10) {
    this->lbuf_base = la;
    this->lbuf_random = random;
    this->set_payload(la, length, lk);
    return *this;
  }

  inline auto refresh(){
    if(!valid()) return;
    u64 *ra = rbuf_base + (this->rand.next() % rbuf_random);
    u64 *la = lbuf_base + (this->rand.next() % lbuf_random);
    switch (type) {
      case RDMA_READ:
        this->wr.wr.rdma.remote_addr = (u64)ra;
        break;
      case RDMA_WRITE:
        this->wr.wr.rdma.remote_addr = (u64)ra;
        this->sges[0].addr = (u64)la;
        break;
      case ATOMIC_CAS:
        this->wr.wr.atomic.remote_addr = (u64)ra;
        this->sges[0].addr = (u64)la;
        this->set_cas(this->rand.next() % 100, this->rand.next() % 100);
        break;
      case ATOMIC_FAA:
        this->wr.wr.atomic.remote_addr = (u64)ra;
        this->set_fetch_add(this->rand.next() % 100);
        this->sges[0].addr = (u64)la;
        break;
      default:
        break;
    }
  }
};  // namespace rdma

}  // namespace rdmaio
