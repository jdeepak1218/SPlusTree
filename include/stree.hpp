#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

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

private:
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
};

} // namespace sst
