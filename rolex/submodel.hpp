#pragma once

#include <math.h>
#include <algorithm>
#include "r2/src/common.hh"
#include "leaf_table.hpp"
#include "leaf.hpp"


#define SUB_EPS(x, epsilon) ((x) <= (epsilon) ? 0 : ((x) - (epsilon)))
#define ADD_EPS(x, epsilon, size) ((x) + (epsilon) + 2 >= (size) ? (size-1) : (x) + (epsilon) + 2)

struct ApproxPos {
    size_t pos; ///< The approximate position of the key.
    size_t lo;  ///< The lower bound of the range.
    size_t hi;  ///< The upper bound of the range.
};


namespace rolex {



template<typename K, size_t Epsilon=16>
class LinearRegressionModel{
private:
  double slope;
  double intercept;

public: 
  explicit LinearRegressionModel(double s, double i) : slope(s), intercept(i) {}

  void deserialize(const std::string_view& seria) {
    ASSERT(seria.size() == sizeof(double)*2);
    char* cur_ptr = (char *)seria.data();
    this->slope = ::xstore::util::Marshal<double>::deserialize(cur_ptr, seria.size());
    cur_ptr += sizeof(double);
    this->intercept = ::xstore::util::Marshal<double>::deserialize(cur_ptr, seria.size());
  }

  auto serialize() -> std::string {
    std::string res;
    res += ::xstore::util::Marshal<double>::serialize_to(this->slope);
    res += ::xstore::util::Marshal<double>::serialize_to(this->intercept);
    return res;
  }

  ApproxPos predict(const K &k, const size_t size) const {
    auto pos = int64_t(slope * k) + intercept;
    auto lo = SUB_EPS(pos, Epsilon);
    auto hi = ADD_EPS(pos, Epsilon, size);
    lo = lo>hi? hi:lo;
    // return {static_cast<size_t>(ceil(pos)), static_cast<size_t>(ceil(lo)), static_cast<size_t>(ceil(hi))};
    return {static_cast<size_t>(pos), static_cast<size_t>(lo), static_cast<size_t>(hi)};
  }

  void print() {
    std::cout<<"Model -> [slope, intercept]: "<<slope<<", "<<intercept<<std::endl;
  }

};

template<typename K, typename V, typename leaf_t, typename leaf_alloc_t, size_t Epsilon = 16>
class SubModel {
  using lr_model_t = LinearRegressionModel<K, Epsilon>;
  using leaf_table_t = struct LeafTable<K, V, leaf_t, leaf_alloc_t>;

private:
  lr_model_t model;
  leaf_table_t ltable;
  size_t capacity;

public:
  /**
   * @brief Construct a LinearRegressionModel, 
   *            store data into leaves and leaf_table
   * 
   * @param alloc the LeafAllocator to alloc new leaves
   */
  explicit SubModel(double slope, double intercept,
                    const typename std::vector<K>::const_iterator &keys_begin,
                    const typename std::vector<V>::const_iterator &vals_begin, 
                    size_t size, leaf_alloc_t* alloc) : model(slope, intercept), capacity(size), ltable()
  {
    assert(size>0);
    auto res = alloc->fetch_new_leaf();
    ltable.train_emplace_back(res.second);
    leaf_t* cur_leaf = reinterpret_cast<leaf_t*>(res.first);
    for(int i=0; i<size; i++) {
      if(cur_leaf->isfull()){
        res = alloc->fetch_new_leaf();
        ltable.train_emplace_back(res.second);
        cur_leaf = reinterpret_cast<leaf_t*>(res.first);
      }
      cur_leaf->insert_not_full(*(keys_begin+i), *(vals_begin+i));
    }
  }

  // ============== functions for serialization and deserialization ================
  explicit SubModel(const std::string_view& seria) : model(0, 0), ltable() {
    i32 model_size = sizeof(double)*2;
    ASSERT(seria.size() >= model_size + sizeof(size_t) + sizeof(i32)) << "submodel seria.size(): "<<seria.size();
    char* cur_ptr = (char *)seria.data();
    // model
    std::string modelSeria(cur_ptr, model_size);
    this->model.deserialize(modelSeria);
    cur_ptr += model_size;
    // capacity
    this->capacity = ::xstore::util::Marshal<size_t>::deserialize(cur_ptr, seria.size());
    cur_ptr += sizeof(size_t);
    // ltable
    i32 ltable_size = ::xstore::util::Marshal<i32>::deserialize(cur_ptr, seria.size());
    cur_ptr += sizeof(i32);
    ASSERT(seria.size() == model_size + sizeof(size_t) + sizeof(i32) + ltable_size)
      <<"submodel seria.size(): "<<seria.size()<<", ltable_size: "<<ltable_size;
    std::string ltableSeria(cur_ptr, ltable_size);
    this->ltable.deserialize(ltableSeria);
  }

  /**
   * @brief The sequence of serialization:
   *             model, capacity, ltable_size, ltable
   */
  auto serialize() -> std::string {
    std::string res;
    res += this->model.serialize();
    res += ::xstore::util::Marshal<size_t>::serialize_to(this->capacity);
    auto ltableSeria = this->ltable.serialize();
    res += ::xstore::util::Marshal<i32>::serialize_to(ltableSeria.size());
    res += ltableSeria;
    return res;
  }

  // ========= API functions for memory nodes {debugging} : search, update, insert, remove ===========
  auto search(const K &key, V &val, leaf_alloc_t* alloc) -> bool {
    auto[pre, lo, hi] = this->model.predict(key, capacity);
    lo /= leaf_t::max_slot();
    hi /= leaf_t::max_slot();
    int l=std::max((int)lo, 0);
    int h=std::max((int)hi, 0);
    return ltable.search(key, val, alloc, l, h);
  }

  auto update(const K &key, const V &val, leaf_alloc_t* alloc) -> bool {
    auto[pre, lo, hi] = this->model.predict(key, capacity);
    lo /= leaf_t::max_slot();
    hi /= leaf_t::max_slot();
    int l=std::max((int)lo, 0);
    int h=std::max((int)hi, 0);
    return ltable.update(key, val, alloc, l, h);
  }

  auto insert(const K &key, const V &val, leaf_alloc_t* alloc) -> bool {
    auto[pre, lo, hi] = this->model.predict(key, capacity);
    lo /= leaf_t::max_slot();
    hi /= leaf_t::max_slot();
    int l=std::max((int)lo, 0);
    int h=std::max((int)hi, 0);
    // LOG(2) << "model predict leaf l: " <<l<<", h: "<<h;
    return ltable.insert(key, val, alloc, l, h);
  }

  auto remove(const K &key, leaf_alloc_t* alloc) -> bool {
    auto[pre, lo, hi] = this->model.predict(key, capacity);
    lo /= leaf_t::max_slot();
    hi /= leaf_t::max_slot();
    int l=std::max((int)lo, 0);
    int h=std::max((int)hi, 0);
    return ltable.remove(key, alloc, l, h);
  }

  void range(const K& key, const int n, std::vector<V> &vals, leaf_alloc_t* alloc) {
    auto[pre, lo, hi] = this->model.predict(key, capacity);
    lo /= leaf_t::max_slot();
    hi /= leaf_t::max_slot();
    int l=std::max((int)lo, 0);
    int h=std::max((int)hi, 0);
    ltable.range(key, n, vals, alloc, l, h);
  }

  // ================ API functions for compute nodes : search, update, insert, remove ===========
  auto get_leaf_addr(const K &key, std::vector<leaf_addr_t> &leaves) {
    auto[pre, lo, hi] = this->model.predict(key, capacity);
    lo /= leaf_t::max_slot();
    hi /= leaf_t::max_slot();
    this->ltable.get_leaf_addr(lo, hi, leaves);
  }


  // ============== functions for debugging =================
  void print_data(leaf_alloc_t* alloc) {
    model.print();
    ltable.print(alloc);
  }

  void print() {
    model.print();
    ltable.print();
  }



};



} // namespace rolex