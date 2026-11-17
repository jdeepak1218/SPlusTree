#include "stree.hpp"
#include "stree.hpp"

std::size_t lookup_from_other_translation_unit();

int main()
{
  const sst::StaticSearchTree tree({2, 5, 5, 9, 14});
  const std::array<std::uint32_t, 4> queries{6, 5, 20, 0};
  const auto answers = tree.lower_bound_batch(queries);
  return answers == std::vector<std::size_t>{3, 1, 5, 0}
      && lookup_from_other_translation_unit() == 1 ? 0 : 1;
}
