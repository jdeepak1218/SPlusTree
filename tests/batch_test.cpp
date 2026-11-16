#include "stree.hpp"
#include "static_search_tree.hpp"
#include "../benchmarks/workloads.hpp"

#include <atomic>
#include <iostream>
#include <thread>

namespace {
std::size_t datasets = 0, checked = 0;
constexpr auto guard = std::numeric_limits<std::size_t>::max();

template<std::size_t P, std::size_t B>
void exercise(const sst::STree<B>& tree, const workloads::Keys& keys,
              const workloads::Keys& queries, const std::vector<std::size_t>& expected)
{
  std::vector<std::size_t> output(queries.size() + 2, guard);
  tree.template lower_bound_batch<P>(queries, std::span<std::size_t>(output).subspan(1, queries.size()));
  if (output.front() != guard || output.back() != guard) throw std::runtime_error("Output guard overwritten");
  const auto check = [&](std::uint32_t query, std::size_t actual, std::size_t want) {
    if (actual != want || actual != tree.lower_bound_packed(query) || actual > keys.size()
        || (actual < keys.size() && keys[actual] < query)
        || (actual > 0 && keys[actual - 1] >= query)) {
      throw std::runtime_error("Batch mismatch B=" + std::to_string(B) + " P=" + std::to_string(P)
          + " n=" + std::to_string(keys.size()) + " q=" + std::to_string(query)
          + " expected=" + std::to_string(want) + " actual=" + std::to_string(actual));
    }
    ++checked;
  };
  for (std::size_t i = 0; i < queries.size(); ++i) check(queries[i], output[i + 1], expected[i]);
  std::array<std::uint32_t, P> fixed{};
  for (std::size_t i = 0; i < P; ++i) fixed[i] = queries.empty() ? workloads::maximum : queries[i % queries.size()];
  const auto answers = tree.lower_bound_batch(fixed);
  for (std::size_t i = 0; i < P; ++i) check(fixed[i], answers[i], sst::reference_lower_bound(keys, fixed[i]));
  bool rejected = false;
  const auto before_rejection = output;
  try { tree.template lower_bound_batch<P>(queries, std::span<std::size_t>(output).first(queries.size() + 1)); }
  catch (const std::invalid_argument&) { rejected = true; }
  if (!rejected || output != before_rejection) {
    throw std::runtime_error("Mismatched output span was not rejected before writing");
  }
}

template<std::size_t B>
void verify(const workloads::Keys& keys, const workloads::Keys& queries)
{
  const sst::STree<B> tree(keys);
  const auto unchanged = queries;
  std::vector<std::size_t> expected;
  for (auto query : queries) expected.push_back(sst::reference_lower_bound(keys, query));
  exercise<1>(tree, keys, queries, expected);
  exercise<2>(tree, keys, queries, expected);
  exercise<4>(tree, keys, queries, expected);
  exercise<8>(tree, keys, queries, expected);
  exercise<16>(tree, keys, queries, expected);
  exercise<32>(tree, keys, queries, expected);
  exercise<64>(tree, keys, queries, expected);
  exercise<128>(tree, keys, queries, expected);
  // A non-power-of-two batch also exercises generic loop and tail arithmetic.
  exercise<3>(tree, keys, queries, expected);
  if (queries != unchanged) throw std::runtime_error("Query input modified");
  ++datasets;
}

template<std::size_t B>
void widths()
{
  for (auto base : {std::size_t{0}, B, B * (B + 1), B * (B + 1) * (B + 1)}) {
    for (int delta = -1; delta <= 1; ++delta) {
      if (base == 0 && delta < 0) continue;
      const auto n = delta < 0 ? base - 1 : base + static_cast<std::size_t>(delta);
      workloads::Keys keys(n), queries{0, 1, 0x7fffffffU, 0x80000000U, workloads::maximum - 1, workloads::maximum};
      for (std::size_t i = 0; i < n; ++i) {
        keys[i] = i < n / 3 ? 0U : (i < n * 2 / 3 ? 0x80000000U : workloads::maximum);
      }
      for (std::size_t i = 0; i < 257; ++i) queries.push_back(static_cast<std::uint32_t>(i * 163391));
      // All node widths; full batch-size matrix for the two representative widths.
      if constexpr (B == 2 || B == 16) verify<B>(keys, queries);
      else {
        const sst::STree<B> tree(keys);
        std::vector<std::size_t> expected;
        for (auto q : queries) expected.push_back(sst::reference_lower_bound(keys, q));
        exercise<3>(tree, keys, queries, expected);
        ++datasets;
      }
    }
  }
  if constexpr (B < 16) widths<B + 1>();
}

void concurrent_ownership()
{
  auto keys = workloads::make_keys(100003, "clusters", 19);
  const auto queries = workloads::make_queries(keys, 8195, "mixed", 61);
  std::vector<std::size_t> expected;
  for (auto q : queries) expected.push_back(sst::reference_lower_bound(keys, q));
  sst::STree<> original(keys);
  auto copy = original;
  auto tree = std::move(copy);
  std::vector<std::size_t> moved_output(queries.size(), guard);
  copy.lower_bound_batch<16>(queries, moved_output);
  if (std::any_of(moved_output.begin(), moved_output.end(), [](auto index) { return index != 0; })) {
    throw std::runtime_error("Moved-from tree batch was not empty");
  }
  copy = tree;
  copy.lower_bound_batch<16>(queries, moved_output);
  if (moved_output != expected) throw std::runtime_error("Copy assignment lost batch data");
  checked += 2 * queries.size();
  original = sst::STree<>({});
  keys.clear();
  keys.shrink_to_fit();
  std::atomic<bool> failed{false};
  std::array<std::thread, 4> threads;
  for (auto& thread : threads) thread = std::thread([&] {
    std::vector<std::size_t> output(queries.size());
    tree.lower_bound_batch<16>(queries, output);
    if (output != expected) failed.store(true);
  });
  for (auto& thread : threads) thread.join();
  if (failed.load()) throw std::runtime_error("Concurrent copied/moved batch tree failed");
  checked += threads.size() * queries.size();
}
} // namespace

int main(int argc, char** argv)
try {
  if (argc > 2) throw std::runtime_error("Usage: batch_test [16mer-u32le-file]");
  if (argc == 2) {
    auto keys = workloads::read_keys(argv[1]);
    std::sort(keys.begin(), keys.end());
    for (const auto& pattern : {"mixed", "hot", "sorted", "boundaries"}) {
      verify<16>(keys, workloads::make_queries(keys, 131075, pattern, 778));
    }
    std::cout << "Real keys=" << keys.size() << ' ';
  } else {
    widths<1>();
    for (const auto n : {0U, 1U, 2U, 3U, 7U, 8U, 15U, 16U, 17U, 31U, 32U, 33U,
                         63U, 64U, 65U, 127U, 128U, 129U, 255U, 256U, 257U, 1027U}) {
      auto queries = workloads::make_queries({0, 5, workloads::maximum}, n, "mixed", n + 1);
      // Test unaligned subspans, not only whole-vector inputs.
      queries.insert(queries.begin(), 123);
      const workloads::Keys slice(queries.begin() + 1, queries.end());
      verify<16>({}, slice);
      verify<16>({0}, slice);
      verify<16>({workloads::maximum}, slice);
      verify<16>({2, 5, 5, 9, 14}, slice);
      const sst::STree<> tree({2, 5, 5, 9, 14});
      std::vector<std::size_t> output(n);
      tree.lower_bound_batch<16>(std::span<const std::uint32_t>(queries).subspan(1), output);
      for (std::size_t i = 0; i < n; ++i) {
        if (output[i] != tree.lower_bound_packed(queries[i + 1])) throw std::runtime_error("Input subspan mismatch");
        ++checked;
      }
    }
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
      for (const auto& distribution : {"uniform", "clusters", "dense", "equal"}) {
        const auto keys = workloads::make_keys(10003 + seed * 101, distribution, seed);
        for (const auto& pattern : {"mixed", "hot", "sorted", "reverse", "boundaries"}) {
          verify<16>(keys, workloads::make_queries(keys, 1027, pattern, seed));
        }
      }
    }
    for (const auto n : {std::size_t{1048579}, std::size_t{8388731}}) {
      const auto keys = workloads::make_keys(n, "uniform", 981);
      verify<16>(keys, workloads::make_queries(keys, 8195, "mixed", 182));
    }
    concurrent_ownership();
  }
  std::cout << "PASS batch datasets=" << datasets << " checked_results=" << checked << '\n';
} catch (const std::exception& error) {
  std::cerr << "FAIL: " << error.what() << '\n';
  return 1;
}
