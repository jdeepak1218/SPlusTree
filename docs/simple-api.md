# Simple public API

`sst::StaticSearchTree` is the recommended interface for applications. It lives
in the standalone `include/stree.hpp`; copying that one header is sufficient.
The root README and `examples/simple_search.cpp` demonstrate complete usage.

## Interface

| Operation | Behavior |
| --- | --- |
| Construction | Accept sorted uint32 vectors, arrays, spans, or initializer lists; own a copy |
| `lower_bound(query)` | Return the first qualifying sorted-input position, or `size()` |
| `lower_bound_batch(queries)` | Return an allocated vector of positions in query order |
| `lower_bound_batch(queries, output)` | Fill caller storage without heap allocation |
| `size()`, `empty()` | Inspect logical input size |
| `storage_bytes()` | Report index-owned allocated capacity |

Users do not supply algorithm names, node widths, or batch-size template
arguments. Both batch forms accept arbitrary counts, including empty inputs and
incomplete groups. Output positions are relative to the construction input.
Duplicate keys return their first occurrence. All uint32 values remain valid;
the maximum is not a missing-result sentinel. An empty tree returns zero.

Unsorted keys throw `std::invalid_argument`. Unequal query/output lengths throw
before writes. The buffer overload requires non-overlapping valid storage.
Convenience-overload allocation failures propagate normally. Concurrent const
lookups use independent output storage. Source keys need not outlive the tree;
copy and move operations retain owning-container semantics.

## Default policy

Individual lookup uses the existing counting implementation. Generic batches
also use counting. Eligible x86 builds with AVX2 and POPCNT enabled and the
explicit mask backend available use prefetched batches of 32. The existing
individual tail handling processes incomplete groups. Disabling prefetch hints
retains the batch traversal but omits the hints.

Selection is compile-time, not runtime CPU detection. An executable built with
target-specific instructions requires appropriate hardware. Generic compilation
is the starting point; consistent flags/macros are required across translation
units. This is a conservative policy based on existing backend measurements,
not a promise that one default wins on every workload or machine.

Existing `sst::STree` APIs remain unchanged. Its vector constructor now delegates
to a span-based construction path, so the facade can consume arrays and spans
without allocating an intermediate vector. The tree still copies leaf keys and
owns the same internal storage. No extra policy object or duplicate key vector
is stored in the facade.

## Validation

The new API suite passes **694,215 checks across 88 dataset/query cases per
configuration**. The full runner executes it in five release configurations:
generic, AVX2/POPCNT, explicit mask backend disabled, software population count,
and prefetch hints disabled. It also passes ASan/UBSan with library assertions.
All existing reference, Eytzinger, S+, robustness, batch, data and encoder checks
pass after the span-constructor change.

New coverage includes default/list/vector/array/C-array/span construction,
temporary inputs, sorted subspans, source mutation and destruction, copy/move
assignment, empty and all-equal indexes, duplicates, full unsigned boundaries,
irregular tree capacities, million-key arrays, query counts around default-batch
boundaries, poisoned output guards, no writes on length rejection, documented
examples, reference comparisons and independent lower-bound bracket checks.
Four concurrent readers also exercise the facade.

An isolated directory containing only a copied `stree.hpp` successfully compiles
and runs the example API in five build configurations, with repeated inclusion
and two translation units. The root README quick-start code was extracted,
compiled, and run independently. The complete included example was compiled and
run too.

Raw evidence is in [api-validation.txt](../tests/api-validation.txt) and
[api-integration.txt](../tests/api-integration.txt). Reproduce with:

```bash
bash tools/run_validation.sh
g++ -std=c++20 -O3 -Iinclude examples/simple_search.cpp -o build/simple_search
./build/simple_search
```

No new search algorithm or speedup is claimed in this API change. Existing
backend measurements remain available; allocating output and complete application
costs must be measured for the intended use case. The full benchmark matrix was
not rerun for these delegating methods.

This environment tested GCC 13.3 / x86 under WSL. Leak detection was disabled for
ASan runs. ThreadSanitizer, other compilers/platforms, runtime CPU dispatch, and
automated CI remain separate release-preparation work. The simple public API is
complete; this does not constitute full cross-platform production qualification.
