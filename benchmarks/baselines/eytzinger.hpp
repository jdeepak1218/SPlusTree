/*
MIT License

Copyright (c) 2026 SPlusTree contributors
Copyright (c) 2025 Ragnar Groot Koerkamp

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace sst {

namespace detail {
template<class T>
struct CacheAlignedAllocator {
  using value_type = T;
  CacheAlignedAllocator() = default;
  template<class U>
  CacheAlignedAllocator(const CacheAlignedAllocator<U>&) noexcept {}
  T* allocate(std::size_t count)
  {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_array_new_length();
    }
    return static_cast<T*>(::operator new(count * sizeof(T), std::align_val_t{64}));
  }
  void deallocate(T* pointer, std::size_t) noexcept
  {
    ::operator delete(pointer, std::align_val_t{64});
  }
};
template<class T, class U>
bool operator==(const CacheAlignedAllocator<T>&, const CacheAlignedAllocator<U>&) noexcept
{
  return true;
}
} // namespace detail

class Eytzinger {
public:
  explicit Eytzinger(const std::vector<std::uint32_t>& values)
      : keys_(checked_slots(values)), sorted_positions_(keys_.size())
  {
    keys_[0] = std::numeric_limits<std::uint32_t>::max();
    std::size_t next = 0;
    if (!values.empty()) {
      build(values, 1, next);
    }
  }

  std::size_t size() const noexcept
  {
    return keys_.empty() ? 0 : keys_.size() - 1;
  }

  // Vector buffers only; excludes the object and allocator metadata.
  std::size_t storage_bytes() const noexcept
  {
    return keys_.capacity() * sizeof(std::uint32_t)
           + sorted_positions_.capacity() * sizeof(std::size_t);
  }

  std::size_t lower_bound(std::uint32_t query) const noexcept
  {
    const auto n = size();
    std::size_t node = 1;
    std::size_t answer = n;
    while (node <= n) {
      if (keys_[node] >= query) {
        answer = sorted_positions_[node];
        // Check child existence before multiplying to avoid index overflow.
        if (node > n / 2) {
          break;
        }
        node *= 2;
      } else {
        if (node > (n - 1) / 2) {
          break;
        }
        node = node * 2 + 1;
      }
    }
    return answer;
  }

  std::size_t lower_bound_prefetch(std::uint32_t query) const noexcept
  {
    const auto n = size();
    std::size_t node = 1;
    std::size_t answer = n;
    while (node <= n) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(SST_DISABLE_PREFETCH)
      // Keep hints in this loop: GCC 13 can eliminate a hint-only helper call.
      // Check before multiplying; clamp incomplete four-level descendant blocks.
      if (node <= n / 16) {
        const auto first = node * 16;
        const auto last = first + std::min<std::size_t>(15, n - first);
        // Retain the earlier two-endpoint/T2 experiment for comparison.
        __builtin_prefetch(keys_.data() + first, 0, 1);
        __builtin_prefetch(keys_.data() + last, 0, 1);
      }
#endif
      if (keys_[node] >= query) {
        answer = sorted_positions_[node];
        if (node > n / 2) {
          break;
        }
        node *= 2;
      } else {
        if (node > (n - 1) / 2) {
          break;
        }
        node = node * 2 + 1;
      }
    }
    return answer;
  }

  // Comparison-based traversal with four-level prefetching.
  std::size_t lower_bound_optimized(std::uint32_t query) const noexcept
  {
    const auto node = optimized_node<true>(query);
    return node == 0 ? size() : sorted_positions_[node];
  }

  std::size_t lower_bound_optimized_no_prefetch(std::uint32_t query) const noexcept
  {
    const auto node = optimized_node<false>(query);
    return node == 0 ? size() : sorted_positions_[node];
  }

  // Returns a value or sentinel; use the index API to distinguish
  // an actual UINT32_MAX key from an absent lower bound.
  std::uint32_t search_value_optimized(std::uint32_t query) const noexcept
  {
    const auto node = optimized_node<true>(query);
    return node == 0 ? std::numeric_limits<std::uint32_t>::max() : keys_[node];
  }

private:
  std::vector<std::uint32_t, detail::CacheAlignedAllocator<std::uint32_t>> keys_;
  std::vector<std::size_t> sorted_positions_;

  template<bool Prefetch>
  std::size_t optimized_node(std::uint32_t query) const noexcept
  {
    const auto n = size();
    std::size_t node = 1;
    if constexpr (Prefetch) {
      while (node <= n / 16) {
        node = 2 * node + static_cast<std::size_t>(query > keys_[node]);
#if (defined(__GNUC__) || defined(__clang__)) && !defined(SST_DISABLE_PREFETCH)
        // Prefetch after updating node. Some targets exceed n;
        // skip those hints rather than form an out-of-allocation pointer.
        if (node <= n / 16) {
          __builtin_prefetch(keys_.data() + 16 * node, 0, 3);
        }
#endif
      }
    }
    while (node <= n) {
      node = 2 * node + static_cast<std::size_t>(query > keys_[node]);
    }
    // Drop trailing right edges, then the final left edge. All-right paths
    // have no candidate. Guard C++ shifts by the word width (undefined).
    const auto shift = std::countr_one(node) + 1;
    return shift >= std::numeric_limits<std::size_t>::digits ? 0 : node >> shift;
  }

  static std::size_t checked_slots(const std::vector<std::uint32_t>& values)
  {
    if (!std::is_sorted(values.begin(), values.end())) {
      throw std::invalid_argument("Eytzinger input must be sorted");
    }
    if (values.size() >= std::vector<std::uint32_t>{}.max_size()
        || values.size() >= std::vector<std::size_t>{}.max_size()
        || values.size() > (std::numeric_limits<std::size_t>::max() - 1) / 2) {
      throw std::length_error("Eytzinger input is too large");
    }
    return values.size() + 1;
  }

  void build(const std::vector<std::uint32_t>& values,
             std::size_t node, std::size_t& next)
  {
    const auto n = values.size();
    if (node <= n / 2) {
      build(values, node * 2, next);
    }
    keys_[node] = values[next];
    sorted_positions_[node] = next++;
    if (node <= (n - 1) / 2) {
      build(values, node * 2 + 1, next);
    }
  }
};

} // namespace sst
