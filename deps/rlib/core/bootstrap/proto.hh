#pragma once

#include "../rmem/handler.hh"
#include "../qps/mod.hh"

namespace rdmaio {

namespace proto {

using rpc_id_t = u8;

/*!
  RPC ids used for the callbacks
 */
enum RCtrlBinderIdType : rpc_id_t {
  HeartBeat = 0, // ping whether RCtrl is started
  FetchMr,       // fetch remote MR
  CreateRC,      // create an RC for connect (for one-sided)
  CreateRCM,     // create an RC which uses message (for two-sided)
  DeleteRC,
  FetchQPAttr,  // fetch a created QP's attr. useful for UD QP
  Reserved,
};

enum CallbackStatus : u8 {
  Ok = 0,
  Err = 1,
  NotFound,
  WrongArg,
  ConnectErr,
  AuthErr,
};

/*!
  Req/Reply for handling MR requests
 */
struct __attribute__((packed)) MRReq {
  ::rdmaio::rmem::register_id_t id;
  // maybe for future extensions, like permission, etc
};

struct __attribute__((packed)) MRReply {
  CallbackStatus status;
  ::rdmaio::rmem::RegAttr attr;
};

/*******************************************/

struct __attribute__((packed)) QPReq {
  char name[::rdmaio::qp::kMaxQPNameLen + 1];
  u64 key;
};

/*!
  Req/Reply for creating ~(RC) QPs
 */
struct __attribute__((packed)) RCReq {

  RCReq() = default;

  // parameter for querying the QP
  char name[::rdmaio::qp::kMaxQPNameLen + 1];
  char name_recv[::rdmaio::qp::kMaxQPNameLen + 1]; // the name used to create recv_cqs

  u8 whether_create = 0; // 1: create the QP, 0 only query the QP attr
  u8 whether_recv   = 0; // 1: create with the recv_cq specified by the *name_recv*, 0 not

  // if whether_create = 1, uses the following parameter to create the QP
  ::rdmaio::nic_id_t nic_id;
  ::rdmaio::qp::QPConfig config;
  ::rdmaio::qp::QPAttr attr; // the attr used for connect

  u64 max_recv_sz = 4096;
};

struct __attribute__((packed)) RCReply {
  CallbackStatus status;
  ::rdmaio::qp::QPAttr attr;
  u64 key;
};

struct __attribute__((packed)) DelRCReq {
  // parameter for querying the QP
  char name[::rdmaio::qp::kMaxQPNameLen + 1];

  u64 key;
};

/*******************************************/

} // namespace proto

} // namespace rdmaio
