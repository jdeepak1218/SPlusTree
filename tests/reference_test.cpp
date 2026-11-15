#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

std::size_t reference_lower_bound(
    const std::vector<std::uint32_t>& values, std::uint32_t query)
{
  const auto it = std::lower_bound(values.begin(), values.end(), query);
  return static_cast<std::size_t>(it - values.begin());
}

std::size_t binary_lower_bound(
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

int main()
{
  const std::vector<std::uint32_t> values{2, 5, 5, 9, 14};
  assert(reference_lower_bound(values, 0) == 0);
  assert(reference_lower_bound(values, 5) == 1);
  assert(reference_lower_bound(values, 6) == 3);
  assert(reference_lower_bound(values, 14) == 4);
  assert(reference_lower_bound(values, 15) == values.size());

  const std::vector<std::uint32_t> empty;
  assert(reference_lower_bound(empty, 7) == 0);

  const std::vector<std::uint32_t> single{5};
  assert(reference_lower_bound(single, 4) == 0);
  assert(reference_lower_bound(single, 5) == 0);
  assert(reference_lower_bound(single, 6) == 1);

  const std::vector<std::uint32_t> repeated{5, 5, 5};
  assert(reference_lower_bound(repeated, 4) == 0);
  assert(reference_lower_bound(repeated, 5) == 0);
  assert(reference_lower_bound(repeated, 6) == repeated.size());

  const auto max_key = std::numeric_limits<std::uint32_t>::max();
  const std::vector<std::uint32_t> extremes{0, max_key};
  assert(reference_lower_bound(extremes, 0) == 0);
  assert(reference_lower_bound(extremes, max_key) == 1);

  const std::vector<std::vector<std::uint32_t>> datasets{
      values, empty, single, repeated, extremes
  };
  const std::vector<std::uint32_t> queries{
      0, 1, 2, 4, 5, 6, 9, 14, 15, max_key
  };

  for (const auto& data : datasets) {
    for (const auto query : queries) {
      assert(binary_lower_bound(data, query)
             == reference_lower_bound(data, query));
    }
  }
}
