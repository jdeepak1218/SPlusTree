#include "../benchmarks/baselines/eytzinger.hpp"
#include "../benchmarks/baselines/lower_bound.hpp"
#include "stree.hpp"
#include "../benchmarks/workloads.hpp"

#include <array>
#include <atomic>
#include <iostream>
#include <thread>

namespace {
std::size_t datasets = 0, cases = 0, results = 0;

template<std::size_t B>
void verify(const workloads::Keys& values, const workloads::Keys& queries,
            const std::string& label)
{
  const sst::STree<B> tree(values);
  const sst::Eytzinger eytzinger(values);
  ++datasets;
  std::size_t previous = 0;
  std::uint32_t previous_query = 0;
  for (const auto q : queries) {
    const auto expected = sst::reference_lower_bound(values, q);
    const std::array actual{
        sst::binary_lower_bound(values, q), eytzinger.lower_bound(q),
        eytzinger.lower_bound_prefetch(q), eytzinger.lower_bound_optimized(q),
        eytzinger.lower_bound_optimized_no_prefetch(q), tree.lower_bound(q),
        tree.lower_bound_count(q), tree.lower_bound_mask(q),
        tree.lower_bound_popcount(q), tree.lower_bound_packed(q)};
    for (std::size_t method = 0; method < actual.size(); ++method) {
      const auto index = actual[method];
      if (index != expected || index > values.size()
          || (index < values.size() && values[index] < q)
          || (index > 0 && values[index - 1] >= q)
          || (q >= previous_query && index < previous)) {
        throw std::runtime_error(label + " B=" + std::to_string(B)
            + " n=" + std::to_string(values.size()) + " q=" + std::to_string(q)
            + " method=" + std::to_string(method) + " expected="
            + std::to_string(expected) + " actual=" + std::to_string(index));
      }
      ++results;
    }
    const auto value = expected == values.size() ? workloads::maximum : values[expected];
    if (eytzinger.search_value_optimized(q) != value) throw std::runtime_error("Value mismatch: " + label);
    ++results;
    ++cases;
    previous = expected;
    previous_query = q;
  }
}

template<std::size_t B>
void width_suite()
{
  // Every leaf and height transition through three levels, including partial nodes.
  for (const auto base : {std::size_t{0}, B, B * (B + 1), B * (B + 1) * (B + 1)}) {
    for (int delta = -2; delta <= 2; ++delta) {
      if (delta < 0 && base < static_cast<std::size_t>(-delta)) continue;
      const auto n = delta < 0 ? base - static_cast<std::size_t>(-delta)
                               : base + static_cast<std::size_t>(delta);
      auto keys = workloads::make_keys(n, "uniform", static_cast<std::uint32_t>(B * 31 + n));
      workloads::Keys queries{0, 1, 0x7fffffffU, 0x80000000U, workloads::maximum - 1, workloads::maximum};
      for (auto& key : keys) key = key / 7 * 7;
      for (const auto key : keys) {
        queries.push_back(key);
        if (key > 0) queries.push_back(key - 1);
        if (key < workloads::maximum) queries.push_back(key + 1);
      }
      std::sort(queries.begin(), queries.end());
      verify<B>(keys, queries, "width boundary");
      // Duplicate runs cross separators and the last real key equals the padding value.
      for (std::size_t i = 0; i < keys.size(); ++i) {
        keys[i] = i < keys.size() / 3 ? 0U
            : (i < keys.size() * 2 / 3 ? 0x80000000U : workloads::maximum);
      }
      verify<B>(keys, {0, 1, 0x7fffffffU, 0x80000000U, 0x80000001U,
                       workloads::maximum - 1, workloads::maximum}, "width duplicate boundary");
    }
  }
  if constexpr (B < 16) width_suite<B + 1>();
}

void concurrent_reads()
{
  auto source = workloads::make_keys(100003, "clusters", 771);
  sst::STree<16> original(source);
  sst::Eytzinger original_eytz(source);
  auto tree = original;
  auto eytz = original_eytz;
  original = sst::STree<16>({});
  original_eytz = sst::Eytzinger({});
  const auto queries = workloads::make_queries(source, 32768, "mixed", 991);
  std::vector<std::size_t> expected;
  for (auto q : queries) expected.push_back(sst::reference_lower_bound(source, q));
  source.clear();
  source.shrink_to_fit();
  std::atomic<bool> failed{false};
  std::array<std::thread, 4> workers;
  for (auto& worker : workers) worker = std::thread([&] {
    for (std::size_t i = 0; i < queries.size(); ++i) {
      const auto q = queries[i];
      if (tree.lower_bound(q) != expected[i] || tree.lower_bound_count(q) != expected[i]
          || tree.lower_bound_mask(q) != expected[i] || tree.lower_bound_popcount(q) != expected[i]
          || tree.lower_bound_packed(q) != expected[i] || eytz.lower_bound(q) != expected[i]
          || eytz.lower_bound_prefetch(q) != expected[i] || eytz.lower_bound_optimized(q) != expected[i]
          || eytz.lower_bound_optimized_no_prefetch(q) != expected[i]) failed.store(true);
    }
  });
  for (auto& worker : workers) worker.join();
  if (failed.load()) throw std::runtime_error("Concurrent/copy ownership mismatch");
  results += 4 * queries.size() * 9;
}
} // namespace

int main()
try {
  width_suite<1>();
  for (std::uint32_t seed = 1; seed <= 20; ++seed) {
    for (const auto& distribution : {"uniform", "clusters", "dense", "equal"}) {
      const auto keys = workloads::make_keys(10003 + seed * 101, distribution, seed);
      for (const auto& pattern : {"hits", "mixed", "sorted", "reverse", "hot", "boundaries"}) {
        verify<16>(keys, workloads::make_queries(keys, 4096, pattern, seed * 777),
                   std::string(distribution) + "/" + pattern + " seed=" + std::to_string(seed));
      }
    }
  }
  // Known hit/miss and ordering properties test the workload generator itself.
  const auto dense = workloads::make_keys(1000, "dense", 1);
  for (const auto& pattern : {"hits", "misses"}) {
    const auto queries = workloads::make_queries(dense, 4096, pattern, 1);
    for (auto q : queries) {
      const bool hit = std::binary_search(dense.begin(), dense.end(), q);
      if (hit != (std::string(pattern) == "hits")) throw std::runtime_error("Generator hit/miss mismatch");
    }
    verify<16>(dense, queries, pattern);
  }
  concurrent_reads();
  std::cout << "PASS datasets=" << datasets << " differential_queries=" << cases
            << " checked_results_including_concurrent=" << results << '\n';
} catch (const std::exception& error) {
  std::cerr << "FAIL: " << error.what() << '\n';
  return 1;
}
