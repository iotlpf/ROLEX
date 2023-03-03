#pragma once

#include <unordered_map>

#include "./ud_session.hh"

namespace r2 {

class UDMsg {
  using session_id_t = u32;
  using session_map_t = std::unordered_map < session_id_t, Arc<UDSession>;

  session_map_t opened_sessions;

  Arc<UD> ud;

  explicit UDMsg(Arc<UD> ud) : ud(ud) {}


};
} // namespace r2
