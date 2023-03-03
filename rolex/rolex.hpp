#pragma once

#include "plr.hpp"
#include "submodel.hpp"
#include "remote_memory.hh"
#include "rolex_util.hh"


namespace rolex {

template<typename K, typename V, typename leaf_t, typename alloc_t, typename remote_memory_t, size_t Epsilon=8>
class Rolex {
  using model_t = SubModel<K, V, leaf_t, alloc_t, Epsilon>;
  using OptimalPLR = PLR<K, size_t>;

private:
  remote_memory_t* RM;
  std::vector<K> model_keys;
  std::vector<model_t> models;

public:
  explicit Rolex(remote_memory_t *RM)
      : RM(RM), model_keys(), models() { assert(RM->leaf_allocator() && RM->model_allocator()); }

  explicit Rolex(remote_memory_t *RM, const std::vector<K> &keys, const std::vector<V> &vals)
      : RM(RM), model_keys(), models() {
    assert(RM->leaf_allocator() && RM->model_allocator());
    train(keys, vals);
  }

  // ====================== functions for serialization and deserialization =================
  // Alloc will not used on compute nodes
  explicit Rolex(const std::string_view& seria) : model_keys(), models() {
    ASSERT(seria.size() > sizeof(i32));
    char* cur_ptr = (char *)seria.data();
    i32 model_total_size = ::xstore::util::Marshal<i32>::deserialize(cur_ptr, seria.size());
    cur_ptr += sizeof(i32);
    ASSERT(seria.size() == sizeof(i32) + model_total_size);
    while(cur_ptr - (char *)seria.data()<seria.size()) {
      // model_key
      K key = ::xstore::util::Marshal<K>::deserialize(cur_ptr, seria.size());
      this->model_keys.push_back(key);
      cur_ptr += sizeof(K);
      // model
      i32 mSeria_size = ::xstore::util::Marshal<i32>::deserialize(cur_ptr, seria.size());
      cur_ptr += sizeof(i32);
      std::string mSeria(cur_ptr, mSeria_size);
      this->models.emplace_back(mSeria);
      cur_ptr += mSeria_size;
    }
  }

  /**
   * @brief The sequence of serialization:
   *             model[0].total, model_key[0], model[0].size, model[0]...
   */
  auto serialize() -> std::string {
    std::string ans;
    for(int i=0; i<models.size(); i++){
      std::string res;
      res += ::xstore::util::Marshal<K>::serialize_to(model_keys[i]);
      auto mSeria = models[i].serialize();
      res += ::xstore::util::Marshal<i32>::serialize_to(mSeria.size());
      res += mSeria;
      ans += res;
    }
    std::string res;
    res += ::xstore::util::Marshal<i32>::serialize_to(ans.size());
    res += ans;
    return res;
  }

  void deserialize() {
    u64 total_size;
    memcpy(&total_size, RM->model_allocator()->get_total_ptr(), sizeof(u64));
    LOG(4) << "Read model size: "<<total_size;
    for(u64 i=0; i<total_size; i++) {
      // read upper model_keys
      auto upper_res = RM->model_allocator()->get_upper(i);
      K key;
      u64 off;
      memcpy(&key, upper_res.first, sizeof(K));
      memcpy(&off, upper_res.second, sizeof(u64));
      model_keys.emplace_back(key);

      // read submodel
      auto sub_res = RM->model_allocator()->get_submodel(off);
      i32 cur_ms;
      memcpy(&cur_ms, sub_res, sizeof(i32));
      LOG(2)<<"Model "<<i<<" size: "<<cur_ms<<", offset: "<<off;

      char* read_model_buf = (char *)malloc(cur_ms);
      memcpy(read_model_buf, sub_res+sizeof(i32), cur_ms);
      std::string rSeria(read_model_buf, cur_ms);
      model_t model(rSeria);
      models.emplace_back(model);
    }
  }

  /**
   * @brief Training models, Note: the data are stored in submodels 
   *                and the model structure are constructed with model_keys
   */
  void train(const std::vector<K> &keys, const std::vector<V> &vals)
  {
    assert(keys.size() == vals.size());
    if(keys.size()==0) return;
    LOG(2) << "Training data: "<<keys.size()<<", Epsilon: "<<Epsilon;

    OptimalPLR* opt = new OptimalPLR(Epsilon-1);
    K p = keys[0];
    size_t pos=0;
    opt->add_point(p, pos);
    auto k_iter = keys.begin();
    auto v_iter = vals.begin();
    for(int i=1; i<keys.size(); i++) {
      K next_p = keys[i];
      if (next_p == p){
        LOG(5)<<"DUPLICATE keys";
        exit(0);
      }
      p = next_p;
      pos++;
      if(!opt->add_point(p, pos)) {
        auto cs = opt->get_segment();
        auto[cs_slope, cs_intercept] = cs.get_slope_intercept();
        append_model(cs_slope, cs_intercept, k_iter, v_iter, pos);
        k_iter += pos;
        v_iter += pos;
        pos=0;
        opt = new OptimalPLR(Epsilon-1);
        opt->add_point(p, pos);
      }
    }
    auto cs = opt->get_segment();
    auto[cs_slope, cs_intercept] = cs.get_slope_intercept();
    append_model(cs_slope, cs_intercept, k_iter, v_iter, ++pos);

    u64 total_size = models.size();
    LOG(4) << "Training models: "<<total_size<<" used leaves: "<<this->RM->leaf_allocator()->used_num();
    assert(model_keys.size() == total_size);
    // write total_num into model_region
    memcpy(RM->model_allocator()->get_total_ptr(), &total_size, sizeof(u64));
  }

  // ========= API functions for memory nodes {debugging} : search, update, insert, remove ===========
  auto search(const K &key, V &val) -> bool {
    return models[model_for_key(key)].search(key, val, this->RM->leaf_allocator());
  }

  auto update(const K &key, const V &val) -> bool {
    return models[model_for_key(key)].update(key, val, this->RM->leaf_allocator());
  }

  auto insert(const K &key, const V &val) -> bool {
    auto model_n = model_for_key(key);
    // LOG(2) <<"Key: "<<key<<", Insert into model: "<< model_n;
    return models[model_n].insert(key, val, this->RM->leaf_allocator());
  }

  auto remove(const K &key) -> bool {
    return models[model_for_key(key)].remove(key, this->RM->leaf_allocator());
  }

  void range(const K& key, const int n, std::vector<V> &vals) {
    auto model_n = model_for_key(key);
    models[model_n].range(key, n, vals, this->RM->leaf_allocator());
    model_n++;
    while(vals.size()<n && model_n<models.size()) {
      models[model_n].range(key, n, vals, this->RM->leaf_allocator());
      model_n++;
    }
  } 

  // ============== functions for debugging ================
  void print_data() {
    ASSERT(this->RM->leaf_allocator()) << "Leaf allocator in the model is nullptr";
    for(int i=0; i<models.size(); i++){
      LOG(3)<<"Submodel " << i <<", model_key: "<<model_keys[i];
      models[i].print_data(this->RM->leaf_allocator());
    }
  }

  void print() {
    for(int i=0; i<models.size(); i++){
      LOG(3)<<"Submodel " << i <<", model_key: "<<model_keys[i];
      models[i].print();
    }
  }

private:
  /**
   * @brief construct submodels with parameters and KVs
   *      
   */
  void append_model(double slope, double intercept,
                     const typename std::vector<K>::const_iterator &keys_begin,
                     const typename std::vector<V>::const_iterator &vals_begin, 
                     size_t size) 
  {
    ASSERT(this->RM->leaf_allocator()) << "Leaf allocator in the model is nullptr";
    auto key = *(keys_begin+size-1);
    model_keys.push_back(key);
    model_t model(slope, intercept, keys_begin, vals_begin, size, this->RM->leaf_allocator());
    //models.emplace_back(slope, intercept, keys_begin, vals_begin, size, this->RM->leaf_allocator());
    models.emplace_back(model);

    // write model into model_region
    auto mSeria = model.serialize();
    auto subReg = RM->model_allocator()->alloc_submodel(mSeria.size()+sizeof(i32));
    i32 m_size = mSeria.size();
    memcpy(subReg.first, &m_size, sizeof(i32));
    memcpy(subReg.first+sizeof(i32), mSeria.data(), mSeria.size());
    // LOG(2) << "Write submodel size: " <<mSeria.size()<<", offset: "<<subReg.second;
    
    i32 cur_ms;
    memcpy(&cur_ms, subReg.first, sizeof(i32));
    // LOG(2) << "cur_ms: "<<cur_ms;


    // write upper
    auto upper_off = RM->model_allocator()->alloc_upper();
    memcpy(upper_off.first, &key, sizeof(key));
    memcpy(upper_off.second, &(subReg.second), sizeof(u64));
  }

  auto model_for_key(const K &key) -> usize {
    auto idx = binary_search_branchless(&model_keys[0], model_keys.size(), key);
    return idx<models.size()? idx:(models.size()-1);
  }

};




} // namespace rolex