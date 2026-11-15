#include "static_search_tree.hpp"
#include "eytzinger.hpp"

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <string_view>

namespace {
constexpr std::size_t query_count = 1'000'000;
constexpr std::size_t trials = 8;

template<class Search>
double measure(const Search& search,
               const std::vector<std::uint32_t>& queries,
               std::vector<std::size_t>& output)
{
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < queries.size(); ++i) {
    output[i] = search(queries[i]);
  }
  const auto stop = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::nano>(stop - start).count()
         / static_cast<double>(queries.size());
}

void report(std::size_t n, const char* name, std::array<double, trials> times,
            const std::vector<std::size_t>& output, std::size_t storage_bytes,
            double build_ms)
{
  const auto raw_times = times;
  std::sort(times.begin(), times.end());
  std::uint64_t checksum = 0;
  for (const auto index : output) {
    checksum += index;
  }
  std::cout << n << ',' << n * sizeof(std::uint32_t) << ',' << name << ','
            << query_count << ',' << trials << ','
            << (times[trials / 2 - 1] + times[trials / 2]) / 2 << ','
            << times.front() << ',' << times.back() << ',' << checksum << ','
            << storage_bytes << ',' << build_ms;
  for (const auto time : raw_times) std::cout << ',' << time;
  std::cout << std::endl;
}
} // namespace

int main(int argc, char** argv)
{
  const std::string_view workload = argc == 2 ? argv[1] : "uniform";
  if (argc > 2 || (workload != "uniform" && workload != "duplicates"
                   && workload != "equal")) {
    std::cerr << "Usage: search_benchmark [uniform|duplicates|equal]\n";
    return 1;
  }
  constexpr std::array<std::size_t, 6> sizes{
      1'024, 65'536, 1'048'576, 8'388'608, 16'777'216 + 123, 33'554'432 + 123
  };
  std::cout << "elements,input_bytes,implementation,queries,trials,median_ns_per_query,"
               "min_ns_per_query,max_ns_per_query,checksum,storage_bytes,build_ms";
  for (std::size_t i = 0; i < trials; ++i) std::cout << ",trial_" << i << "_ns";
  std::cout << '\n';
  std::cout << std::fixed << std::setprecision(3);

  for (const auto n : sizes) {
    // Stress distributions at one smaller and one RAM-sized input.
    if (workload != "uniform" && n != sizes[2] && n != sizes[5]) continue;
    std::mt19937 generator(42);
    std::uniform_int_distribution<std::uint32_t> distribution(0, 0x7fffffffU);
    std::vector<std::uint32_t> values(n);
    for (auto& value : values) {
      if (workload == "uniform") {
        value = distribution(generator);
      } else if (workload == "duplicates") {
        // Eight clusters including values on both sides of the signed boundary.
        value = static_cast<std::uint32_t>(generator() % 8) * 0x24924924U;
      } else {
        value = 0x80000000U;
      }
    }
    std::sort(values.begin(), values.end());
    std::vector<std::uint32_t> queries(query_count);
    for (std::size_t i = 0; i < queries.size(); ++i) {
      if (workload == "uniform") {
        queries[i] = distribution(generator);
      } else {
        // Half exact hits, half full-range queries (including above the data).
        queries[i] = i % 2 == 0 ? values[generator() % n]
                                : static_cast<std::uint32_t>(generator());
      }
    }
    const auto build_start = std::chrono::steady_clock::now();
    const sst::Eytzinger tree(values);
    const double build_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - build_start).count();
    const auto reference_search = [&](std::uint32_t q) {
      return sst::reference_lower_bound(values, q);
    };
    const auto binary_search = [&](std::uint32_t q) {
      return sst::binary_lower_bound(values, q);
    };
    const auto tree_search = [&](std::uint32_t q) { return tree.lower_bound(q); };
    const auto prefetch_search = [&](std::uint32_t q) { return tree.lower_bound_prefetch(q); };
    std::vector<std::size_t> reference(query_count), binary(query_count), layout(query_count),
        prefetched(query_count);

    // Full untimed warm-up and element-by-element correctness check.
    measure(reference_search, queries, reference);
    measure(binary_search, queries, binary);
    measure(tree_search, queries, layout);
    measure(prefetch_search, queries, prefetched);
    if (reference != binary || reference != layout || reference != prefetched) {
      std::cerr << "Warm-up results differ for " << n << " elements\n";
      return 1;
    }

    std::array<double, trials> reference_times{}, binary_times{}, tree_times{}, prefetch_times{};
    for (std::size_t trial = 0; trial < trials; ++trial) {
      // Eight trials give each implementation every execution position twice.
      for (std::size_t offset = 0; offset < 4; ++offset) {
        switch ((trial + offset) % 4) {
          case 0: reference_times[trial] = measure(reference_search, queries, reference); break;
          case 1: binary_times[trial] = measure(binary_search, queries, binary); break;
          case 2: tree_times[trial] = measure(tree_search, queries, layout); break;
          case 3: prefetch_times[trial] = measure(prefetch_search, queries, prefetched); break;
        }
      }
      // Consume and validate every trial's output outside the timed region.
      if (reference != binary || reference != layout || reference != prefetched) {
        std::cerr << "Measured results differ for " << n << " elements\n";
        return 1;
      }
    }
    const auto input_storage = values.capacity() * sizeof(std::uint32_t);
    report(n, "std_lower_bound", reference_times, reference, input_storage, 0);
    report(n, "binary_lower_bound", binary_times, binary, input_storage, 0);
    report(n, "eytzinger", tree_times, layout, tree.storage_bytes(), build_ms);
    report(n, "eytzinger_prefetch", prefetch_times, prefetched, tree.storage_bytes(), build_ms);
  }
}
