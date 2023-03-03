#pragma once

#include <limits>

#include "../common.hh"

namespace rdmaio {

namespace qp {

/*!
  we restrict the maximun number of a doorbelled request to be 16
 */
const usize kNMaxDoorbell = 16;

/*!
  currently this class is left with no methods, because I found its hard
  to abstract many away the UD and RC doorbelled requests

  Example:
  `
  DoorbellHelper<2> doorbell; // init a doorbell with at most two requeast

  doorbell.next(); // add one
  doorbell.cur_sge() = ...; // setup the current sge
  doorbell.cur_wr() = ...;  // setup the current (send) wr

  doorbell.next(); // start another doorbell request
  doorbell.cur_sge() = ...; // setup the current sge
  doorbell.cur_wr() = ...;  // setup the current (send) wr

  // now we can post this doorbell
  doorbell.freeze(); // freeze this doorbell to avoid future next()
  ibv_post_send(doorbell.first_wr_ptr(), ... );
  doorbell.freeze_done(); // re-set this doorbell to reuse, (optional)
  doorbell.clear(); // re-set the counter
  `
*/
template <usize N = kNMaxDoorbell>
struct DoorbellHelper {
  ibv_send_wr wrs[N];
  ibv_sge sges[N];

  i8 cur_idx = -1;

  static_assert(std::numeric_limits<i8>::max() > kNMaxDoorbell,
                "Index type's max value must be larger than the number of max "
                "doorbell num");

  explicit DoorbellHelper(const ibv_wr_opcode &op) {
    for (uint i = 0; i < N; ++i) {
      wrs[i].opcode = op;
      wrs[i].num_sge = 1;
      // its fine to store an invalid pointer when i == N,
      // this is because before flushing the doorbelled requests,
      // we will modify change the laster pointer to nullptr
      wrs[i].next = &(wrs[i + 1]);
      wrs[i].sg_list = &sges[i];
    }
    wrs[N - 1].next = &(wrs[0]);
  }

  /*!
    Current pending doorbelled request
   */
  int size() const { return cur_idx + 1; }

  bool empty() const { return size() == 0; }

  bool next() {
    // assert(!full());
    if (unlikely(full())) return false;
    cur_idx += 1;
    return true;
  }

  bool full() const { return size() == N; }

  /*!
    setup the cur_wr's next to nullptr
    so that we are able to post these doorbells to the NIC
   */
  inline void freeze() {
    assert(!empty());
    cur_wr().next = nullptr;
  }

  inline void freeze_at(const usize &idx) {
    this->get_wr_ptr(idx)->next = nullptr;
  }

  inline void freeze_done_at(const usize &idx) {
    if (idx == N - 1) {
      this->get_wr_ptr(idx)->next = this->first_wr_ptr();
    } else {
      this->get_wr_ptr(idx)->next = this->get_wr_ptr(idx + 1);
    }
  }

  inline void freeze_done() {
    assert(!empty());
    wrs[cur_idx].next = &(wrs[cur_idx + 1]);
  }

  inline void clear() {
    freeze_done();
    cur_idx = -1;
  }

  /*!
    Warning!!
    We donot check i due to performance reasons
   */
  inline ibv_send_wr &cur_wr() {
    assert(!empty());
    return wrs[cur_idx];
  }

  inline ibv_sge &cur_sge() {
    assert(!empty());
    return sges[cur_idx];
  }

  inline ibv_send_wr *first_wr_ptr() { return &wrs[0]; }

  /*!
    \note: not check index
   */
  inline ibv_send_wr *get_wr_ptr(const usize &idx) { return &wrs[idx]; }

  inline ibv_sge *get_sge_ptr(const usize &idx) { return &sges[idx]; }

  // some helper functions for verifying the correctness of the wrs/sges
  usize sanity_check_sz() {
    auto cur_ptr = first_wr_ptr();
    if (empty()) return 0;

    for (uint i = 0; i < N; ++i) {
      cur_ptr = cur_ptr->next;
      if (cur_ptr == nullptr) {
        return i + 1;
      }
    }
    // an invalid sz
    return N + 1;
  }
};

}  // namespace qp

}  // namespace rdmaio
