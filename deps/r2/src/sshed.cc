#include "./libroutine.hh"

namespace r2 {

const usize kMaxRoutineSupported = 64;
static_assert(
    kMaxRoutineSupported + 1 <= std::numeric_limits<id_t>::max(),
    "invalid id_t type, consider make it from u8 to a larger unsigned type");

/*!
  The constructor will spawn a main coroutine for polling all the futures
 */
SScheduler::SScheduler() : pending_futures(kMaxRoutineSupported, 0) {

  routines.reserve(kMaxRoutineSupported + 1);

  this->spawn([this](R2_ASYNC) {
    while (R2_EXECUTOR.running) {
      // TODO, the body is not implemented
      this->poll_all_futures();
      R2_YIELD;
    }
    R2_RET;
  });
  assert(cur_routine_ptr != nullptr);
}

Option<id_t> SScheduler::spawn(const sroutine_func_t &f) {
  auto cid = routines.size();
  if (cid >= kMaxRoutineSupported)
    return {};
  auto wrapper = std::shared_ptr<routine_func_t>(new routine_func_t,
                                                 [](auto p) { delete p; });
  *wrapper = std::bind(f, std::placeholders::_1, std::ref(*this));

  { // unsafe code
    routines.push_back(new Node<Routine>(cid, wrapper));
    Node<Routine> *cur_ptr = routines[cid];

    auto cur = routine_chain.add(cur_ptr);
    if (cur_routine_ptr == nullptr)
      cur_routine_ptr = cur;
  }
  return cid;
}

void SScheduler::exit(yield_f &f) {
  auto temp = cur_routine_ptr;
  cur_routine_ptr = routine_chain.leave_one(cur_routine_ptr);

  // free resource
  // FIXME: currently delete this cause segmentation fault, I dnonot know why
  delete temp;

  // there are still remaining coroutines
  if (cur_routine_ptr != temp) {
    cur_routine_ptr->val.execute(f);
  }
}

void SScheduler::poll_all_futures() {

  for (auto it = futures.begin(); it != futures.end();) {
    auto res = (*it)(); // call the future function

    auto cid = std::get<0>(res.desc);
    auto num = std::get<1>(res.desc);

    bool need_add = false;

    // if res == Ok, NearOk, we need to decrease the pending futures
    if (res == IOCode::Ok || res == IOCode::NearOk) {
      ASSERT(pending_futures[cid] >= num)
          << " reduce num: " << num << ", pending_futures[cid]: " <<pending_futures[cid] << ", for cid: " << (u16)cid;
      pending_futures.at(cid) -= num;
      if (pending_futures[cid] == 0) {
        need_add = true;
      }
    }

    if (res == IOCode::Err) {
      need_add = true;
      routines.at(cid)->val.status = ::rdmaio::Err();
    }

    // if res == Ok, or Err, we need to eject this future
    if (res == IOCode::Ok || res == IOCode::Err)
      it = futures.erase(it);
    else
      it++;

    // finally we check whether we need to add back coroutine
    if (need_add) {
      assert(routines.at(cid)->val.active == false);
      //if (cid != 2)
      routine_chain.add(routines.at(cid));
    }
  }
}

} // namespace r2
