#include "static_search_tree.hpp"
#include "eytzinger.hpp"
#include "stree.hpp"

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <string_view>

namespace {
constexpr std::size_t query_count = 1'000'000;
constexpr std::size_t trials = 14;
constexpr std::size_t count_trials = 12;
constexpr std::size_t mask_trials = 10;
constexpr std::size_t popcount_trials = 12;

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

template<std::size_t Trials>
void report(std::size_t n, const char* name, std::array<double, Trials> times,
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
            << query_count << ',' << Trials << ','
            << (times[Trials / 2 - 1] + times[Trials / 2]) / 2 << ','
            << times.front() << ',' << times.back() << ',' << checksum << ','
            << storage_bytes << ',' << build_ms;
  for (const auto time : raw_times) std::cout << ',' << time;
  std::cout << std::endl;
}
} // namespace

int main(int argc, char** argv)
{
  const std::string_view workload = argc >= 2 ? argv[1] : "uniform";
  const std::string_view results = argc >= 3 ? argv[2] : "indices";
  if (argc > 3 || (workload != "uniform" && workload != "duplicates"
                   && workload != "equal") || (results != "indices" && results != "values" && results != "counting" && results != "mask" && results != "popcount")) {
    std::cerr << "Usage: search_benchmark [uniform|duplicates|equal] [indices|values|counting|mask|popcount]\n";
    return 1;
  }
  constexpr std::array<std::size_t, 6> sizes{
      1'024, 65'536, 1'048'576, 8'388'608, 16'777'216 + 123, 33'554'432 + 123
  };
  std::cout << "elements,input_bytes,implementation,queries,trials,median_ns_per_query,"
               "min_ns_per_query,max_ns_per_query,checksum,storage_bytes,build_ms";
  const auto output_trials = results == "popcount" ? popcount_trials
      : (results == "mask" ? mask_trials : (results == "counting" ? count_trials : trials));
  for (std::size_t i = 0; i < output_trials; ++i) std::cout << ",trial_" << i << "_ns";
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
    if (results == "values") {
      const auto standard_value = [&](std::uint32_t q) {
        const auto index = sst::reference_lower_bound(values, q);
        return index == values.size() ? std::numeric_limits<std::uint32_t>::max() : values[index];
      };
      const auto optimized_value = [&](std::uint32_t q) { return tree.search_value_optimized(q); };
      std::vector<std::size_t> reference(query_count), optimized(query_count);
      measure(standard_value, queries, reference);
      measure(optimized_value, queries, optimized);
      if (reference != optimized) {
        std::cerr << "Value warm-up mismatch for " << n << " elements\n";
        return 1;
      }
      std::array<double, trials> reference_times{}, optimized_times{};
      for (std::size_t trial = 0; trial < trials; ++trial) {
        if (trial % 2 == 0) {
          reference_times[trial] = measure(standard_value, queries, reference);
          optimized_times[trial] = measure(optimized_value, queries, optimized);
        } else {
          optimized_times[trial] = measure(optimized_value, queries, optimized);
          reference_times[trial] = measure(standard_value, queries, reference);
        }
        if (reference != optimized) {
          std::cerr << "Value result mismatch for " << n << " elements\n";
          return 1;
        }
      }
      report(n, "std_lower_bound_value", reference_times, reference,
             values.capacity() * sizeof(std::uint32_t), 0);
      // This object retains a position map for the separate index-returning API.
      report(n, "optimized_eytzinger_value", optimized_times, optimized, tree.storage_bytes(), build_ms);
      continue;
    }
    const auto stree_build_start = std::chrono::steady_clock::now();
    const sst::STree<> stree(values);
    const double stree_build_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - stree_build_start).count();
    const auto stree_search = [&](std::uint32_t q) { return stree.lower_bound(q); };
    if (results == "popcount") {
      const auto standard = [&](std::uint32_t q) { return sst::reference_lower_bound(values, q); };
      const auto eytzinger = [&](std::uint32_t q) { return tree.lower_bound_optimized(q); };
      const auto counted = [&](std::uint32_t q) { return stree.lower_bound_count(q); };
      const auto masked = [&](std::uint32_t q) { return stree.lower_bound_mask(q); };
      const auto populated = [&](std::uint32_t q) { return stree.lower_bound_popcount(q); };
      std::vector<std::size_t> reference(query_count), eytzinger_output(query_count),
          early_output(query_count), count_output(query_count), mask_output(query_count),
          population_output(query_count);
      measure(standard, queries, reference);
      measure(eytzinger, queries, eytzinger_output);
      measure(stree_search, queries, early_output);
      measure(counted, queries, count_output);
      measure(masked, queries, mask_output);
      measure(populated, queries, population_output);
      const auto matches = [&] {
        return reference == eytzinger_output && reference == early_output
            && reference == count_output && reference == mask_output && reference == population_output;
      };
      if (!matches()) { std::cerr << "Population warm-up mismatch\n"; return 1; }
      std::array<double, popcount_trials> standard_times{}, eytzinger_times{}, early_times{},
          count_times{}, mask_times{}, population_times{};
      for (std::size_t trial = 0; trial < popcount_trials; ++trial) {
        for (std::size_t position = 0; position < 6; ++position) {
          switch ((trial + position) % 6) {
            case 0: standard_times[trial] = measure(standard, queries, reference); break;
            case 1: eytzinger_times[trial] = measure(eytzinger, queries, eytzinger_output); break;
            case 2: early_times[trial] = measure(stree_search, queries, early_output); break;
            case 3: count_times[trial] = measure(counted, queries, count_output); break;
            case 4: mask_times[trial] = measure(masked, queries, mask_output); break;
            case 5: population_times[trial] = measure(populated, queries, population_output); break;
          }
        }
        if (!matches()) { std::cerr << "Population result mismatch\n"; return 1; }
      }
      report(n, "std_lower_bound", standard_times, reference, values.capacity() * sizeof(std::uint32_t), 0);
      report(n, "optimized_eytzinger", eytzinger_times, eytzinger_output, tree.storage_bytes(), build_ms);
      report(n, "stree_scalar", early_times, early_output, stree.storage_bytes(), stree_build_ms);
      report(n, "stree_count", count_times, count_output, stree.storage_bytes(), stree_build_ms);
      report(n, "stree_mask", mask_times, mask_output, stree.storage_bytes(), stree_build_ms);
      report(n, "stree_popcount", population_times, population_output, stree.storage_bytes(), stree_build_ms);
      continue;
    }
    if (results == "mask") {
      const auto standard = [&](std::uint32_t q) { return sst::reference_lower_bound(values, q); };
      const auto eytzinger = [&](std::uint32_t q) { return tree.lower_bound_optimized(q); };
      const auto counted = [&](std::uint32_t q) { return stree.lower_bound_count(q); };
      const auto masked = [&](std::uint32_t q) { return stree.lower_bound_mask(q); };
      std::vector<std::size_t> reference(query_count), eytzinger_output(query_count),
          early_output(query_count), count_output(query_count), mask_output(query_count);
      measure(standard, queries, reference);
      measure(eytzinger, queries, eytzinger_output);
      measure(stree_search, queries, early_output);
      measure(counted, queries, count_output);
      measure(masked, queries, mask_output);
      const auto matches = [&] {
        return reference == eytzinger_output && reference == early_output
            && reference == count_output && reference == mask_output;
      };
      if (!matches()) { std::cerr << "Mask warm-up mismatch\n"; return 1; }
      std::array<double, mask_trials> standard_times{}, eytzinger_times{}, early_times{}, count_times{}, mask_times{};
      for (std::size_t trial = 0; trial < mask_trials; ++trial) {
        for (std::size_t position = 0; position < 5; ++position) {
          switch ((trial + position) % 5) {
            case 0: standard_times[trial] = measure(standard, queries, reference); break;
            case 1: eytzinger_times[trial] = measure(eytzinger, queries, eytzinger_output); break;
            case 2: early_times[trial] = measure(stree_search, queries, early_output); break;
            case 3: count_times[trial] = measure(counted, queries, count_output); break;
            case 4: mask_times[trial] = measure(masked, queries, mask_output); break;
          }
        }
        if (!matches()) { std::cerr << "Mask result mismatch\n"; return 1; }
      }
      report(n, "std_lower_bound", standard_times, reference, values.capacity() * sizeof(std::uint32_t), 0);
      report(n, "optimized_eytzinger", eytzinger_times, eytzinger_output, tree.storage_bytes(), build_ms);
      report(n, "stree_scalar", early_times, early_output, stree.storage_bytes(), stree_build_ms);
      report(n, "stree_count", count_times, count_output, stree.storage_bytes(), stree_build_ms);
      report(n, "stree_mask", mask_times, mask_output, stree.storage_bytes(), stree_build_ms);
      continue;
    }
    if (results == "counting") {
      const auto standard = [&](std::uint32_t q) { return sst::reference_lower_bound(values, q); };
      const auto eytzinger = [&](std::uint32_t q) { return tree.lower_bound_optimized(q); };
      const auto counted = [&](std::uint32_t q) { return stree.lower_bound_count(q); };
      std::vector<std::size_t> reference(query_count), eytzinger_output(query_count),
          early_output(query_count), count_output(query_count);
      measure(standard, queries, reference);
      measure(eytzinger, queries, eytzinger_output);
      measure(stree_search, queries, early_output);
      measure(counted, queries, count_output);
      const auto matches = [&] {
        return reference == eytzinger_output && reference == early_output && reference == count_output;
      };
      if (!matches()) { std::cerr << "Counting warm-up mismatch\n"; return 1; }
      std::array<double, count_trials> standard_times{}, eytzinger_times{}, early_times{}, count_times{};
      for (std::size_t trial = 0; trial < count_trials; ++trial) {
        for (std::size_t position = 0; position < 4; ++position) {
          switch ((trial + position) % 4) {
            case 0: standard_times[trial] = measure(standard, queries, reference); break;
            case 1: eytzinger_times[trial] = measure(eytzinger, queries, eytzinger_output); break;
            case 2: early_times[trial] = measure(stree_search, queries, early_output); break;
            case 3: count_times[trial] = measure(counted, queries, count_output); break;
          }
        }
        if (!matches()) { std::cerr << "Counting result mismatch\n"; return 1; }
      }
      report(n, "std_lower_bound", standard_times, reference, values.capacity() * sizeof(std::uint32_t), 0);
      report(n, "optimized_eytzinger", eytzinger_times, eytzinger_output, tree.storage_bytes(), build_ms);
      report(n, "stree_scalar", early_times, early_output, stree.storage_bytes(), stree_build_ms);
      report(n, "stree_count", count_times, count_output, stree.storage_bytes(), stree_build_ms);
      continue;
    }
    const auto reference_search = [&](std::uint32_t q) {
      return sst::reference_lower_bound(values, q);
    };
    const auto binary_search = [&](std::uint32_t q) {
      return sst::binary_lower_bound(values, q);
    };
    const auto tree_search = [&](std::uint32_t q) { return tree.lower_bound(q); };
    const auto prefetch_search = [&](std::uint32_t q) { return tree.lower_bound_prefetch(q); };
    const auto optimized_search = [&](std::uint32_t q) { return tree.lower_bound_optimized(q); };
    const auto optimized_plain_search = [&](std::uint32_t q) { return tree.lower_bound_optimized_no_prefetch(q); };
    std::vector<std::size_t> reference(query_count), binary(query_count), layout(query_count),
        prefetched(query_count), optimized(query_count), optimized_plain(query_count),
        scalar(query_count);

    // Full untimed warm-up and element-by-element correctness check.
    measure(reference_search, queries, reference);
    measure(binary_search, queries, binary);
    measure(tree_search, queries, layout);
    measure(prefetch_search, queries, prefetched);
    measure(optimized_search, queries, optimized);
    measure(optimized_plain_search, queries, optimized_plain);
    measure(stree_search, queries, scalar);
    if (reference != binary || reference != layout || reference != prefetched
        || reference != optimized || reference != optimized_plain || reference != scalar) {
      std::cerr << "Warm-up results differ for " << n << " elements\n";
      return 1;
    }

    std::array<double, trials> reference_times{}, binary_times{}, tree_times{}, prefetch_times{};
    std::array<double, trials> optimized_times{}, optimized_plain_times{};
    std::array<double, trials> stree_times{};
    for (std::size_t trial = 0; trial < trials; ++trial) {
      // Fourteen trials give each implementation every execution position twice.
      for (std::size_t offset = 0; offset < 7; ++offset) {
        switch ((trial + offset) % 7) {
          case 0: reference_times[trial] = measure(reference_search, queries, reference); break;
          case 1: binary_times[trial] = measure(binary_search, queries, binary); break;
          case 2: tree_times[trial] = measure(tree_search, queries, layout); break;
          case 3: prefetch_times[trial] = measure(prefetch_search, queries, prefetched); break;
          case 4: optimized_times[trial] = measure(optimized_search, queries, optimized); break;
          case 5: optimized_plain_times[trial] = measure(optimized_plain_search, queries, optimized_plain); break;
          case 6: stree_times[trial] = measure(stree_search, queries, scalar); break;
        }
      }
      // Consume and validate every trial's output outside the timed region.
      if (reference != binary || reference != layout || reference != prefetched
          || reference != optimized || reference != optimized_plain || reference != scalar) {
        std::cerr << "Measured results differ for " << n << " elements\n";
        return 1;
      }
    }
    const auto input_storage = values.capacity() * sizeof(std::uint32_t);
    report(n, "std_lower_bound", reference_times, reference, input_storage, 0);
    report(n, "binary_lower_bound", binary_times, binary, input_storage, 0);
    report(n, "eytzinger", tree_times, layout, tree.storage_bytes(), build_ms);
    report(n, "eytzinger_prefetch", prefetch_times, prefetched, tree.storage_bytes(), build_ms);
    report(n, "optimized_eytzinger", optimized_times, optimized, tree.storage_bytes(), build_ms);
    report(n, "optimized_eytzinger_no_prefetch", optimized_plain_times, optimized_plain, tree.storage_bytes(), build_ms);
    report(n, "stree_scalar", stree_times, scalar, stree.storage_bytes(), stree_build_ms);
  }
}
