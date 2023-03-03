#pragma once

#include <limits>
#include <iostream>
#include <optional>
#include <utility> 

#include "r2/src/common.hh"


using namespace r2;


namespace rolex {


template<usize N = 4, typename K = u64, typename V = u64>
struct __attribute__((packed)) Leaf
{
  K keys[N];
  V vals[N];

  Leaf() {
    for (uint i = 0; i < N; ++i) {
      this->keys[i] = invalidKey();
    }
  }

  inline static K invalidKey() { return std::numeric_limits<K>::max(); }

  inline static usize max_slot() { return N; }
    
    
  bool isfull() { return keys[N-1]==invalidKey()? false:true; }

  bool isEmpty() { return keys[0]==invalidKey(); }

  K last_key() { return keys[N-1]; }

  /**
   * @brief The start size of vals
   */
  static auto value_start_offset() -> usize { return offsetof(Leaf, vals); }


  // ================== API functions: search, update, insert, remove ==================
  auto search(const K &key, V &val) -> bool {
    if(keys[0]>key) return false;
    for (uint i = 0; i < N; ++i) {
      if (this->keys[i] == key) {
        val = vals[i];
        return true;
      }
    }
    return false;
  }

  auto contain(const K &key) ->bool {
    if(keys[0]>key) return false;
    for (uint i = 0; i < N; ++i) {
      if (this->keys[i] == key) 
        return true;
    }
    return false;
  }

  auto update(const K &key, const V &val) -> bool {
    if(keys[0]>key) return false;
    for (uint i = 0; i < N; ++i) {
      if (this->keys[i] == key) {
        vals[i] = val;
        return true;
      }
    }
    return false;
  }

  // fixme: what if the leaf is empty?
  auto insertHere(const K &key) -> bool { return key>=keys[0]; }

  /**
   * worth: guarantee to insert here by leaf_table
   *  1.not full?
   *  2.full?
   * 
   */
  // auto insert(const K &key, const V &val) -> bool {
  //   ASSERT(keys[0]<=key);
  //   for (uint i = 0; i < N; ++i) {
  //     if (this->keys[i] == key) {
  //       vals[i] = val;
  //       return true;
  //     }
  //   }
  //   return false;
  // }

  /**
   * @brief Insert data while keeping all data sorted
   *     Note: this function assume that the leaf is not full, used for init
   * 
   * @return u8 the inseted position
   */
  auto insert_not_full(const K &key, const V &val) -> u8 {
    u8 i=0;
    for(; i<N; i++){
      if(this->keys[i] == key) return 0;
      if(this->keys[i] > key) break;
    }
    u8 j=i;
    while(j<N && this->keys[j]!=invalidKey()) j++;
    if(j>=N) return N;
    std::copy_backward(keys+i, keys+j, keys+j+1);
    std::copy_backward(vals+i, vals+j, vals+j+1);
    this->keys[i] = key;
    this->vals[i] = val;
    return i;
  }

  /**
   * @brief Remove the data and reset the empty slot as invaidKey()
   * 
   * @return std::optional<u8> the idx of the removed key in the leaf if success
   */
  auto remove(const K &key) -> bool {
    for (u8 i = 0; i < N; ++i) {
      if (this->keys[i] == key) {
        std::copy(keys+i+1, keys+N, keys+i);
        std::copy(vals+i+1, vals+N, vals+i);
        keys[N-1] = invalidKey();
        return true;
      }
    }
    return false;
  }

  void range(const K& key, const int n, std::vector<V> &r_vals) {
    int i=0;
    while(i<N && r_vals.size()<n) {
      if(keys[i] == invalidKey()) break;
      if(keys[i] >= key) r_vals.push_back(this->vals[i]);
      i++;
    }
  }


  // ============== functions for degugging ================
  void print() {
    for(int i=0; i<N; i++) {
      std::cout<<keys[i]<<" ";
    }
    std::cout<<std::endl;
  }

  void self_check() {
    u8 i=N-1;
    while(keys[i]==invalidKey()) i--;
    for(; i>0; i--) ASSERT(keys[i]>keys[i-1]) << "Bad Leaf!";
  }


  //  ============ functions for remote machines =================
  /**
   * @brief Insert the data from the compute nodes
   *    Fixme: if need construct new leaf, we need insert existing data to the new one
   * 
   * @return std::pair<u8, u8> <the state of the insertion, the position>
   *    state: 0--> key exists,   1 --> insert here, 2--> insert here and create a new leaf,  3 --> insert to next leaf
   */
  auto insert_to_remote(const K &key, const V &val) -> std::pair<u8, u8> {
    u8 i=0;
    for (; i < N; ++i) {
      if (this->keys[i] == key) return std::make_pair(0, i);   // key exists
      if (this->keys[i] > key) break;
    }
    if(i>=N) return std::make_pair(3, 0); // insert to next leaf
    // insert into here
    u8 state = 1;
    u8 j=i;
    if(isfull()) {
      j=N-1;
      state = 2;
    } else {
      while(this->keys[j]!=invalidKey()) j++;
    }
    ASSERT(j<N) << "Something wrong in data leaves";
    while(i!=j) {
      this->keys[j] = keys[j-1];
      this->vals[j] = vals[j-1];
      j--;
    }
    this->keys[i] = key;
    this->vals[i] = val;
    return std::make_pair(state, 0);
  }

};


} // namespace rolex