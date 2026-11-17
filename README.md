# Static search tree for C++

A C++20 index for lower-bound searches over sorted `std::uint32_t` keys.
Copy **`include/stree.hpp`** into your project. It is self-contained and needs
no compiled library or third-party dependency.

## Quick start

```cpp
#include "stree.hpp"
#include <array>
#include <iostream>

int main()
{
    const sst::StaticSearchTree tree({2, 5, 5, 9, 14});

    const auto index = tree.lower_bound(6);
    if (index != tree.size()) {
        std::cout << index << '\n'; // 3: original key at this position is 9
    }

    const std::array<std::uint32_t, 4> queries{6, 5, 20, 0};
    const auto positions = tree.lower_bound_batch(queries);
    // positions is std::vector<std::size_t>{3, 1, 5, 0}
}
```

Compile with an optimizing C++20 compiler:

```bash
g++ -std=c++20 -O3 main.cpp -o example
```

To build the included example from this repository:

```bash
mkdir -p build
g++ -std=c++20 -O3 -Iinclude examples/simple_search.cpp -o build/simple_search
./build/simple_search
```

## What the results mean

`lower_bound(query)` returns a `std::size_t` position in the original sorted
input, selecting the **first key greater than or equal to the query**.

| Query for keys `[2, 5, 5, 9, 14]` | Returned position |
| --- | ---: |
| `0` | `0` |
| `5` | `1`, the first duplicate |
| `6` | `3`, whose key is `9` |
| `20` | `5`, equal to `tree.size()` |

Results are positions, not key values or booleans. `tree.size()` means there is
no qualifying key and must not be used to access an element. For an empty tree,
all positions are zero, also equal to its size. `UINT32_MAX` is a valid key and
query, not a missing-result marker. Batch results preserve query order.

## Construction and ownership

```cpp
const std::vector<std::uint32_t> keys{0, 5, 5, 9};
const sst::StaticSearchTree tree(keys);
const sst::StaticSearchTree empty;
```

Construction accepts vectors, `std::array`, C arrays, const spans, and initializer
lists. Keys must already be sorted in nondecreasing order; unsorted input throws
`std::invalid_argument`. The index copies its keys, so the source can be modified
or destroyed afterward. Arrays and spans do not require a temporary input vector.
Change keys by constructing a new tree. Allocation failures propagate normally.
Copy and move construction/assignment are supported.

`size()` returns the original number of keys, `empty()` reports whether it is
zero, and `storage_bytes()` reports index-owned allocated capacity, excluding
caller input/output buffers and temporary construction storage.

## Batch lookup

For convenience, let the library allocate the answers:

```cpp
auto positions = tree.lower_bound_batch(queries);
```

To reuse a buffer and avoid query-time heap allocation:

```cpp
std::vector<std::size_t> positions(queries.size());
tree.lower_bound_batch(queries, positions);
```

Query elements are `std::uint32_t`; output elements are `std::size_t`. The buffer
overload accepts spans, vectors, and arrays with equal lengths. A length mismatch
throws `std::invalid_argument` before output writes. Input/output storage must
not overlap. Empty queries produce no answers. Concurrent searches on the same
tree are supported with independent output buffers and valid read-only queries.

## Defaults and portability

The simple API chooses its implementation at compile time. Individual lookups
use counting. Batch lookups use counting for generic builds; eligible x86 builds
with AVX2 and POPCNT enabled use prefetched groups of 32, with individual tail
handling. Users do not pass an algorithm name or batch-size template argument.
These are conservative defaults, not a guarantee of the fastest method for
every distribution or machine.

An optional targeted GCC/Clang build is:

```bash
g++ -std=c++20 -O3 -mavx2 -mpopcnt main.cpp -o example
```

That executable requires a CPU supporting its enabled instructions. There is
**no runtime CPU dispatch**; build for the CPUs on which the program will run.
Generic builds are the default starting point. Selection macros and target flags
must be consistent across translation units. `SST_DISABLE_MASK_SIMD` selects the
counting batch fallback; `SST_DISABLE_PREFETCH` removes explicit hints.

The existing `sst::STree` methods remain available for tuning and experiments.
They do not need to be understood to use `sst::StaticSearchTree`.

## Validation

```bash
bash tools/run_validation.sh
```

The runner builds the C++ release configurations and sanitizer checks. See
[API validation](docs/simple-api.md), [search contract](docs/contract.md), and
[measured backend results](docs/batch-prefetch-results.md). The published backend
measurements exclude convenience-overload allocation and are not end-to-end
application performance guarantees. Further compiler/platform validation remains
part of release preparation.
