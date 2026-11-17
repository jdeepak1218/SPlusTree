#include "stree.hpp"

#include <atomic>
#include <iostream>
#include <random>
#include <thread>

namespace {
std::size_t datasets = 0, checks = 0;
constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
constexpr auto guard = std::numeric_limits<std::size_t>::max();

void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
  ++checks;
}

void verify(const std::vector<std::uint32_t>& keys, const std::vector<std::uint32_t>& queries)
{
  const sst::StaticSearchTree tree(keys);
  require(tree.size() == keys.size(), "Size mismatch");
  require(tree.empty() == keys.empty(), "Empty mismatch");
  for (std::size_t i = 0; i < keys.size(); ++i) {
    require(tree.key_at(i) == keys[i], "Owned key accessor mismatch");
  }
  for (auto index : {keys.size(), guard}) {
    bool rejected = false;
    try { (void)tree.key_at(index); }
    catch (const std::out_of_range&) { rejected = true; }
    require(rejected, "Invalid key index accepted");
  }
  const auto allocated = tree.lower_bound_batch(queries);
  std::vector<std::size_t> guarded(queries.size() + 2, guard);
  tree.lower_bound_batch(queries, std::span<std::size_t>(guarded).subspan(1, queries.size()));
  require(guarded.front() == guard && guarded.back() == guard, "Output canary overwritten");
  require(allocated.size() == queries.size(), "Batch output length mismatch");
  for (std::size_t i = 0; i < queries.size(); ++i) {
    const auto expected = static_cast<std::size_t>(std::lower_bound(keys.begin(), keys.end(), queries[i]) - keys.begin());
    const auto actual = tree.lower_bound(queries[i]);
    require(actual == expected, "Individual lookup mismatch");
    require(tree.contains(queries[i]) == (expected < keys.size() && keys[expected] == queries[i]),
            "Membership mismatch");
    require(allocated[i] == expected && guarded[i + 1] == expected, "Batch lookup mismatch");
    require(actual <= keys.size() && (actual == keys.size() || keys[actual] >= queries[i])
        && (actual == 0 || keys[actual - 1] < queries[i]), "Lower-bound bracket mismatch");
  }
  for (auto length : {queries.size() + 1, queries.empty() ? std::size_t{1} : queries.size() - 1}) {
    const auto before = guarded;
    bool rejected = false;
    try { tree.lower_bound_batch(queries, std::span<std::size_t>(guarded).first(length)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && guarded == before, "Invalid length wrote output or was accepted");
  }
  ++datasets;
}

void construction_and_ownership()
{
  const sst::StaticSearchTree empty;
  require(empty.empty() && empty.lower_bound(maximum) == 0, "Default tree not empty");
  require(empty.lower_bound_batch(std::span<const std::uint32_t>{}).empty(), "Empty batch allocated answers");
  std::span<std::size_t> no_output;
  empty.lower_bound_batch({}, no_output);
  const sst::StaticSearchTree empty_list({});
  require(empty_list.empty(), "Empty initializer list failed");
  const std::array<std::uint32_t, 5> array{2, 5, 5, 9, 14};
  const std::uint32_t raw[]{2, 5, 5, 9, 14};
  std::vector<std::uint32_t> vector(array.begin(), array.end());
  const sst::StaticSearchTree from_array(array), from_raw(raw), from_vector(vector);
  const sst::StaticSearchTree from_view{std::span<const std::uint32_t>(array)};
  const sst::StaticSearchTree from_mutable_view{std::span<std::uint32_t>(vector)};
  const sst::StaticSearchTree from_list({2, 5, 5, 9, 14});
  const sst::StaticSearchTree from_temporary(std::vector<std::uint32_t>{2, 5, 5, 9, 14});
  static_assert(noexcept(from_array.lower_bound(6)));
  for (const auto* tree : {&from_array, &from_raw, &from_vector, &from_view, &from_mutable_view, &from_list, &from_temporary}) {
    require(tree->lower_bound(5) == 1 && tree->lower_bound(20) == 5, "Construction type mismatch");
    require(tree->storage_bytes() == from_array.storage_bytes(), "Construction changed index storage");
  }
  vector.assign(5, 99);
  vector.clear();
  vector.shrink_to_fit();
  require(from_vector.lower_bound(6) == 3 && from_mutable_view.lower_bound(5) == 1, "Source lifetime dependency");
  require(from_vector.key_at(3) == 9 && from_vector.contains(5) && !from_vector.contains(6),
          "Owned key lookup depends on source");
  auto copy = from_list;
  auto moved = std::move(copy);
  require(moved.lower_bound(6) == 3, "Move lost keys");
  copy = from_array;
  require(copy.lower_bound(5) == 1, "Copy assignment lost keys");
  moved = std::move(copy);
  require(moved.lower_bound(14) == 4 && moved.key_at(4) == 14 && moved.contains(5),
          "Move assignment lost keys");
  const std::array<std::uint32_t, 4> queries{6, 5, 20, 0};
  require(moved.lower_bound_batch(queries) == std::vector<std::size_t>{3, 1, 5, 0}, "Documented example mismatch");
  bool rejected = false;
  try { const sst::StaticSearchTree bad({3, 1, 2}); }
  catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "Unsorted initializer list accepted");
  rejected = false;
  try { const sst::StaticSearchTree bad(std::vector<std::uint32_t>{2, 1}); }
  catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "Unsorted vector accepted");

  const std::array<std::uint32_t, 6> storage{99, 0, 5, 5, maximum, 1};
  const sst::StaticSearchTree subspan(std::span<const std::uint32_t>(storage).subspan(1, 4));
  require(subspan.size() == 4 && subspan.lower_bound(maximum) == 3, "Key subspan mismatch");
  const sst::STree<> legacy_span{std::span<const std::uint32_t>(array)};
  require(legacy_span.lower_bound(6) == 3, "Legacy span constructor mismatch");

  std::atomic<bool> failed{false};
  std::array<std::thread, 4> threads;
  for (auto& thread : threads) thread = std::thread([&] {
    for (int i = 0; i < 100; ++i) {
      const auto result = moved.lower_bound_batch(queries);
      if (result != std::vector<std::size_t>{3, 1, 5, 0} || moved.lower_bound(5) != 1) failed.store(true);
    }
  });
  for (auto& thread : threads) thread.join();
  require(!failed.load(), "Concurrent facade lookup mismatch");
}
} // namespace

int main()
try {
  construction_and_ownership();
  std::mt19937 rng(20261117);
  for (const auto n : {0U, 1U, 2U, 15U, 16U, 17U, 271U, 272U, 273U, 4623U, 4624U, 4625U, 100003U, 1048579U}) {
    for (unsigned distribution = 0; distribution < 4; ++distribution) {
      std::vector<std::uint32_t> keys(n);
      for (auto& key : keys) {
        key = distribution == 0 ? static_cast<std::uint32_t>(rng())
            : distribution == 1 ? static_cast<std::uint32_t>(rng() % 7)
            : distribution == 2 ? maximum : 0x80000000U;
      }
      std::sort(keys.begin(), keys.end());
      std::vector<std::uint32_t> queries{0, 1, 0x7fffffffU, 0x80000000U, maximum - 1, maximum};
      for (unsigned i = 0; i < 4097; ++i) queries.push_back(i % 2 == 0 && !keys.empty() ? keys[rng() % keys.size()] : static_cast<std::uint32_t>(rng()));
      verify(keys, queries);
    }
  }
  for (const auto count : {0U, 1U, 2U, 3U, 15U, 16U, 17U, 31U, 32U, 33U, 63U, 64U, 65U, 127U, 128U, 129U}) {
    std::vector<std::uint32_t> queries(count);
    for (auto& query : queries) query = static_cast<std::uint32_t>(rng());
    verify({}, queries);
    verify({0, 5, 5, 0x80000000U, maximum}, queries);
  }
  std::cout << "PASS simple API datasets=" << datasets << " checks=" << checks << '\n';
} catch (const std::exception& error) {
  std::cerr << "FAIL: " << error.what() << '\n';
  return 1;
}
