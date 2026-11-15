#include "eytzinger.hpp"
#include "static_search_tree.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <utility>

namespace {
constexpr auto max_key = std::numeric_limits<std::uint32_t>::max();
std::size_t datasets = 0;
std::size_t lookups = 0;
std::mt19937 generator(20261115);

void require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void verify(const std::vector<std::uint32_t>& values, std::size_t samples = 100)
{
  const sst::Eytzinger tree(values);
  ++datasets;
  require(tree.size() == values.size(), "Wrong tree size");
  const auto check = [&](std::uint32_t query) {
    const auto expected = sst::reference_lower_bound(values, query);
    if (tree.lower_bound(query) != expected
        || tree.lower_bound_prefetch(query) != expected
        || tree.lower_bound_optimized(query) != expected
        || tree.lower_bound_optimized_no_prefetch(query) != expected
        || tree.search_value_optimized(query) != (expected == values.size() ? max_key : values[expected])
        || sst::binary_lower_bound(values, query) != expected) {
      throw std::runtime_error("Mismatch: n=" + std::to_string(values.size())
                               + " query=" + std::to_string(query));
    }
    ++lookups;
  };
  for (const auto q : {0U, 1U, 0x7fffffffU, 0x80000000U, max_key - 1, max_key}) {
    check(q);
  }
  for (std::size_t i = 0; i < samples; ++i) {
    check(static_cast<std::uint32_t>(generator()));
  }
  // Probe keys and their neighbors: random queries alone often miss duplicates.
  const auto stride = std::max<std::size_t>(1, values.size() / samples);
  for (std::size_t i = 0; i < values.size(); i += stride) {
    const auto key = values[i];
    check(key);
    if (key > 0) check(key - 1);
    if (key < max_key) check(key + 1);
  }
  if (!values.empty()) {
    check(values.back());
  }
}

// Enumerate every nondecreasing array of length <= 10 over this alphabet.
void exhaustive(std::vector<std::uint32_t>& values, std::size_t first)
{
  verify(values, 1);
  if (values.size() == 10) return;
  constexpr std::array<std::uint32_t, 3> alphabet{0, 1, max_key};
  for (auto i = first; i < alphabet.size(); ++i) {
    values.push_back(alphabet[i]);
    exhaustive(values, i);
    values.pop_back();
  }
}
} // namespace

int main()
try {
  std::vector<std::uint32_t> values;
  exhaustive(values, 0);

  // Exhaust all small tree shapes, especially lookahead and partial-block edges.
  for (std::size_t n = 0; n <= 513; ++n) {
    values.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      values[i] = static_cast<std::uint32_t>(2 * i);
    }
    verify(values, n + 1);
  }

  // Complete-tree and partial-last-level boundaries, including even lengths.
  for (unsigned power = 0; power <= 17; ++power) {
    const std::size_t base = std::size_t{1} << power;
    for (int delta = -2; delta <= 2; ++delta) {
      if (delta < 0 && base < static_cast<std::size_t>(-delta)) continue;
      const auto n = delta < 0 ? base - static_cast<std::size_t>(-delta)
                               : base + static_cast<std::size_t>(delta);
      values.resize(n);
      for (std::size_t i = 0; i < n; ++i) {
        values[i] = static_cast<std::uint32_t>(i / 7);
      }
      verify(values);
    }
  }

  for (std::size_t trial = 0; trial < 500; ++trial) {
    values.resize(generator() % 4097);
    for (auto& value : values) {
      switch (trial % 5) {
        case 0: value = static_cast<std::uint32_t>(generator()); break;
        case 1: value = static_cast<std::uint32_t>(generator() % 8); break;
        case 2: value = max_key; break;
        case 3: value = 0; break;
        default: value = max_key - static_cast<std::uint32_t>(generator() % 8); break;
      }
    }
    std::sort(values.begin(), values.end());
    verify(values);
  }

  values.resize(1'000'003);
  for (auto& value : values) value = static_cast<std::uint32_t>(generator());
  std::sort(values.begin(), values.end());
  verify(values, 10'000);

  values.resize(8'388'731);
  for (std::size_t i = 0; i < values.size(); ++i) {
    values[i] = static_cast<std::uint32_t>(i / 4);
  }
  values.back() = max_key;
  verify(values, 20'000);

  for (const auto& bad : std::vector<std::vector<std::uint32_t>>{
           {2, 1}, {0, max_key, 1}, {3, 2, 1}, {1, 1, 0}}) {
    bool rejected = false;
    try {
      const sst::Eytzinger tree(bad);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "Unsorted input was accepted");
  }

  // The source can be changed or destroyed after construction.
  values = {2, 5, 5, 9};
  const sst::Eytzinger owned(values);
  values.assign(100, 0);
  require(owned.lower_bound(5) == 1 && owned.lower_bound(10) == 4,
          "Tree does not own its data");
  require(owned.lower_bound_prefetch(5) == 1 && owned.lower_bound_prefetch(10) == 4,
          "Prefetch search lost owned data");
  auto copy = owned;
  auto moved = std::move(copy);
  require(moved.lower_bound(5) == 1, "Move lost tree data");
  require(moved.lower_bound_prefetch(5) == 1, "Prefetch search lost moved data");
  require(moved.lower_bound_optimized(5) == 1 && moved.search_value_optimized(5) == 5,
          "optimized search lost moved data");
  copy = owned;
  require(copy.lower_bound(6) == 3, "Copy assignment lost tree data");

  sst::detail::CacheAlignedAllocator<std::uint32_t> allocator;
  for (const auto count : {1U, 16U, 17U, 1025U}) {
    auto* allocation = allocator.allocate(count);
    require(reinterpret_cast<std::uintptr_t>(allocation) % 64 == 0,
            "Key allocator is not cache-line aligned");
    allocator.deallocate(allocation, count);
  }

  std::cout << "PASS: " << datasets << " datasets, " << lookups
            << " differential queries; invalid-input and ownership checks passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "FAIL: " << error.what() << '\n';
  return 1;
}
