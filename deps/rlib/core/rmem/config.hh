#pragma once

#include "../common.hh"

namespace rdmaio {

namespace rmem {

/*!
  class for manging registeration permission of RDMA registered memory.
  The detailed documentation of it can be found at the documentation of ibverbs.
  Example usage: (generate a flag which has local write and remote read permissions)
  `
  MemoryFlags flags;
  flags.clear_flags().add_local_write().add_remote_read();

  // use it:
  auto value = flags.get_value();
  */
class MemoryFlags
{
  int protection_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                         IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
 public:
  MemoryFlags() = default;

  MemoryFlags& set_flags(int flags)
  {
    protection_flags = flags;
    return *this;
  }

  int get_value() const { return protection_flags; }

  MemoryFlags& clear_flags() { return set_flags(0); }

  MemoryFlags& add_local_write()
  {
    protection_flags |= IBV_ACCESS_LOCAL_WRITE;
    return *this;
  }

  MemoryFlags& add_remote_write()
  {
    protection_flags |= IBV_ACCESS_REMOTE_WRITE;
    /*
      According to https://www.rdmamojo.com/2012/09/07/ibv_reg_mr/
      local write must be set to enable remote write
     */
    add_local_write();
    return *this;
  }

  MemoryFlags& add_remote_read()
  {
    protection_flags |= IBV_ACCESS_REMOTE_READ;
    return *this;
  }
};

} // end namespace rmem

}
