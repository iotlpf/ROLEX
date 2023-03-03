#pragma once

#include <iostream>

#include "./common.hh"

namespace rdmaio {

/*!
 */
using HostId = std::tuple<std::string, int>; // TCP host host,port

/*!
  DevIdx is a handy way to identify the RNIC.
  A NIC is identified by a dev_id, while each device has several ports.
*/
struct DevIdx {
  usize dev_id;
  usize port_id;

  friend std::ostream &operator<<(std::ostream &os, const DevIdx &i) {
    return os << "{" << i.dev_id << ":" << i.port_id << "}";
  }
};

/*!
  Internal network address used by the driver
 */
struct __attribute__((packed)) RAddress {
  u64 subnet_prefix;
  u64 interface_id;
  u32 local_id;
};


} // namespace rdmaio
