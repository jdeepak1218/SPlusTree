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
