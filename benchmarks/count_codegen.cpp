#include "stree.hpp"

// Stable symbols for inspecting just the query code in compiler output.
extern "C" std::size_t stree_count_lookup(
    const sst::STree<>& tree, std::uint32_t query) noexcept
{
  return tree.lower_bound_count(query);
}

extern "C" std::size_t stree_early_lookup(
    const sst::STree<>& tree, std::uint32_t query) noexcept
{
  return tree.lower_bound(query);
}

extern "C" std::size_t stree_mask_lookup(
    const sst::STree<>& tree, std::uint32_t query) noexcept
{
  return tree.lower_bound_mask(query);
}

extern "C" std::size_t stree_popcount_lookup(
    const sst::STree<>& tree, std::uint32_t query) noexcept
{
  return tree.lower_bound_popcount(query);
}

extern "C" std::size_t stree_packed_lookup(
    const sst::STree<>& tree, std::uint32_t query) noexcept
{
  return tree.lower_bound_packed(query);
}

extern "C" void stree_batch_prefetch_lookup(
    const sst::STree<>& tree, const std::array<std::uint32_t, 16>& queries,
    std::array<std::size_t, 16>& results) noexcept
{
  results = tree.lower_bound_batch_prefetch(queries);
}
