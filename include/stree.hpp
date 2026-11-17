#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#endif

namespace sst {

template<std::size_t KeysPerNode = 16>
class STree {
  static_assert(KeysPerNode > 0 && KeysPerNode <= 16);
  static constexpr std::size_t fanout = KeysPerNode + 1;
  struct alignas(64) Node {
    std::array<std::uint32_t, KeysPerNode> keys;
  };
  static_assert(sizeof(Node) == 64);

public:
  explicit STree(const std::vector<std::uint32_t>& values) : size_(values.size())
  {
    if (!std::is_sorted(values.begin(), values.end())) {
      throw std::invalid_argument("STree input must be sorted");
    }
    if (values.empty()) return;

    // Count bottom-up, then pack the levels in root-to-leaf order.
    std::vector<std::size_t> counts{ceil_div(size_, KeysPerNode)};
    while (counts.back() > 1) counts.push_back(ceil_div(counts.back(), fanout));
    std::reverse(counts.begin(), counts.end());
    std::size_t total = 0;
    for (const auto count : counts) {
      if (count > nodes_.max_size() - total) {
        throw std::length_error("STree input is too large");
      }
      offsets_.push_back(total);
      total += count;
    }
    nodes_.resize(total);
    for (auto& node : nodes_) node.keys.fill(std::numeric_limits<std::uint32_t>::max());

    const auto leaf_offset = offsets_.back();
    for (std::size_t i = 0; i < size_; ++i) {
      nodes_[leaf_offset + i / KeysPerNode].keys[i % KeysPerNode] = values[i];
    }
    std::vector<std::uint32_t> minima(counts.back());
    for (std::size_t i = 0; i < minima.size(); ++i) minima[i] = values[i * KeysPerNode];
    for (std::size_t level = counts.size() - 1; level > 0; --level) {
      std::vector<std::uint32_t> parent_minima(counts[level - 1]);
      for (std::size_t parent = 0; parent < parent_minima.size(); ++parent) {
        const auto first = parent * fanout;
        parent_minima[parent] = minima[first];
        auto& node = nodes_[offsets_[level - 1] + parent];
        // Separator j is the minimum key of child j + 1.
        for (std::size_t child = 1; child <= KeysPerNode && child < minima.size() - first; ++child) {
          node.keys[child - 1] = minima[first + child];
        }
      }
      minima = std::move(parent_minima);
    }
  }

  std::size_t size() const noexcept { return nodes_.empty() ? 0 : size_; }

  std::size_t storage_bytes() const noexcept
  {
    return nodes_.capacity() * sizeof(Node) + offsets_.capacity() * sizeof(std::size_t);
  }

  std::size_t lower_bound(std::uint32_t query) const noexcept
  {
    const auto n = size();
    if (n == 0) return 0;
    std::size_t node = 0;
    for (std::size_t level = 0; level + 1 < offsets_.size(); ++level) {
      const auto child = find_linear(nodes_[offsets_[level] + node], query);
      node = node * fanout + child;
    }
    const auto slot = find_linear(nodes_[offsets_.back() + node], query);
    // A slot equal to KeysPerNode intentionally spills to the next leaf.
    return std::min(n, node * KeysPerNode + slot);
  }

  std::size_t lower_bound_count(std::uint32_t query) const noexcept
  {
    const auto n = size();
    if (n == 0) return 0;
    std::size_t node = 0;
    for (std::size_t level = 0; level + 1 < offsets_.size(); ++level) {
      const auto child = find_count(nodes_[offsets_[level] + node], query);
      node = node * fanout + child;
    }
    const auto slot = find_count(nodes_[offsets_.back() + node], query);
    return std::min(n, node * KeysPerNode + slot);
  }

  std::size_t lower_bound_mask(std::uint32_t query) const noexcept
  {
    const auto n = size();
    if (n == 0) return 0;
    std::size_t node = 0;
    for (std::size_t level = 0; level + 1 < offsets_.size(); ++level) {
      const auto child = find_mask(nodes_[offsets_[level] + node], query);
      node = node * fanout + child;
    }
    const auto slot = find_mask(nodes_[offsets_.back() + node], query);
    return std::min(n, node * KeysPerNode + slot);
  }

  std::size_t lower_bound_popcount(std::uint32_t query) const noexcept
  {
    const auto n = size();
    if (n == 0) return 0;
    std::size_t node = 0;
    for (std::size_t level = 0; level + 1 < offsets_.size(); ++level) {
      const auto child = find_popcount(nodes_[offsets_[level] + node], query);
      node = node * fanout + child;
    }
    const auto slot = find_popcount(nodes_[offsets_.back() + node], query);
    return std::min(n, node * KeysPerNode + slot);
  }

  std::size_t lower_bound_packed(std::uint32_t query) const noexcept
  {
    const auto n = size();
    if (n == 0) return 0;
    std::size_t node = 0;
    for (std::size_t level = 0; level + 1 < offsets_.size(); ++level) {
      const auto child = find_packed(nodes_[offsets_[level] + node], query);
      node = node * fanout + child;
    }
    const auto slot = find_packed(nodes_[offsets_.back() + node], query);
    return std::min(n, node * KeysPerNode + slot);
  }

  template<std::size_t BatchSize>
  std::array<std::size_t, BatchSize> lower_bound_batch(
      const std::array<std::uint32_t, BatchSize>& queries) const noexcept
  {
    return batch_impl<BatchSize, false>(queries);
  }

  template<std::size_t BatchSize>
  std::array<std::size_t, BatchSize> lower_bound_batch_prefetch(
      const std::array<std::uint32_t, BatchSize>& queries) const noexcept
  {
    return batch_impl<BatchSize, true>(queries);
  }

  // Query and output spans must have equal lengths and must not overlap.
  template<std::size_t BatchSize>
  void lower_bound_batch(std::span<const std::uint32_t> queries,
                        std::span<std::size_t> results) const
  {
    batch_into<BatchSize, false>(queries, results);
  }

  template<std::size_t BatchSize>
  void lower_bound_batch_prefetch(std::span<const std::uint32_t> queries,
                        std::span<std::size_t> results) const
  {
    batch_into<BatchSize, true>(queries, results);
  }

private:
  template<std::size_t BatchSize, bool Prefetch>
  std::array<std::size_t, BatchSize> batch_impl(
      const std::array<std::uint32_t, BatchSize>& queries) const noexcept
  {
    static_assert(BatchSize > 0);
    std::array<std::size_t, BatchSize> positions{};
    std::array<std::size_t, BatchSize> results{};
    const auto n = size();
    if (n == 0) return results;

    // Advance all independent queries before proceeding to the next level.
    for (std::size_t level = 0; level + 1 < offsets_.size(); ++level) {
      const auto offset = offsets_[level];
      for (std::size_t i = 0; i < BatchSize; ++i) {
        const auto& node = nodes_[offset + positions[i]];
        const auto child = find_packed(node, queries[i]);
        positions[i] = positions[i] * fanout + child;
#if (defined(__GNUC__) || defined(__clang__)) && !defined(SST_DISABLE_PREFETCH)
        if constexpr (Prefetch) {
          const auto next_offset = offsets_[level + 1];
          // Check the index before forming a pointer, even for a non-faulting hint.
          if (positions[i] < nodes_.size() - next_offset) {
            __builtin_prefetch(nodes_.data() + next_offset + positions[i], 0, 3);
          }
        }
#endif
      }
    }
    const auto leaf_offset = offsets_.back();
    for (std::size_t i = 0; i < BatchSize; ++i) {
      const auto& leaf = nodes_[leaf_offset + positions[i]];
      const auto slot = find_packed(leaf, queries[i]);
      results[i] = std::min(n, positions[i] * KeysPerNode + slot);
    }
    return results;
  }

  // Query and output spans must have equal lengths and must not overlap.
  template<std::size_t BatchSize, bool Prefetch>
  void batch_into(std::span<const std::uint32_t> queries,
                        std::span<std::size_t> results) const
  {
    static_assert(BatchSize > 0);
    if (queries.size() != results.size()) {
      throw std::invalid_argument("Batch query and output lengths must match");
    }
    std::array<std::uint32_t, BatchSize> batch{};
    std::size_t offset = 0;
    while (queries.size() - offset >= BatchSize) {
      std::copy_n(queries.begin() + static_cast<std::ptrdiff_t>(offset), BatchSize, batch.begin());
      const auto answers = batch_impl<BatchSize, Prefetch>(batch);
      std::copy(answers.begin(), answers.end(), results.begin() + static_cast<std::ptrdiff_t>(offset));
      offset += BatchSize;
    }
    for (; offset < queries.size(); ++offset) results[offset] = lower_bound_packed(queries[offset]);
  }

  std::size_t size_ = 0;
  std::vector<Node> nodes_;
  std::vector<std::size_t> offsets_;

  static std::size_t ceil_div(std::size_t n, std::size_t divisor) noexcept
  {
    return n / divisor + static_cast<std::size_t>(n % divisor != 0);
  }

  static std::size_t find_linear(const Node& node, std::uint32_t query) noexcept
  {
    for (std::size_t i = 0; i < KeysPerNode; ++i) {
      if (node.keys[i] >= query) return i;
    }
    return KeysPerNode;
  }

  static std::size_t find_count(const Node& node, std::uint32_t query) noexcept
  {
    // Fixed-length reduction permits auto-vectorization; the count is <= 16.
    std::uint32_t count = 0;
#if defined(__GNUC__) && !defined(__clang__)
    // Preserve the loop for vectorization before GCC's early scalar unrolling.
#pragma GCC unroll 1
#endif
    for (std::size_t i = 0; i < KeysPerNode; ++i) {
      count += static_cast<std::uint32_t>(node.keys[i] < query);
    }
    return count;
  }

  static std::size_t find_mask(const Node& node, std::uint32_t query) noexcept
  {
    std::uint32_t mask = 0;
#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__)) && !defined(SST_DISABLE_MASK_SIMD)
    if constexpr (KeysPerNode == 16) {
      const auto q = _mm256_set1_epi32(std::bit_cast<std::int32_t>(query));
      const auto low = _mm256_load_si256(reinterpret_cast<const __m256i*>(node.keys.data()));
      const auto high = _mm256_load_si256(reinterpret_cast<const __m256i*>(node.keys.data() + 8));
      // Unsigned min(key, q) == q is equivalent to key >= q.
      const auto low_matches = _mm256_cmpeq_epi32(_mm256_min_epu32(low, q), q);
      const auto high_matches = _mm256_cmpeq_epi32(_mm256_min_epu32(high, q), q);
      // One bit per 32-bit lane, preserving sorted positions across both halves.
      mask = static_cast<std::uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(low_matches)))
           | (static_cast<std::uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(high_matches))) << 8);
    } else
#endif
    {
      for (std::size_t i = 0; i < KeysPerNode; ++i) {
        mask |= static_cast<std::uint32_t>(node.keys[i] >= query) << i;
      }
    }
    // A marker returns the node width when no real lane qualifies; never scan zero.
    mask |= std::uint32_t{1} << KeysPerNode;
    return static_cast<std::size_t>(std::countr_zero(mask));
  }

  static std::size_t find_popcount(const Node& node, std::uint32_t query) noexcept
  {
    std::uint32_t mask = 0;
#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__)) && !defined(SST_DISABLE_MASK_SIMD)
    if constexpr (KeysPerNode == 16) {
      // Flip the sign bit so signed SIMD ordering matches unsigned key ordering.
      const auto bias = _mm256_set1_epi32(std::numeric_limits<std::int32_t>::min());
      const auto q = _mm256_set1_epi32(std::bit_cast<std::int32_t>(query ^ 0x80000000U));
      const auto low = _mm256_load_si256(reinterpret_cast<const __m256i*>(node.keys.data()));
      const auto high = _mm256_load_si256(reinterpret_cast<const __m256i*>(node.keys.data() + 8));
      const auto low_below = _mm256_cmpgt_epi32(q, _mm256_xor_si256(low, bias));
      const auto high_below = _mm256_cmpgt_epi32(q, _mm256_xor_si256(high, bias));
      mask = static_cast<std::uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(low_below)))
           | (static_cast<std::uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(high_below))) << 8);
    } else
#endif
    {
      for (std::size_t i = 0; i < KeysPerNode; ++i) {
        mask |= static_cast<std::uint32_t>(node.keys[i] < query) << i;
      }
    }
    // Exactly one bit per real key; no end marker or lane-order restoration.
    return static_cast<std::size_t>(std::popcount(mask));
  }

  static std::size_t find_packed(const Node& node, std::uint32_t query) noexcept
  {
#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__)) && !defined(SST_DISABLE_MASK_SIMD)
    if constexpr (KeysPerNode == 16) {
      const auto bias = _mm256_set1_epi32(std::numeric_limits<std::int32_t>::min());
      const auto q = _mm256_set1_epi32(std::bit_cast<std::int32_t>(query ^ 0x80000000U));
      const auto low = _mm256_load_si256(reinterpret_cast<const __m256i*>(node.keys.data()));
      const auto high = _mm256_load_si256(reinterpret_cast<const __m256i*>(node.keys.data() + 8));
      const auto low_below = _mm256_cmpgt_epi32(q, _mm256_xor_si256(low, bias));
      const auto high_below = _mm256_cmpgt_epi32(q, _mm256_xor_si256(high, bias));
      // True comparisons are -1, so signed saturation preserves them as 0xffff.
      const auto packed = _mm256_packs_epi32(low_below, high_below);
      const auto mask = static_cast<std::uint32_t>(_mm256_movemask_epi8(packed));
      // Each true 16-bit lane contributes two byte-sign bits; lane order is irrelevant.
      return static_cast<std::size_t>(std::popcount(mask)) / 2;
    } else
#endif
    {
      return find_popcount(node, query);
    }
  }
};

} // namespace sst
