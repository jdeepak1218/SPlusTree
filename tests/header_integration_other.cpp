#include "stree.hpp"

std::size_t lookup_from_other_translation_unit()
{
  const sst::StaticSearchTree tree({0, 0xffffffffU});
  return tree.lower_bound(0xffffffffU);
}
