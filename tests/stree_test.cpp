#include "stree.hpp"
#include "static_search_tree.hpp"

#include <iostream>
#include <random>
#include <string>

namespace {
constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
std::mt19937 generator(20261115);
std::size_t datasets = 0;
std::size_t queries = 0;

void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}

template<std::size_t B>
void verify(const std::vector<std::uint32_t>& values, std::size_t samples = 128)
{
  const sst::STree<B> tree(values);
  require(tree.size() == values.size(), "Wrong size");
  ++datasets;
  const auto check = [&](std::uint32_t q) {
    const auto expected = sst::reference_lower_bound(values, q);
    const auto actual = tree.lower_bound(q);
    const auto counted = tree.lower_bound_count(q);
    const auto masked = tree.lower_bound_mask(q);
    if (expected != actual || counted != expected || masked != expected) {
      throw std::runtime_error("Mismatch: B=" + std::to_string(B)
          + " n=" + std::to_string(values.size()) + " query=" + std::to_string(q)
          + " expected=" + std::to_string(expected) + " early=" + std::to_string(actual)
          + " count=" + std::to_string(counted) + " mask=" + std::to_string(masked));
    }
    ++queries;
  };
  for (const auto q : {0U, 1U, 0x7fffffffU, 0x80000000U, maximum - 1, maximum}) check(q);
  for (std::size_t i = 0; i < samples; ++i) check(static_cast<std::uint32_t>(generator()));
  const auto stride = values.size() <= 2048 ? 1 : std::max<std::size_t>(1, values.size() / samples);
  for (std::size_t i = 0; i < values.size(); i += stride) {
    const auto key = values[i];
    check(key);
    if (key > 0) check(key - 1);
    if (key < maximum) check(key + 1);
  }
  if (!values.empty()) check(values.back());
}

void exhaustive(std::vector<std::uint32_t>& values, std::size_t first)
{
  verify<2>(values, 1);
  verify<16>(values, 1);
  if (values.size() == 10) return;
  constexpr std::array<std::uint32_t, 3> alphabet{0, 1, maximum};
  for (auto i = first; i < alphabet.size(); ++i) {
    values.push_back(alphabet[i]);
    exhaustive(values, i);
    values.pop_back();
  }
}

template<std::size_t B>
void boundaries(unsigned heights)
{
  std::size_t capacity = B;
  for (unsigned height = 0; height < heights; ++height) {
    for (int delta = -2; delta <= 2; ++delta) {
      if (delta < 0 && capacity < static_cast<std::size_t>(-delta)) continue;
      const auto n = delta < 0 ? capacity - static_cast<std::size_t>(-delta)
                               : capacity + static_cast<std::size_t>(delta);
      std::vector<std::uint32_t> values(n);
      for (std::size_t i = 0; i < n; ++i) values[i] = static_cast<std::uint32_t>(i / 7);
      verify<B>(values);
    }
    capacity *= B + 1;
  }
}
} // namespace

int main()
try {
  std::vector<std::uint32_t> values;
  exhaustive(values, 0);
  for (std::size_t n = 0; n <= 1025; ++n) {
    values.resize(n);
    for (std::size_t i = 0; i < n; ++i) values[i] = static_cast<std::uint32_t>(i * 2);
    verify<2>(values);
    verify<16>(values);
  }
  boundaries<2>(9);
  boundaries<16>(5);
  for (std::size_t trial = 0; trial < 500; ++trial) {
    values.resize(generator() % 8193);
    for (auto& value : values) {
      switch (trial % 5) {
        case 0: value = static_cast<std::uint32_t>(generator()); break;
        case 1: value = static_cast<std::uint32_t>(generator() % 8); break;
        case 2: value = 0; break;
        case 3: value = maximum; break;
        default: value = maximum - static_cast<std::uint32_t>(generator() % 32); break;
      }
    }
    std::sort(values.begin(), values.end());
    verify<2>(values);
    verify<16>(values);
  }

  // Non-power-of-two widths exercise vector-loop tails and small reductions.
  for (const auto n : {0U, 1U, 2U, 3U, 7U, 8U, 15U, 16U, 17U, 31U, 32U, 33U, 255U, 256U, 257U, 1025U}) {
    values.resize(n);
    for (auto& value : values) value = static_cast<std::uint32_t>(generator());
    std::sort(values.begin(), values.end());
    if (!values.empty()) { values.front() = 0; values.back() = maximum; }
    verify<1>(values);
    verify<3>(values);
    verify<7>(values);
    verify<8>(values);
    verify<15>(values);
  }

  values.resize(1'000'003);
  for (auto& value : values) value = static_cast<std::uint32_t>(generator());
  std::sort(values.begin(), values.end());
  verify<16>(values, 10'000);
  values.resize(8'388'731);
  for (std::size_t i = 0; i < values.size(); ++i) values[i] = static_cast<std::uint32_t>(i / 31);
  values.back() = maximum;
  verify<16>(values, 20'000);
  values.assign(1'000'003, maximum);
  verify<16>(values);

  for (const auto& invalid : std::vector<std::vector<std::uint32_t>>{
           {2, 1}, {0, maximum, 1}, {3, 2, 1}, {1, 1, 0}}) {
    bool rejected = false;
    try { const sst::STree<> tree(invalid); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Unsorted input accepted");
  }

  const sst::STree<2> example({2, 5, 8, 11, 14, 17});
  require(example.lower_bound(8) == 2, "Leaf spill failed");
  require(example.lower_bound(14) == 4, "Leaf spill failed");
  require(example.lower_bound_count(8) == 2 && example.lower_bound_count(14) == 4,
          "Counting leaf spill failed");
  const sst::STree<2> duplicates({2, 5, 5, 5, 5, 9, 14, 17});
  require(duplicates.lower_bound(5) == 1, "First duplicate lost");
  require(duplicates.lower_bound_count(5) == 1, "Counting first duplicate lost");
  require(duplicates.lower_bound_mask(5) == 1, "Mask first duplicate lost");
  // Exercise every suffix mask, including the 7/8 lane boundary and no match.
  for (std::size_t split = 0; split <= 16; ++split) {
    std::vector<std::uint32_t> lanes(16, maximum);
    std::fill_n(lanes.begin(), split, 0U);
    const sst::STree<> lane_tree(lanes);
    require(lane_tree.lower_bound_mask(0) == 0, "All-match mask failed");
    require(lane_tree.lower_bound_mask(1) == split, "Lane order or end marker failed");
    require(lane_tree.lower_bound_mask(maximum) == split, "Unsigned mask failed");
  }
  values = {2, 5, 5, 9};
  const sst::STree<> owned(values);
  values.assign(100, 0);
  require(owned.lower_bound(5) == 1, "Source mutation changed tree");
  require(owned.lower_bound_count(5) == 1, "Source mutation changed counting search");
  require(owned.lower_bound_mask(5) == 1, "Source mutation changed mask search");
  auto copy = owned;
  auto moved = std::move(copy);
  require(moved.lower_bound(6) == 3, "Move lost data");
  copy = owned;
  require(copy.lower_bound(10) == 4, "Copy assignment lost data");

  std::cout << "PASS: " << datasets << " datasets, " << queries
            << " differential queries; rejection, leaf-spill, duplicates, ownership checks passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "FAIL: " << error.what() << '\n';
  return 1;
}
