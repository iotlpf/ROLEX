#pragma once


#include "leaf.hpp"
#include "leaf_allocator.hpp"
#include "model_allocator.hpp"
#include "leaf_table.hpp"
#include "rolex.hpp"
#include "learned_cache.hpp"
#include "remote_memory.hh"

namespace rolex {

using K = u64;
using V = u64;
using leaf_t = Leaf<64, K, V>;
using leaf_alloc_t = LeafAllocator<leaf_t, sizeof(leaf_t)>;
using model_alloc_t = ModelAllocator<K>;
using remote_memory_t = RemoteMemory<leaf_alloc_t, model_alloc_t>;
using leaf_table_t = LeafTable<K, V, leaf_t, leaf_alloc_t>;
using rolex_t = Rolex<K, V, leaf_t, leaf_alloc_t, remote_memory_t, 32>;
using learned_cache_t = LearnedCache<K, V, leaf_t, leaf_alloc_t, 32>;



}