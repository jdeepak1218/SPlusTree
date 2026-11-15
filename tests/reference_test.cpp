#include "static_search_tree.hpp"
#include "eytzinger.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

int main()
{
  const std::vector<std::uint32_t> values{2, 5, 5, 9, 14};
  assert(sst::reference_lower_bound(values, 0) == 0);
  assert(sst::reference_lower_bound(values, 5) == 1);
  assert(sst::reference_lower_bound(values, 6) == 3);
  assert(sst::reference_lower_bound(values, 14) == 4);
  assert(sst::reference_lower_bound(values, 15) == values.size());

  const std::vector<std::uint32_t> empty;
  assert(sst::reference_lower_bound(empty, 7) == 0);

  const std::vector<std::uint32_t> single{5};
  assert(sst::reference_lower_bound(single, 4) == 0);
  assert(sst::reference_lower_bound(single, 5) == 0);
  assert(sst::reference_lower_bound(single, 6) == 1);

  const std::vector<std::uint32_t> repeated{5, 5, 5};
  assert(sst::reference_lower_bound(repeated, 4) == 0);
  assert(sst::reference_lower_bound(repeated, 5) == 0);
  assert(sst::reference_lower_bound(repeated, 6) == repeated.size());

  const auto max_key = std::numeric_limits<std::uint32_t>::max();
  const std::vector<std::uint32_t> extremes{0, max_key};
  assert(sst::reference_lower_bound(extremes, 0) == 0);
  assert(sst::reference_lower_bound(extremes, max_key) == 1);

  const std::vector<std::vector<std::uint32_t>> datasets{
      values, empty, single, repeated, extremes
  };
  const std::vector<std::uint32_t> queries{
      0, 1, 2, 4, 5, 6, 9, 14, 15, max_key
  };

  for (const auto& data : datasets) {
    const sst::Eytzinger tree(data);
    for (const auto query : queries) {
      assert(sst::binary_lower_bound(data, query)
             == sst::reference_lower_bound(data, query));
      assert(tree.lower_bound(query) == sst::reference_lower_bound(data, query));
    }
  }
}
