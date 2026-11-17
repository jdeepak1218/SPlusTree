#include "stree.hpp"

#include <array>
#include <iostream>

int main()
{
  const sst::StaticSearchTree tree({2, 5, 5, 9, 14});
  const auto index = tree.lower_bound(6);
  if (index != tree.size()) std::cout << "Lower-bound key for 6: " << tree.key_at(index) << '\n';

  const std::array<std::uint32_t, 4> queries{6, 5, 20, 0};
  const auto positions = tree.lower_bound_batch(queries);
  for (auto position : positions) std::cout << position << ' ';
  std::cout << '\n';

  std::array<std::size_t, 4> output{};
  tree.lower_bound_batch(queries, output);
  return positions == std::vector<std::size_t>{3, 1, 5, 0}
      && std::equal(output.begin(), output.end(), positions.begin()) ? 0 : 1;
}
