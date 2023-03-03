#pragma once

#include <vector>

#include "nic.hh"

namespace rdmaio {

class RNicInfo {
public:
  /*!
    Query all available RNic on the host machine.
    Return a vector of all their index used by RLib,
    so one can open a device handler using RNic, as follows:
    `
    auto dev_idxs = RNicInfo::query_dev_names();
    if(dev_idxs.size() > 0) {
      RNic nic(dev_idxs[0]);
    }
    `
   */
  static std::vector<DevIdx> query_dev_names() {

    std::vector<DevIdx> res;

    int num_devices;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);

    for (uint i = 0; i < num_devices; ++i) {
      RNic rnic({.dev_id = i, .port_id = 73 /* a dummy value*/});
      if (rnic.valid()) {
        ibv_device_attr attr;
        auto rc = ibv_query_device(rnic.get_ctx(), &attr);

        if (rc)
          continue;
        for (uint port_id = 1; port_id <= attr.phys_port_cnt; ++port_id) {
          res.push_back({.dev_id = i, .port_id = port_id});
        }
      } else
        RDMA_ASSERT(false);
    }
    if (dev_list != nullptr)
      ibv_free_device_list(dev_list);
    return res;
  }
}; // end class RNicInfo

} // namespace rdmaio
