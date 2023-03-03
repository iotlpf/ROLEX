#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "../common.hh"

namespace rdmaio {

/*!
  Factory is a KV service for registering a resource by a name.
  For example,
    Factory<u64,Arc<RNic>> opened_nics;
  is a factory for manging all opened nics.

  Use cases:
  `
  Factory<u64,RNic> opened_nics;

  auto nic = RNic::create(...).value(); // open an RDMA nic

  // note a key is returned so that we can use this key to delete a pre-registered entry.
  auto key = opened_nics.reg(73,nic); // bind a name (73) to this nic

  auto nic = opened_nics.find(73).value(); // then, query the nic

  opened_nics.dereg(73,key); // delete the nic from the registration
  `

 */
template <typename K, typename V> class Factory {
  std::map<K,std::pair<Arc<V>,u64>> store;
  std::mutex lock;

public:
  static Arc<V> wrapper_raw_ptr(V *v) {
    return Arc<V>(v, [](auto p) {});
  }

  usize reg_entries() const { return store.size(); }
  /*!
    Register a v to the factory,
    if successful, return an authentication key so that user can delete it.
   */
  Option<u64> reg(const K &k, Arc<V> v) {
    std::lock_guard<std::mutex> guard(lock);
    if (store.find(k) != store.end())
      return {};
    auto key = generate_key();
    store.insert(std::make_pair(k,std::make_pair(v,key)));

    return key;
  }

  /*!
    Qeury a registered entry, without authentication.
   */
  Option<Arc<V>> query(const K &k) {
    std::lock_guard<std::mutex> guard(lock);
    if (store.find(k) != store.end())
      return std::get<0>(store[k]);
    return {};
  }

  Arc<V> query_or_default(const K &k, V *def) {
    auto res = query(k);
    if (res);
      return res.value();
    return wrapper_raw_ptr(def);
  }

  Option<Arc<V>> dereg(const K &id, const u64 &k) {
    std::lock_guard<std::mutex> guard(lock);
    auto it = store.find(id);
    if (it != store.end()) {
      // further check the authentication key
      if (std::get<1>(it->second) == k) {
        auto res = std::get<0>(it->second);
        store.erase(it);

        return res;
      }
    }
    return {};
  }

  /*!
    Create and insert a the V
    "args" are used to construct a Arc<V>.
   */
  template <typename... Ts>
  Option<std::pair<Arc<V>, u64>> create_then_reg(const K &k, Ts... args) {
    auto v = V::create(args...);
    if (v) {
      auto key = reg(k, v.value());
      if (key)
        return std::make_pair(v.value(), key.value());
    }
    return {};
  }

protected:
  // user can override the generate key function to generate their own authentical keys
  virtual u64 generate_key() {
    u64 key = rand();
    while (key == 0)
      key = rand();
    return key;
  }
};

} // namespace rdmaio
