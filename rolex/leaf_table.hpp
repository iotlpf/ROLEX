#pragma once 

#include <limits.h>     /* CHAR_BIT */
#include <bitset>
#include <iostream>
#include <vector>

#include "r2/src/common.hh"
#include "xutils/marshal.hh"
#include "xutils/spin_lock.hh"



namespace rolex {

using namespace r2;


/**
 * @brief Create all __ONE_COUNT__ 1s for __TYPE__ data
 *    This nice code comes from: 
 * https://stackoverflow.com/questions/1392059/algorithm-to-generate-bit-mask
 */
template <typename R> static constexpr R bitmask(unsigned int const onecount) {
  return static_cast<R>(-(onecount != 0)) &
         (static_cast<R>(-1) >> ((sizeof(R) * CHAR_BIT) - onecount));
}


const u64 kInvalidAddr = std::numeric_limits<u64>::max();   /// the invalid addr
const u32 kAddrBit = 56;
const u32 kLeafBit = 48;
const u64 kAddrMask = bitmask<u64>(kAddrBit);
const u64 kLeafMask = bitmask<u64>(kLeafBit);
const u8 kNonLock = bitmask<u8>(7);
 
const u32 kSynonymMax = 128;


/**
 * @brief Help functions for encode and decode.
 *          bits set: [1, 7, 8, 48] = [lock, leaf region, synonym leaf, leaves]
 */
auto encode(const u64& num, const u8& synonym_leaf = 0, const u8& leaf_region = 0) -> u64 {
  assert(num < (1L << kAddrBit) && leaf_region < (1L<<7));
  auto temp = (u64)leaf_region<<kAddrBit;
  temp |= (u64)synonym_leaf<<kLeafBit;
  temp |= (num & kAddrMask);
  return temp;
}

inline auto decode(const u64& encode_num) -> std::tuple<u64, u8, u8> { 
  u64 tmp = encode_num;
  u64 leaf_num = tmp & kLeafMask;
  u64 *ptr = &tmp;
  u8 synonym_num = static_cast<u8>(reinterpret_cast<char *>(ptr)[6]);
  u8 leaf_region = static_cast<u8>(reinterpret_cast<char *>(ptr)[7]) & kNonLock;
  return {leaf_num, synonym_num, leaf_region};
}


union TableEntry {
  struct {
    uint64_t lock: 1;
    uint64_t leaf_region: 7;
    uint64_t synonym_leaf: 8;
    uint64_t leaf_num: 48;
  };
  uint64_t val;
};
using TE = TableEntry;

struct leaf_addr {
  int off;   // offset of TE in leaf table
  TE addr;
};
using leaf_addr_t = leaf_addr;

/**
 * @brief Used in memory nodes, contains the Leaf table and Synonym table
 * 
 */
template<typename K, typename V, typename leaf_t, typename leaf_alloc_t>
struct LeafTable {

  std::vector<TE> table;
  TE SynonymTable[kSynonymMax];
  std::vector<::xstore::util::SpinLock*> csLocks;

  LeafTable() { 
    SynonymTable[0].leaf_num = 1;           // SynonymTable[0] indicate the next available slot 
  }

  auto table_size() -> usize { return table.size(); }

  u64 operator[](int i) {
    assert(i<table.size());
    return table[i].val;
  }

  // =============== API funcitons for submodels {debugging} ===============
  /**
   * @brief Add a number of the data nodes into address table
   *          Note: used for training phase
   * @return usize the total size of existing table
   */
  auto train_emplace_back(const u64& leaf_num, const u8& synonym_leaf = 0, const u8& leaf_region = 0) -> usize {
    TE te = { {.lock=0, 
               .leaf_region=leaf_region,
               .synonym_leaf=synonym_leaf, 
               .leaf_num=leaf_num} };
    table.emplace_back(te);
    csLocks.emplace_back(new ::xstore::util::SpinLock());
    return table.size();
  }

  auto synonym_emplace_back(bool in_table, const u64 l_idx, const u64& leaf_num, const u8& synonym_leaf = 0, const u8& leaf_region = 0) -> usize {
    TE te = { {.lock=0, 
               .leaf_region=leaf_region,
               .synonym_leaf=synonym_leaf, 
               .leaf_num=leaf_num} };
    if(SynonymTable[0].leaf_num == kSynonymMax-1) {
      // LOG(5) << "Synonym table is full, need retraining";
      return 0;
    }
    auto idx = SynonymTable[0].leaf_num++;
    if(in_table){
      te.synonym_leaf = table[l_idx].synonym_leaf;
      table[l_idx].synonym_leaf = idx;
    }
    else {
      te.synonym_leaf = SynonymTable[l_idx].synonym_leaf;
      SynonymTable[l_idx].synonym_leaf = idx;
    }
    SynonymTable[idx] = te;
    return idx;
  }

  void synonym_table_remove(std::vector<usize> s_leaves, const u64 l_idx, const int idx) {
    if(idx==0) table[l_idx].synonym_leaf = SynonymTable[s_leaves[idx]].synonym_leaf;
    else SynonymTable[s_leaves[idx-1]].synonym_leaf = SynonymTable[s_leaves[idx]].synonym_leaf;
  }

  auto search(const K &key, V &val, leaf_alloc_t* alloc, int lo, int hi) -> bool {
    ASSERT(hi<table.size()) << "[hi:table.size()] " << hi<<" : " << table.size();
    leaf_t* leaf;
    for(int i=hi; i>lo; i--){
      // leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[i].leaf_num));
      // if(leaf->search(key, val)) return true;
      leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[i].leaf_num));
      if(leaf->insertHere(key)) {
        // insert leaf and synonym leaf
        if(leaf->search(key, val)) return true;
        return search_synonym(key, val, i, alloc);
      }
    }
    leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[lo].leaf_num));
    if(leaf->search(key, val)) return true;
    return search_synonym(key, val, lo, alloc);
  } 

  auto search_synonym(const K &key, V &val, const usize l_idx, leaf_alloc_t* alloc) -> bool {
    // obtain synonym leaf index
    usize s_idx = table[l_idx].synonym_leaf;
    std::vector<usize> s_leaves;
    while(s_idx!=0) {
      TE s_te = SynonymTable[s_idx];
      s_leaves.push_back(s_idx);
      s_idx = s_te.synonym_leaf;
    }
    for(int i=s_leaves.size()-1; i>=0; i--) {
      leaf_t* tem_leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(SynonymTable[s_leaves[i]].leaf_num));
      if(tem_leaf->search(key, val)) return true;
    }
    return false;
  }

  void range(const K& key, const int n, std::vector<V> &vals, leaf_alloc_t* alloc, int lo, int hi) {
    ASSERT(hi<table.size()) << "[hi:table.size()] " << hi<<" : " << table.size();
    leaf_t* leaf;
    int idx=-1;
    for(int i=hi; i>=lo; i--){
      leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[i].leaf_num));
      if(leaf->insertHere(key)) {
        range_synonym(key, n, vals, i, leaf, alloc);
        idx=i;
        break;
      }
    }
    if(idx==-1) idx=lo;
    else idx++;
    while(idx<table.size() && vals.size()<n) {
      leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[idx].leaf_num));
      leaf->range(key, n, vals);
      if(vals.size()>=n) return;
      next_range(key, n, vals, idx, alloc);
      idx++;
    }
  }

  void range_synonym(const K& key, const int n, std::vector<V> &vals, const usize l_idx, leaf_t* leaf, leaf_alloc_t* alloc) {
    leaf_t *cur = leaf;
    usize s_idx = table[l_idx].synonym_leaf;
    std::vector<usize> s_leaves;
    while(s_idx!=0) {
      TE s_te = SynonymTable[s_idx];
      s_leaves.push_back(s_idx);
      s_idx = s_te.synonym_leaf;
    }
    // To guarantee all data sorted, we traverse leaves from back
    int idx=-1;
    for(int i=s_leaves.size()-1; i>=0; i--) {
      leaf_t* tem_leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(SynonymTable[s_leaves[i]].leaf_num));
      if(tem_leaf->insertHere(key)) {
        cur = tem_leaf;
        idx=i;
        break;
      }
    }
    cur->range(key, n, vals);
    idx++;
    while(vals.size()<n && idx<s_leaves.size()) {
      leaf_t* tem_leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(SynonymTable[s_leaves[idx]].leaf_num));
      tem_leaf->range(key, n, vals);
      idx++;
    }
  }

  void next_range(const K& key, const int n, std::vector<V> &vals, const usize l_idx, leaf_alloc_t* alloc) {
    usize s_idx = table[l_idx].synonym_leaf;
    std::vector<usize> s_leaves;
    while(s_idx!=0) {
      TE s_te = SynonymTable[s_idx];
      s_leaves.push_back(s_idx);
      s_idx = s_te.synonym_leaf;
    }
    int idx=0;
    while(vals.size()<n && idx<s_leaves.size()) {
      leaf_t* tem_leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(SynonymTable[s_leaves[idx]].leaf_num));
      tem_leaf->range(key, n, vals);
      idx++;
    }
  }

  auto update(const K &key, const V &val, leaf_alloc_t* alloc, int lo, int hi) -> bool { 
    ASSERT(hi<table.size()) << "[hi:table.size()] " << hi<<" : " << table.size();
    leaf_t* leaf;
    for(int i=hi; i>lo; i--){
      // leaf_t* leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[i].leaf_num));
      // if(leaf->update(key, val)) return true;
      leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[i].leaf_num));
      if(leaf->insertHere(key)) {
        // update leaf and synonym leaf
        return update_synonym(key, val, i, leaf, alloc);
      }
    }
    leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[lo].leaf_num));
    return update_synonym(key, val, lo, leaf, alloc);
  } 

  auto update_synonym(const K &key, const V &val, const usize l_idx, leaf_t* leaf, leaf_alloc_t* alloc) -> bool {
    // lock the leaf
    lock_leaf(l_idx);
    // obtain synonym leaf index
    leaf_t *cur = leaf;
    usize s_idx = table[l_idx].synonym_leaf;
    std::vector<usize> s_leaves;
    while(s_idx!=0) {
      TE s_te = SynonymTable[s_idx];
      s_leaves.push_back(s_idx);
      s_idx = s_te.synonym_leaf;
    }
    // To guarantee all data sorted, we traverse leaves from back
    for(int i=s_leaves.size()-1; i>=0; i--) {
      leaf_t* tem_leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(SynonymTable[s_leaves[i]].leaf_num));
      if(tem_leaf->insertHere(key)) {
        cur = tem_leaf;
        break;
      }
    }
    bool res = cur->update(key, val);
    unlock_leaf(l_idx);
    return res;
  }

  /**
   * 1.find which leaf to insert
   * 2.insert into a leaf or a synonym leaf
   *      lock the leaf;
   */
  // fixme: use a better lock?
  //         should lock in the model region
  void lock_leaf(size_t idx) {
    while(table[idx].lock != 0) 
      asm volatile("pause\n" : : : "memory");
    table[idx].lock = 1;
  }
  void unlock_leaf(size_t idx) {
    ::r2::compile_fence();
    table[idx].lock = 0;
  }

  auto insert(const K &key, const V &val, leaf_alloc_t* alloc, int lo, int hi) -> bool {
    ASSERT(hi<table.size() && hi>=lo)<<"lo "<<lo<<", hi "<<hi<<", table.size() "<< table.size();
    // To guarantee all data sorted, we traverse leaves from back
    leaf_t* leaf;
    for(int i=hi; i>lo; i--){
      leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[i].leaf_num));
      if(leaf->insertHere(key)) {
        // insert leaf and synonym leaf
        return insert_synonym(key, val, i, leaf, alloc);
      }
    }
    leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[lo].leaf_num));
    return insert_synonym(key, val, lo, leaf, alloc);
  } 

  /**
   * Two cases: 1.insert into leaf  2.insert into synonym leaf
   * 
   */
  auto insert_synonym(const K &key, const V &val, const usize l_idx, leaf_t* leaf, leaf_alloc_t* alloc) -> bool {
    // lock the leaf
    if(SynonymTable[0].leaf_num == kSynonymMax-1) return false;
    // lock_leaf(l_idx);
    this->csLocks[l_idx]->lock();
    // LOG(4) << "lock " << l_idx;
    // obtain synonym leaf index
    leaf_t *cur = leaf;
    usize s_idx = table[l_idx].synonym_leaf;
    std::vector<usize> s_leaves;
    while(s_idx!=0) {
      TE s_te = SynonymTable[s_idx];
      s_leaves.push_back(s_idx);
      s_idx = s_te.synonym_leaf;
    }
    // To guarantee all data sorted, we traverse leaves from back
    int idx = -1;
    for(int i=s_leaves.size()-1; i>=0; i--) {
      // if(s_leaves[i]>SynonymTable[0].leaf_num) {
      //   LOG(2) <<"SynonymTable[0].leaf_num: "<<SynonymTable[0].leaf_num;
      //   for(auto sli:s_leaves) LOG(2) <<"sli: "<<sli;
      //   print(alloc);
      //   ASSERT(false);
      // }
      leaf_t* tem_leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(SynonymTable[s_leaves[i]].leaf_num));
      if(tem_leaf->insertHere(key)) {
        cur = tem_leaf;
        idx = s_leaves[i];
        break;
      }
    }
    if(cur->contain(key)) {
      // unlock_leaf(l_idx);
      this->csLocks[l_idx]->unlock();
      return false;
    }
    // insert into leaf: full?
    if(cur->isfull()) {
      if(SynonymTable[0].leaf_num == kSynonymMax-1) {
        // unlock_leaf(l_idx);
        this->csLocks[l_idx]->unlock();
        return false;
      }
      auto res = alloc->fetch_new_leaf();
      // insert into synonym table
      if(idx==-1)
        synonym_emplace_back(true, l_idx, res.second);
      else 
        synonym_emplace_back(false, idx, res.second);
      leaf_t *n_leaf = reinterpret_cast<leaf_t*>(res.first);    
      // move half data
      int mid = leaf_t::max_slot() / 2;
      for(int i=0; i<mid; i++) {
        n_leaf->keys[i] = cur->keys[mid+i];
        n_leaf->vals[i] = cur->vals[mid+i];
        cur->keys[mid+i] = leaf_t::invalidKey();
      }
      // insert into new leaf?
      if(n_leaf->insertHere(key)) cur = n_leaf;
    }
    cur->insert_not_full(key, val);
    // LOG(3) << "unlock " << l_idx;
    // unlock_leaf(l_idx);
    this->csLocks[l_idx]->unlock();
    return true;
  }

  auto remove(const K &key, leaf_alloc_t* alloc, int lo, int hi) -> bool {
    ASSERT(hi<table.size()) << "[hi:table.size()] " << hi<<" : " << table.size();
    leaf_t* leaf;
    for(int i=hi; i>lo; i--){
      leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[i].leaf_num));
      if(leaf->insertHere(key)) {
        return remove_synonym(key, i, leaf, alloc);
      }
    }
    leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[lo].leaf_num));
    return remove_synonym(key, lo, leaf, alloc);
  }

  auto remove_synonym(const K &key, const usize l_idx, leaf_t* leaf, leaf_alloc_t* alloc) -> bool {
    // lock the leaf
    lock_leaf(l_idx);
    // obtain synonym leaf index
    leaf_t *cur = leaf;
    usize s_idx = table[l_idx].synonym_leaf;
    std::vector<usize> s_leaves;
    int idx = -1;
    while(s_idx!=0) {
      TE s_te = SynonymTable[s_idx];
      s_leaves.push_back(s_idx);
      s_idx = s_te.synonym_leaf;
    }
    // To guarantee all data sorted, we traverse leaves from back
    for(int i=s_leaves.size()-1; i>=0; i--) {
      leaf_t* tem_leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(SynonymTable[s_leaves[i]].leaf_num));
      if(tem_leaf->insertHere(key)) {
        cur = tem_leaf;
        idx = i;
        break;
      }
    }
    bool res = cur->remove(key);
    if(cur->isEmpty()) {
      if(idx!=-1)
        synonym_table_remove(s_leaves, l_idx, idx);
    }
    unlock_leaf(l_idx);
    return res;
  }

  // =============== functions for obtaining leaf numbers ===========================
  /**
   * @brief Get the leaf addresses of range [lo, hi]
   * 
   * @param leaves put the leaf addresses in vector 
   */
  void get_leaf_addr(const usize lo, const usize hi, std::vector<leaf_addr_t> &leaves) {
    ASSERT(hi<table.size());
    for(int i=lo; i<=hi; i++) {
      // leaves.push_back(table[i]);
      leaf_addr_t addr = {.off=i, .addr=table[i]};
      leaves.emplace_back(addr);
    }
  }




  // ============== functions for serialization and deserialization ===================
  /**
   * Deserialize the string to form a LeafTable: table_size, table, SynonymTable
   */
  void deserialize(const std::string_view& seria){
    ASSERT(seria.size() > sizeof(i32) && table.size()==0) <<seria.size();
    char* cur_ptr = (char *)seria.data();
    auto table_size = ::xstore::util::Marshal<i32>::deserialize(cur_ptr, seria.size());
    cur_ptr += sizeof(i32);

    ASSERT(seria.size() >= sizeof(i32) + (table_size + kSynonymMax)*sizeof(u64)) << "seria.size: "
                  << seria.size()<<" "<< sizeof(i32) + (table_size + kSynonymMax)*sizeof(u64);
    for(int i=0; i<table_size; i++) {
      table.push_back(::xstore::util::Marshal<TE>::deserialize(cur_ptr, seria.size()));
      cur_ptr += sizeof(TE);
    }
    for(int i=0; i<kSynonymMax; i++) {
      SynonymTable[i].val = ::xstore::util::Marshal<u64>::deserialize(cur_ptr, seria.size());
      cur_ptr += sizeof(u64);
    }
  }

  auto serialize() -> std::string {
    std::string res;
    res += ::xstore::util::Marshal<i32>::serialize_to(table.size());
    for(int i=0; i<table.size(); i++)
      res += ::xstore::util::Marshal<u64>::serialize_to(table[i].val);
    for(int i=0; i<kSynonymMax; i++)
      res += ::xstore::util::Marshal<u64>::serialize_to(SynonymTable[i].val);

    return res;
  }

  /**
   * @brief Print the trained node numbers for debugging
   * 
   */
  void print() {
    std::cout << "leaf table.size: " << table.size() << " ; Synonym Table available: " << SynonymTable[0].leaf_num<<std::endl;
    for(int i=0; i<table.size(); i++) {
      std::cout<<"["<< table[i].leaf_num <<", "<<table[i].synonym_leaf<<", "<<table[i].leaf_region<<"] ";
    }
    if(SynonymTable[0].leaf_num>1) {
      LOG(2)<<"Synonym leaves";
      for(int i=1; i<SynonymTable[0].leaf_num; i++) {
        std::cout<<"["<< SynonymTable[i].leaf_num <<", "<<SynonymTable[i].synonym_leaf<<", "<<SynonymTable[i].leaf_region<<"] ";
      }
    }
    std::cout<<std::endl;
  }

  void print(leaf_alloc_t* alloc) {
    std::cout << "Leaves -> table.size: " << table.size() << " ; Synonym Table available: " << SynonymTable[0].leaf_num<<std::endl;
    for(int i=0; i<table.size(); i++) {
      std::cout<<"["<< table[i].leaf_num <<", "<<table[i].synonym_leaf<<", "<<table[i].leaf_region<<"] ";
      leaf_t* leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(table[i].leaf_num));
      leaf->print();
    }
    if(SynonymTable[0].leaf_num>1) {
      LOG(2)<<"Synonym leaves";
      for(int i=1; i<SynonymTable[0].leaf_num; i++) {
        std::cout<<"["<< SynonymTable[i].leaf_num <<", "<<SynonymTable[i].synonym_leaf<<", "<<SynonymTable[i].leaf_region<<"] ";
        leaf_t* leaf = reinterpret_cast<leaf_t*>(alloc->get_leaf(SynonymTable[i].leaf_num));
        leaf->print();
      }
    }
  }

};


} // namespace rolex