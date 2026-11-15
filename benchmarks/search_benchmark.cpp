#include "static_search_tree.hpp"

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>

namespace {
using Search = std::size_t (*)(const std::vector<std::uint32_t>&, std::uint32_t);
constexpr std::size_t query_count = 1'000'000;
constexpr std::size_t trials = 7;

double measure(Search search, const std::vector<std::uint32_t>& values,
               const std::vector<std::uint32_t>& queries,
               std::vector<std::size_t>& output)
{
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < queries.size(); ++i) {
    output[i] = search(values, queries[i]);
  }
  const auto stop = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::nano>(stop - start).count()
         / static_cast<double>(queries.size());
}

void report(std::size_t n, const char* name, std::array<double, trials> times,
            const std::vector<std::size_t>& output)
{
  std::sort(times.begin(), times.end());
  std::uint64_t checksum = 0;
  for (const auto index : output) {
    checksum += index;
  }
  std::cout << n << ',' << n * sizeof(std::uint32_t) << ',' << name << ','
            << query_count << ',' << trials << ',' << times[trials / 2] << ','
            << times.front() << ',' << times.back() << ',' << checksum
            << std::endl;
}
} // namespace

int main()
{
  constexpr std::array<std::size_t, 6> sizes{
      1'024, 65'536, 1'048'576, 8'388'608, 16'777'216 + 123, 33'554'432 + 123
  };
  std::cout << "elements,input_bytes,implementation,queries,trials,median_ns_per_query,"
               "min_ns_per_query,max_ns_per_query,checksum\n";
  std::cout << std::fixed << std::setprecision(3);

  for (const auto n : sizes) {
    std::mt19937 generator(42);
    std::uniform_int_distribution<std::uint32_t> distribution(0, 0x7fffffffU);
    std::vector<std::uint32_t> values(n);
    for (auto& value : values) {
      value = distribution(generator);
    }
    std::sort(values.begin(), values.end());
    std::vector<std::uint32_t> queries(query_count);
    for (auto& query : queries) {
      query = distribution(generator);
    }
    std::vector<std::size_t> reference(query_count), binary(query_count);

    // Full untimed warm-up and element-by-element correctness check.
    measure(sst::reference_lower_bound, values, queries, reference);
    measure(sst::binary_lower_bound, values, queries, binary);
    if (reference != binary) {
      std::cerr << "Warm-up results differ for " << n << " elements\n";
      return 1;
    }

    std::array<double, trials> reference_times{}, binary_times{};
    for (std::size_t trial = 0; trial < trials; ++trial) {
      // Alternate order to reduce systematic first/second-run bias.
      if (trial % 2 == 0) {
        reference_times[trial] = measure(sst::reference_lower_bound, values, queries, reference);
        binary_times[trial] = measure(sst::binary_lower_bound, values, queries, binary);
      } else {
        binary_times[trial] = measure(sst::binary_lower_bound, values, queries, binary);
        reference_times[trial] = measure(sst::reference_lower_bound, values, queries, reference);
      }
      // Consume and validate every trial's output outside the timed region.
      if (reference != binary) {
        std::cerr << "Measured results differ for " << n << " elements\n";
        return 1;
      }
    }
    report(n, "std_lower_bound", reference_times, reference);
    report(n, "binary_lower_bound", binary_times, binary);
  }
}
