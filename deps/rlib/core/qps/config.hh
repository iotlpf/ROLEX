#pragma once

#include <string>

#include "../common.hh"

namespace rdmaio {

namespace qp {

const u32 kDefaultQKey = 0x111111;
const u32 kDefaultPSN = 3185;
const u32 kRcMaxSendSz = 128;
const u32 kRcMaxRecvSz = 2048;

class RC;
class UD;
class Impl;

struct __attribute__((packed))  QPConfig {
public:
  QPConfig() = default;

  QPConfig &set_access_flags(int flags) {
    access_flags = flags;
    return *this;
  }

  QPConfig &clear_access_flags() { return set_access_flags(0); }

  QPConfig &set_max_rd_ops(int max_rd) {
    max_rd_atomic = (max_dest_rd_atomic = max_rd);
    return *this;
  }

  QPConfig &set_psn(int psn) {
    rq_psn = (sq_psn = psn);
    return *this;
  }

  QPConfig &set_timeout(int timeout) {
    timeout = timeout;
    return *this;
  }

  QPConfig &set_max_send(int num) {
    max_send_size = num;
    return *this;
  }

  int max_send_sz() const {
    return max_send_size;
  }

  QPConfig &set_max_recv(int num) {
    max_recv_size = num;
    return *this;
  }

  int max_recv_sz() const {
    return max_recv_size;
  }

  QPConfig &add_access_write() {
    access_flags |= IBV_ACCESS_REMOTE_WRITE;
    return *this;
  }

  QPConfig &add_access_read() {
    access_flags |= IBV_ACCESS_REMOTE_READ;
    return *this;
  }

  QPConfig &add_access_atomic() {
    access_flags |= IBV_ACCESS_REMOTE_ATOMIC;
    return *this;
  }

  QPConfig &set_qkey(int k) {
    qkey = k;
    return *this;
  }

  bool allow_remote_read() const {
    return (access_flags & IBV_ACCESS_REMOTE_READ) != 0;
  }

  std::string desc_access_flags() const {
    std::string res = "The access flags of this qp: [ ";
    if (access_flags & IBV_ACCESS_REMOTE_WRITE)
      res.append("remote write,");
    if (access_flags & IBV_ACCESS_REMOTE_READ)
      res.append("remote read,");
    if (access_flags & IBV_ACCESS_REMOTE_ATOMIC)
      res.append("remote atomic ]");
    else
      res.append("]");
    return res;
  }

private:
  int access_flags = (IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ |
                      IBV_ACCESS_REMOTE_ATOMIC);
  int max_rd_atomic = 16;
  int max_dest_rd_atomic = 16;
  int rq_psn = kDefaultPSN;
  int sq_psn = kDefaultPSN;
  int timeout = 20;
  int max_send_size = kRcMaxSendSz;
  int max_recv_size = kRcMaxRecvSz;

  int qkey = kDefaultQKey;

  friend class RC;
  friend class UD;
  friend class Impl;
}; // class QPConfig

} // namespace qp

} // end namespace rdmaio
