#include "eytzinger.hpp"
#include "static_search_tree.hpp"
#include "stree.hpp"
#include "workloads.hpp"

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t trial_count = 8, query_count = 131072, method_count = 8;
constexpr std::array names{"std", "binary", "eytzinger", "stree_early", "stree_count",
                           "stree_mask", "stree_popcount", "stree_packed"};

void run(const workloads::Keys& values, const std::string& dataset, const std::string& pattern)
{
  const auto queries = workloads::make_queries(values, query_count, pattern, 20261116);
  std::size_t unique_keys = 0;
  for (std::size_t i = 0; i < values.size(); ++i) {
    unique_keys += i == 0 || values[i] != values[i - 1];
  }
  const auto begin_build = Clock::now();
  const sst::Eytzinger eytz(values);
  const auto end_eytz = Clock::now();
  const sst::STree<16> tree(values);
  const auto end_tree = Clock::now();
  const double eytz_ms = std::chrono::duration<double, std::milli>(end_eytz - begin_build).count();
  const double tree_ms = std::chrono::duration<double, std::milli>(end_tree - end_eytz).count();
  std::vector<std::size_t> expected, output(queries.size());
  std::size_t hits = 0;
  std::uint64_t checksum = 0;
  expected.reserve(queries.size());
  for (const auto q : queries) {
    const auto index = sst::reference_lower_bound(values, q);
    expected.push_back(index);
    hits += index < values.size() && values[index] == q;
    checksum = checksum * 0x9e3779b185ebca87ULL + index;
  }
  std::array<std::array<double, trial_count>, method_count> samples{};
  auto measure = [&](auto lookup, std::size_t method, int trial) {
    const auto start = Clock::now();
    for (std::size_t i = 0; i < queries.size(); ++i) output[i] = lookup(queries[i]);
    const auto finish = Clock::now();
    for (std::size_t i = 0; i < output.size(); ++i) {
      const auto index = output[i];
      if (index != expected[i] || index > values.size()
          || (index < values.size() && values[index] < queries[i])
          || (index > 0 && values[index - 1] >= queries[i])) {
        throw std::runtime_error(dataset + "/" + pattern + " method=" + names[method]
            + " query=" + std::to_string(queries[i]) + " expected=" + std::to_string(expected[i])
            + " actual=" + std::to_string(index));
      }
    }
    if (trial >= 0) samples[method][static_cast<std::size_t>(trial)] =
        std::chrono::duration<double, std::nano>(finish - start).count() / static_cast<double>(queries.size());
  };
  const auto dispatch = [&](std::size_t method, int trial) {
    // Dispatch outside the timed query loop; each lambda has a concrete target.
    switch (method) {
      case 0: measure([&](auto q) { return sst::reference_lower_bound(values, q); }, method, trial); break;
      case 1: measure([&](auto q) { return sst::binary_lower_bound(values, q); }, method, trial); break;
      case 2: measure([&](auto q) { return eytz.lower_bound_optimized(q); }, method, trial); break;
      case 3: measure([&](auto q) { return tree.lower_bound(q); }, method, trial); break;
      case 4: measure([&](auto q) { return tree.lower_bound_count(q); }, method, trial); break;
      case 5: measure([&](auto q) { return tree.lower_bound_mask(q); }, method, trial); break;
      case 6: measure([&](auto q) { return tree.lower_bound_popcount(q); }, method, trial); break;
      case 7: measure([&](auto q) { return tree.lower_bound_packed(q); }, method, trial); break;
      default: throw std::runtime_error("Invalid benchmark method");
    }
  };
  std::array<std::size_t, method_count> order{};
  std::iota(order.begin(), order.end(), 0);
  std::mt19937 rng(1147);
  std::shuffle(order.begin(), order.end(), rng);
  for (auto method : order) dispatch(method, -1);
  // Every method occupies every run position exactly once.
  for (std::size_t trial = 0; trial < trial_count; ++trial) {
    for (std::size_t j = 0; j < method_count; ++j) dispatch(order[(j + trial) % method_count], static_cast<int>(trial));
  }
  for (std::size_t method = 0; method < method_count; ++method) {
    auto sorted = samples[method];
    std::sort(sorted.begin(), sorted.end());
    const auto storage = method < 2 ? values.size() * sizeof(std::uint32_t)
                                   : (method == 2 ? eytz.storage_bytes() : tree.storage_bytes());
    std::cout << dataset << ',' << pattern << ',' << values.size() << ',' << unique_keys << ',' << queries.size()
              << ',' << hits << ',' << names[method] << ',' << trial_count << ','
              << (sorted[3] + sorted[4]) / 2 << ',' << sorted.front() << ',' << sorted.back()
              << ',' << checksum << ',' << storage << ','
              << (method < 2 ? 0 : (method == 2 ? eytz_ms : tree_ms));
    for (const auto sample : samples[method]) std::cout << ',' << sample;
    std::cout << '\n';
  }
  std::cout.flush();
  std::cerr << "PASS " << dataset << '/' << pattern << " n=" << values.size()
            << " hits=" << hits << '/' << queries.size() << '\n';
}
} // namespace

int main(int argc, char** argv)
try {
  if (argc > 2) throw std::runtime_error("Usage: workload_benchmark [16mer-u32le-file]");
  std::cout << std::fixed << std::setprecision(3)
            << "dataset,pattern,n,unique_keys,queries,hits,method,trials,median_ns,min_ns,max_ns,checksum,storage_bytes,build_ms";
  for (std::size_t i = 0; i < trial_count; ++i) std::cout << ",trial_" << i << "_ns";
  std::cout << '\n';
  if (argc == 2) {
    const auto genome = workloads::read_keys(argv[1]);
    for (const auto n : {std::size_t{1048576}, std::size_t{8388608}, genome.size()}) {
      if (n > genome.size()) continue;
      // Prefixes are genuine consecutive windows, never duplicated to increase size.
      workloads::Keys values(genome.begin(), genome.begin() + static_cast<std::ptrdiff_t>(n));
      std::sort(values.begin(), values.end());
      for (const auto& pattern : {"mixed", "hot", "sorted"}) run(values, "chr22_16mers", pattern);
    }
  } else {
    for (const auto n : {std::size_t{1024}, std::size_t{65536}}) {
      const auto values = workloads::make_keys(n, "uniform", 41);
      for (const auto& pattern : {"mixed", "hot"}) run(values, "uniform", pattern);
    }
    for (const auto& distribution : {"dense", "clusters", "uniform", "equal"}) {
      const auto values = workloads::make_keys(1048576, distribution, 42);
      for (const auto& pattern : {"mixed", "hot", "sorted", "reverse", "boundaries"}) run(values, distribution, pattern);
      if (std::string(distribution) == "dense") {
        run(values, distribution, "hits");
        run(values, distribution, "misses");
      }
    }
    for (const auto& distribution : {"uniform", "clusters"}) {
      for (const auto n : {std::size_t{8388731}, std::size_t{33554555}}) {
        const auto values = workloads::make_keys(n, distribution, 43);
        for (const auto& pattern : {"mixed", "hot"}) run(values, distribution, pattern);
      }
    }
  }
} catch (const std::exception& error) {
  std::cerr << "FAIL: " << error.what() << '\n';
  return 1;
}
