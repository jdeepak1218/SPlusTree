#pragma once

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace workloads {
using Keys = std::vector<std::uint32_t>;
constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();

// Portable little-endian uint32 keys; reject partial or empty datasets.
inline Keys read_keys(const std::string& path)
{
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("Cannot open dataset: " + path);
  const auto length = input.tellg();
  if (length <= 0 || length % 4 != 0) throw std::runtime_error("Invalid dataset length");
  input.seekg(0);
  Keys keys;
  keys.reserve(static_cast<std::size_t>(length / 4));
  char bytes[4];
  while (input.read(bytes, 4)) {
    std::uint32_t key = 0;
    for (unsigned i = 0; i < 4; ++i) {
      key |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (8 * i);
    }
    keys.push_back(key);
  }
  if (!input.eof() || keys.size() != static_cast<std::size_t>(length / 4)) {
    throw std::runtime_error("Failed to read complete dataset");
  }
  return keys;
}

inline Keys make_keys(std::size_t n, const std::string& distribution, std::uint32_t seed)
{
  std::mt19937 rng(seed);
  Keys keys(n);
  for (std::size_t i = 0; i < n; ++i) {
    if (distribution == "dense") keys[i] = static_cast<std::uint32_t>(i * 2);
    else if (distribution == "clusters") {
      // Simulated partitioned IDs: 16 distant clusters with repeated keys.
      keys[i] = static_cast<std::uint32_t>((rng() % 16) * 0x10000000ULL + rng() % 65536);
    } else if (distribution == "equal") keys[i] = 0x80000000U;
    else if (distribution == "uniform") keys[i] = static_cast<std::uint32_t>(rng());
    else throw std::runtime_error("Unknown key distribution: " + distribution);
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

inline Keys make_queries(const Keys& keys, std::size_t n, const std::string& pattern,
                         std::uint32_t seed)
{
  if (keys.empty()) throw std::runtime_error("Query generator needs nonempty keys");
  std::mt19937 rng(seed);
  Keys queries(n);
  for (std::size_t i = 0; i < n; ++i) {
    const auto hit = keys[rng() % keys.size()];
    if (pattern == "hits") queries[i] = hit;
    else if (pattern == "misses") {
      // Only used with dense even IDs: every query is guaranteed absent.
      if (keys.back() > maximum - 1 || keys.front() != 0) {
        throw std::runtime_error("Miss generator requires bounded dense even IDs");
      }
      queries[i] = hit | 1U;
    } else if (pattern == "mixed" || pattern == "sorted" || pattern == "reverse") {
      queries[i] = i % 2 == 0 ? hit : static_cast<std::uint32_t>(rng());
    } else if (pattern == "hot") {
      // 90% sampled from the same 64 positions, 10% unrestricted probes.
      queries[i] = rng() % 10 != 0 ? keys[(rng() % 64) * (keys.size() - 1) / 63]
                                  : static_cast<std::uint32_t>(rng());
    } else if (pattern == "boundaries") {
      const std::uint32_t extremes[]{0, 1, 0x7fffffffU, 0x80000000U, maximum - 1, maximum};
      queries[i] = i % 2 == 0 ? extremes[rng() % 6]
                              : (hit == maximum ? hit : hit + 1);
    } else throw std::runtime_error("Unknown query pattern: " + pattern);
  }
  if (pattern == "sorted" || pattern == "reverse") {
    std::sort(queries.begin(), queries.end());
    if (pattern == "reverse") std::reverse(queries.begin(), queries.end());
  }
  return queries;
}
} // namespace workloads
