#pragma once

#include "rlib/core/nicinfo.hh"      /// RNicInfo
#include "rlib/core/rctrl.hh"        /// RCtrl
#include "rlib/core/naming.hh"       /// DevIdx 
#include "rlib/core/lib.hh"
#include "huge_region.hh"

using namespace rdmaio;


namespace rolex {

class RM_config {
public:
  rdmaio::RCtrl* ctrl;
  uint64_t model_region_size;

  uint64_t leaf_region_size;
  uint64_t reg_leaf_region;
  uint64_t leaf_num;

  explicit RM_config(rdmaio::RCtrl* ctrl, uint64_t ms, uint64_t ls, uint64_t rlr, uint64_t ln) 
    : ctrl(ctrl), model_region_size(ms), leaf_region_size(ls), reg_leaf_region(rlr), leaf_num(ln) {}
};


template<typename leaf_alloc_t, typename model_alloc_t>
class RemoteMemory {

public:
  explicit RemoteMemory(const RM_config &conf) : conf(conf), ctrl(conf.ctrl) {
    // [RCtrl] register all NICs, and create model/leaf regions
    register_nic();
    create_regions();
    // create model/leaf allocators
    init_allocator();
  }

  auto leaf_allocator() -> leaf_alloc_t* { return this->leafAlloc; }

  auto model_allocator() -> model_alloc_t* { return this->modelAlloc; }

  void start_daemon() {
    ctrl->start_daemon();
  }

private:
  RM_config conf;
  rdmaio::RCtrl* ctrl;
  std::vector<DevIdx> all_nics;
  std::shared_ptr<rolex::HugeRegion> model_region;
  std::shared_ptr<rolex::HugeRegion> leaf_region;
  leaf_alloc_t* leafAlloc;
  model_alloc_t* modelAlloc;

  void register_nic() {
    all_nics = RNicInfo::query_dev_names();
    {
      for (uint i = 0; i < all_nics.size(); ++i) {
        auto nic = RNic::create(all_nics.at(i)).value();
        ASSERT(ctrl->opened_nics.reg(i, nic));
      }
    }
  }

  void create_regions() {
    // [RCtrl] create model regions
    model_region = HugeRegion::create(conf.model_region_size).value();
    for (uint i = 0; i < all_nics.size(); ++i) {
      ctrl->registered_mrs.create_then_reg(
        i, model_region->convert_to_rmem().value(), ctrl->opened_nics.query(i).value());
    }
    // [RCtrl] create leaf regions
    leaf_region = HugeRegion::create(conf.leaf_region_size).value();
    ctrl->registered_mrs.create_then_reg(
      conf.reg_leaf_region, leaf_region->convert_to_rmem().value(), ctrl->opened_nics.query(0).value());
  }

  void init_allocator() {
    ASSERT(leaf_region) << "Leaf region not exist";
    leafAlloc = new leaf_alloc_t(static_cast<char *>(leaf_region->start_ptr()), leaf_region->size(), conf.leaf_num);
    // leafAlloc = new leaf_alloc_t(static_cast<char *>(leaf_region->start_ptr()), leaf_region->size());
    ASSERT(model_region) << "Model region not exist";
    modelAlloc = new model_alloc_t(static_cast<char *>(model_region->start_ptr()), model_region->size());
  }
};

} // namespace rolex