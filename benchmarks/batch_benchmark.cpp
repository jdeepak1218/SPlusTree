#include "stree.hpp"
#include "static_search_tree.hpp"
#include "workloads.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t query_count = 131075, method_count = 11, trials = 11;
constexpr std::array names{"std", "count", "packed", "batch_1", "batch_2", "batch_4",
                           "batch_8", "batch_16", "batch_32", "batch_64", "batch_128"};
constexpr std::array<std::size_t, method_count> batch_sizes{0, 0, 0, 1, 2, 4, 8, 16, 32, 64, 128};

void run(const workloads::Keys& keys, const std::string& dataset, const std::string& pattern)
{
  const auto queries = workloads::make_queries(keys, query_count, pattern, 20261116);
  const auto build_start = Clock::now();
  const sst::STree<> tree(keys);
  const double build_ms = std::chrono::duration<double, std::milli>(Clock::now() - build_start).count();
  std::vector<std::size_t> expected, output(queries.size());
  std::uint64_t checksum = 0;
  std::size_t hits = 0;
  for (auto q : queries) {
    const auto index = sst::reference_lower_bound(keys, q);
    expected.push_back(index);
    hits += index < keys.size() && keys[index] == q;
    checksum = checksum * 0x9e3779b185ebca87ULL + index;
  }
  std::array<std::array<double, trials>, method_count> samples{};
  const auto measure = [&](auto fill_output, std::size_t method, int trial) {
    // A skipped output must fail validation instead of reusing a previous answer.
    std::fill(output.begin(), output.end(), std::numeric_limits<std::size_t>::max());
    const auto start = Clock::now();
    fill_output();
    const auto finish = Clock::now();
    for (std::size_t i = 0; i < output.size(); ++i) {
      const auto index = output[i];
      if (index != expected[i] || index > keys.size()
          || (index < keys.size() && keys[index] < queries[i])
          || (index > 0 && keys[index - 1] >= queries[i])) {
        throw std::runtime_error(dataset + "/" + pattern + " method=" + names[method]
            + " query=" + std::to_string(queries[i]) + " expected=" + std::to_string(expected[i])
            + " actual=" + std::to_string(index));
      }
    }
    if (trial >= 0) samples[method][static_cast<std::size_t>(trial)] =
        std::chrono::duration<double, std::nano>(finish - start).count() / static_cast<double>(queries.size());
  };
  const auto dispatch = [&](std::size_t method, int trial) {
    switch (method) {
      case 0: measure([&] {
        for (std::size_t i = 0; i < queries.size(); ++i) output[i] = sst::reference_lower_bound(keys, queries[i]);
      }, method, trial); break;
      case 1: measure([&] {
        for (std::size_t i = 0; i < queries.size(); ++i) output[i] = tree.lower_bound_count(queries[i]);
      }, method, trial); break;
      case 2: measure([&] {
        for (std::size_t i = 0; i < queries.size(); ++i) output[i] = tree.lower_bound_packed(queries[i]);
      }, method, trial); break;
      case 3: measure([&] { tree.lower_bound_batch<1>(queries, output); }, method, trial); break;
      case 4: measure([&] { tree.lower_bound_batch<2>(queries, output); }, method, trial); break;
      case 5: measure([&] { tree.lower_bound_batch<4>(queries, output); }, method, trial); break;
      case 6: measure([&] { tree.lower_bound_batch<8>(queries, output); }, method, trial); break;
      case 7: measure([&] { tree.lower_bound_batch<16>(queries, output); }, method, trial); break;
      case 8: measure([&] { tree.lower_bound_batch<32>(queries, output); }, method, trial); break;
      case 9: measure([&] { tree.lower_bound_batch<64>(queries, output); }, method, trial); break;
      case 10: measure([&] { tree.lower_bound_batch<128>(queries, output); }, method, trial); break;
      default: throw std::runtime_error("Invalid benchmark method");
    }
  };
  std::array<std::size_t, method_count> order{};
  std::iota(order.begin(), order.end(), 0);
  std::mt19937 rng(1147);
  std::shuffle(order.begin(), order.end(), rng);
  for (auto method : order) dispatch(method, -1);
  // Eleven trials put every method in every run position once.
  for (std::size_t trial = 0; trial < trials; ++trial) {
    for (std::size_t i = 0; i < method_count; ++i) dispatch(order[(trial + i) % method_count], static_cast<int>(trial));
  }
  for (std::size_t method = 0; method < method_count; ++method) {
    auto sorted = samples[method];
    std::sort(sorted.begin(), sorted.end());
    std::cout << dataset << ',' << pattern << ',' << keys.size() << ',' << names[method]
              << ',' << batch_sizes[method] << ',' << queries.size() << ',' << hits << ',' << trials
              << ',' << sorted[trials / 2] << ',' << sorted.front() << ',' << sorted.back()
              << ',' << checksum << ',' << (method == 0 ? keys.size() * sizeof(std::uint32_t) : tree.storage_bytes())
              << ',' << (method == 0 ? 0 : build_ms);
    for (auto sample : samples[method]) std::cout << ',' << sample;
    std::cout << '\n';
  }
  std::cout.flush();
  std::cerr << "PASS " << dataset << '/' << pattern << " n=" << keys.size() << '\n';
}
} // namespace

int main(int argc, char** argv)
try {
  if (argc > 2) throw std::runtime_error("Usage: batch_benchmark [16mer-u32le-file]");
  std::cout << std::fixed << std::setprecision(3)
      << "dataset,pattern,n,method,batch_size,queries,hits,trials,median_ns,min_ns,max_ns,checksum,storage_bytes,build_ms";
  for (std::size_t i = 0; i < trials; ++i) std::cout << ",trial_" << i << "_ns";
  std::cout << '\n';
  if (argc == 2) {
    const auto genome = workloads::read_keys(argv[1]);
    for (const auto n : {std::size_t{1048576}, std::size_t{8388608}, genome.size()}) {
      if (n > genome.size()) continue;
      workloads::Keys keys(genome.begin(), genome.begin() + static_cast<std::ptrdiff_t>(n));
      std::sort(keys.begin(), keys.end());
      for (const auto& pattern : {"mixed", "hot", "sorted"}) run(keys, "chr22_16mers", pattern);
    }
  } else {
    for (const auto n : {std::size_t{1024}, std::size_t{65536}, std::size_t{1048576},
                         std::size_t{8388731}, std::size_t{33554555}}) {
      const auto keys = workloads::make_keys(n, "uniform", 42);
      for (const auto& pattern : {"mixed", "hot", "sorted"}) run(keys, "uniform", pattern);
    }
    for (const auto n : {std::size_t{1048576}, std::size_t{33554555}}) {
      const auto keys = workloads::make_keys(n, "clusters", 43);
      for (const auto& pattern : {"mixed", "hot"}) run(keys, "clusters", pattern);
    }
    const auto equal = workloads::make_keys(1048576, "equal", 44);
    run(equal, "equal", "mixed");
    const auto dense = workloads::make_keys(1048576, "dense", 45);
    for (const auto& pattern : {"hits", "misses"}) run(dense, "dense", pattern);
  }
} catch (const std::exception& error) {
  std::cerr << "FAIL: " << error.what() << '\n';
  return 1;
}
