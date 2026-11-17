# SPlusTree

[![C++ CI](https://github.com/jdeepak1218/static-search-tree/actions/workflows/ci.yml/badge.svg)](https://github.com/jdeepak1218/static-search-tree/actions/workflows/ci.yml)

A header-only C++20 S+ tree for lower-bound searches over static, sorted
`std::uint32_t` keys. It owns its keys, returns positions in sorted order, and
supports individual and batched queries. No runtime dependencies.

## Performance

On 39.2 million DNA-derived keys, prefetched batches of 32 took **35–38 ns/query**
versus **621–684 ns/query** for `std::lower_bound`: about **18× throughput**.
It was also about **8.2×** faster than prefetched Eytzinger in the same runs.
These are two recorded runs on an i7-12700H under WSL2 with GCC 13.3,
`-O3 -mavx2 -mpopcnt`, and caller-owned output buffers.

The runs used no core pinning or frequency lock; WSL2 does not establish whether
work ran on a physical P-core. Timings exclude construction and measure independent
query throughput. [Benchmark methodology and reproduction](benchmarks/README.md)
include a prefetched Eytzinger baseline and links to the historical raw results.

## Quick start

Copy [include/stree.hpp](include/stree.hpp) into your project:

```cpp
#include "stree.hpp"
#include <array>
#include <iostream>

int main()
{
    const sst::StaticSearchTree tree({2, 5, 5, 9, 14});
    const auto index = tree.lower_bound(6);
    if (index != tree.size()) std::cout << tree.key_at(index) << '\n'; // 9
    std::cout << tree.contains(5) << '\n'; // 1

    const std::array<std::uint32_t, 4> queries{6, 5, 20, 0};
    for (auto position : tree.lower_bound_batch(queries)) {
        std::cout << position << ' '; // 3 1 5 0
    }
}
```

```bash
g++ -std=c++20 -O3 main.cpp -o example
```

Optional `-mavx2 -mpopcnt` enables the accelerated batch backend on compatible
x86 hardware. Selection happens at compile time; there is no runtime CPU dispatch.
Use the same target flags and backend macros in every translation unit.

## API

Construct from a sorted vector, array, span, or initializer list of `uint32_t`.
Unsorted input throws `std::invalid_argument`. The source can be changed or
released after construction.

| Operation | Result |
| --- | --- |
| `lower_bound(query)` | First position with key ≥ query; `size()` if absent. |
| `key_at(index)` | Owned key at a sorted position; throws `std::out_of_range` for `index >= size()`. |
| `contains(query)` | Whether an exact key exists. |
| `lower_bound_batch(queries)` | Allocated vector of positions in query order. |
| `lower_bound_batch(queries, output)` | Writes positions to an equally sized caller buffer. |
| `size()`, `empty()` | Logical key count and empty status. |
| `storage_bytes()` | Allocated index buffer capacity, excluding caller buffers. |

Duplicates return their first position. The full unsigned range is supported,
including `UINT32_MAX`. Empty indexes return zero for every lower-bound query.
Batch input/output must not overlap; length mismatches throw before writing.
Concurrent const searches are supported with separate output buffers. Copy and
move operations own their storage; changing indexed keys requires rebuilding.

## Build and test

To consume with CMake:

```cmake
add_subdirectory(external/static-search-tree-cpp)
target_link_libraries(my_app PRIVATE sst::static_search_tree)
```

To run the example and tests (CMake 3.21+):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSST_TEST_AVX2=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

The optional accelerated matrix has 22 tests on a supported x86 host. Add
`-DSST_ENABLE_SANITIZERS=ON` in a separate GCC/Clang build for ASan/UBSan.
The linked CI workflow is configured for GCC, Clang, MSVC, and sanitizers; the
badge shows its hosted status.

[Design and backend details](docs/design.md) explain the layout and SIMD code.
Historical experiment reports, validation logs, and CSVs are preserved on the
[`results-archive` branch](https://github.com/jdeepak1218/static-search-tree/tree/results-archive) and in [the pre-cleanup revision](https://github.com/jdeepak1218/static-search-tree/tree/b11610eda455449983d5e93c52f9705ee45dec80).

## Background and license

The search optimizations build on Ragnar Groot Koerkamp's
[Static search trees](https://curiouscoding.nl/posts/static-search-tree/)
and Algorithmica's [static B-trees](https://en.algorithmica.org/hpc/data-structures/s-tree/).
This C++ implementation adds an owning index API, full-range unsigned searches,
first-duplicate positions, portable fallbacks, and integration tests.

[MIT licensed](LICENSE). Retained upstream notices are in
[third_party](third_party/README.md) and the adapted headers.
