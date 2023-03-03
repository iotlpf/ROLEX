#pragma once

// R2 coroutine is a wrapper over boost asymmetric coroutine
#include <boost/coroutine/all.hpp>

#include <functional>
#include <memory>

#include "rlib/core/result.hh"

#include "./common.hh"
#include "./linked_list.hh"

namespace r2 {

using namespace rdmaio;

using yield_f = boost::coroutines::symmetric_coroutine<void>::yield_type;
using b_coroutine_f = boost::coroutines::symmetric_coroutine<void>::call_type;

/*!
  Exposed to user, a routine function is just void -> void
 */
using routine_func_t = std::function<void(yield_f &)>;

class Routine {
public:
  using id_t = u8;

  const id_t id;

  std::shared_ptr<routine_func_t> raw_f;

  b_coroutine_f *core;

  Result<> status;

  bool active = false;

  Routine(const id_t &id, std::shared_ptr<routine_func_t> f)
    : id(id), raw_f(f), core(new b_coroutine_f(*f)), status(::rdmaio::Ok()) {
    //
  }

  ~Routine() {
    // maybe we should not delete this core
    // because other coroutines may have reference to it
    //delete core;
  }

  inline void execute(yield_f &yield) {
    active = true;
    yield(*core);
  }

  void start() { (*core)(); }
};
} // namespace r2
