#pragma once

#include <iostream>
#include <map>
#include <mutex>

#include "./common.hh"
#include "./naming.hh"

namespace rdmaio {

using nic_id_t = u64;

/*!
  RNic is an abstraction of ib_ctx and ib_pd.
  It does the following thing:
  - open a context, protection domain, and filled the address.
 */
class RNic : public std::enable_shared_from_this<RNic> {
public:
  // context exposed by libibverbs
  struct ibv_context *ctx = nullptr;
  struct ibv_pd *pd = nullptr;

public:
  const DevIdx id;

  /*!
    filled after a successful opened device
  */
  const Option<RAddress> addr;
  const Option<u64> lid;

  static Option<Arc<RNic>> create(const DevIdx &idx, u8 gid = 0) {
    auto res = std::make_shared<RNic>(idx,gid);
    if (res->valid()) {
      return res;
    }
    return {};
  }

  /*!
    Open a nic handler (ibv_ctx,protection domain(pd),
    if success, valid() should return true.
    Otherwise, should print the error to the screen, or panic.
   */
  RNic(const DevIdx &idx, u8 gid = 0)
      : id(idx), ctx(open_device(idx)), pd(alloc_pd()), lid(fetch_lid(idx)),
        addr(query_addr(gid)) {
    //
  }

  bool valid() const { return (ctx != nullptr) && (pd != nullptr); }

  struct ibv_context *get_ctx() const {
    return ctx;
  }

  struct ibv_pd *get_pd() const {
    return pd;
  }

  /*!
   */
  Result<std::string> is_active() const {
    if (!valid()) {
      return Err(std::string("Context is not valid."));
    } else {
      ibv_port_attr port_attr;
      auto rc = ibv_query_port(ctx, id.port_id, &port_attr);
      if (rc == 0 && port_attr.state == IBV_PORT_ACTIVE)
        return Ok(std::string(""));
      else if (rc == 0 && port_attr.state != IBV_PORT_ACTIVE) {
        return Err(std::string(ibv_port_state_str(port_attr.state)));
      }else
        return Err(std::string(strerror(errno)));
    }
  }

  ~RNic() {
    // pd must he deallocaed before ctx
    if (pd != nullptr) {
      auto rc = ibv_dealloc_pd(pd);
      RDMA_LOG_IF(2, rc != 0) << "deallocate pd error: " << strerror(errno);
    }
    if (ctx != nullptr) {
      auto rc = ibv_close_device(ctx);
      RDMA_LOG_IF(2, rc != 0) << "deallocate ctx error: " << strerror(errno);
    }
  }

  // implementations
private:
  struct ibv_context *open_device(const DevIdx &idx) {

    ibv_context *ret = nullptr;

    int num_devices;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (idx.dev_id >= num_devices || idx.dev_id < 0) {
      RDMA_LOG(WARNING) << "wrong dev_id: " << idx << "; total " << num_devices
                        << " found";
      goto ALLOC_END;
    }

    RDMA_ASSERT(dev_list != nullptr);
    ret = ibv_open_device(dev_list[idx.dev_id]);
    if (ret == nullptr) {
      RDMA_LOG(WARNING) << "failed to open ib ctx w error: " << strerror(errno)
                        << "; at devid " << idx;
      goto ALLOC_END;
    }
  ALLOC_END:
    if (dev_list != nullptr)
      ibv_free_device_list(dev_list);
    return ret;
  }

  struct ibv_pd *alloc_pd() {
    if (ctx == nullptr)
      return nullptr;
    auto ret = ibv_alloc_pd(ctx);
    if (ret == nullptr) {
      RDMA_LOG(WARNING) << "failed to alloc pd w error: " << strerror(errno);
    }
    return ret;
  }

  Option<u64> fetch_lid(const DevIdx &idx) {
    if (!valid()) {
      return {};
    } else {
      ibv_port_attr port_attr;
      auto rc = ibv_query_port(ctx, idx.port_id, &port_attr);
      if (rc == 0)
        return Option<u64>(port_attr.lid);
      return {};
    }
  }

  Option<RAddress> query_addr(u8 gid_index = 0) const {

    if (!valid())
      return {};

    ibv_gid gid;
    ibv_query_gid(ctx, id.port_id, gid_index, &gid);

    RAddress addr{.subnet_prefix = gid.global.subnet_prefix,
                  .interface_id = gid.global.interface_id,
                  .local_id = gid_index};
    return Option<RAddress>(addr);
  }

  DISABLE_COPY_AND_ASSIGN(RNic);
};

} // namespace rdmaio
