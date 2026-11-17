#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sst {

inline std::size_t reference_lower_bound(
    const std::vector<std::uint32_t>& values, std::uint32_t query)
{
  const auto it = std::lower_bound(values.begin(), values.end(), query);
  return static_cast<std::size_t>(it - values.begin());
}

inline std::size_t binary_lower_bound(
    const std::vector<std::uint32_t>& values, std::uint32_t query)
{
  std::size_t left = 0;
  std::size_t right = values.size();

  while (left < right) {
    const auto mid = left + (right - left) / 2;
    if (values[mid] < query) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return left;
}

} // namespace sst
