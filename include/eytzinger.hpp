#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace sst {

class Eytzinger {
public:
  explicit Eytzinger(const std::vector<std::uint32_t>& values)
      : keys_(checked_slots(values)), sorted_positions_(keys_.size())
  {
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

private:
  std::vector<std::uint32_t> keys_;
  std::vector<std::size_t> sorted_positions_;

  static std::size_t checked_slots(const std::vector<std::uint32_t>& values)
  {
    if (!std::is_sorted(values.begin(), values.end())) {
      throw std::invalid_argument("Eytzinger input must be sorted");
    }
    if (values.size() >= std::vector<std::uint32_t>{}.max_size()
        || values.size() >= std::vector<std::size_t>{}.max_size()) {
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
