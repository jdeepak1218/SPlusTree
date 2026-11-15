# Counting-based node search

`STree::lower_bound_count` uses the existing packed tree, separators, leaves,
alignment, and ownership. It changes how each node chooses a child or leaf slot.
`lower_bound` keeps the early-exit implementation for comparison and predictable
workloads. Both methods return the first original sorted index at least the
query, or the input size when none exists.

## Early exit versus counting

The original helper stops at the first qualifying key:

```cpp
for (std::size_t i = 0; i < KeysPerNode; ++i) {
    if (node.keys[i] >= query) return i;
}
return KeysPerNode;
```

The counting helper reads all key slots:

```cpp
std::uint32_t count = 0;
for (std::size_t i = 0; i < KeysPerNode; ++i) {
    count += static_cast<std::uint32_t>(node.keys[i] < query);
}
return count;
```

For sorted keys `[2,5,5,9]` and query 5:

```text
Comparison: [2<5, 5<5, 5<5, 9<5]
Results:    [ 1,   0,   0,   0 ]
Sum:        1
```

Exactly one key is smaller than 5, so index 1 is the first qualifying position.
For query 6 the sum is 3, selecting key 9. A query above every key produces the
node width, intentionally selecting the final child or spilling to the next leaf.

The strict `<` matters: `<=` would skip equal keys and change lower bound into
upper bound. Comparisons stay unsigned, including values above `INT32_MAX`.
The accumulator cannot overflow: the template permits at most 16 slots and
each comparison contributes zero or one. Conversion to `size_t` is exact.

Padding slots contain `UINT32_MAX`. They never contribute to the count because
no valid query exceeds that value. Actual maximum keys also compare correctly;
the logical input size continues to distinguish real data from unused slots.

## Why this permits SIMD

An early return creates a data-dependent stopping point. Counting is a fixed-size
reduction of independent comparisons, allowing the compiler to compare multiple
keys at once and combine their contributions.

On the inspected GCC 13.3 x86-64 build:

- Generic `-O3` produces 128-bit SIMD, handling four 32-bit keys per vector.
- `-O3 -mavx2` produces 256-bit SIMD, handling eight keys per vector.
- The default node has sixteen keys, so AVX2 processes two eight-key groups.
- Generated instructions include `vpminud`, `vpcmpeqd`, `vpcmpgtd`, and
  `vpaddd`, plus mask conversion and horizontal reduction operations.
- The compiler preserves unsigned comparison semantics through unsigned minimum
  and comparison transformations. A signed-looking machine instruction alone
  does not mean the C++ comparison became signed.
- Both the internal-node loop and final leaf scan are vectorized. The query
  still visits levels sequentially; this is parallelism within a node, not
  batching multiple independent queries.

No SIMD intrinsics, runtime dispatch, tree prefetching, or batching are added.
An AVX2-compiled application requires an AVX2-capable execution environment.
The ordinary header remains usable without that flag; other targets may emit
different SIMD or scalar instructions. Auto-vectorization is a compiler choice.

## GCC unrolling detail

Initial inspection found that GCC vectorized the leaf reduction but unrolled
internal reductions into scalar compare/add chains before vectorization.
`-fvect-cost-model=unlimited` did not correct that code shape in the probe.

The helper now places this conditional hint immediately before its loop:

```cpp
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC unroll 1
#endif
```

This suppresses premature scalar unrolling and lets GCC's vectorizer see the
reduction in both contexts. It does not specify vector instructions or change
the algorithm. Other compilers compile the ordinary C++ loop without this hint.
Compiler diagnostics and assembly from both the probe and actual AVX2 benchmark
confirm vectorization of both scans on the tested toolchain.

## Reproduce code generation

From the project root:

```bash
g++ -std=c++20 -O3 -Iinclude -S -fopt-info-vec-optimized=build/count_generic.txt benchmarks/count_codegen.cpp -o build/count_generic.s
g++ -std=c++20 -O3 -mavx2 -Iinclude -S -fopt-info-vec-optimized=build/count_avx2.txt benchmarks/count_codegen.cpp -o build/count_avx2.s
g++ -std=c++20 -O3 -fno-tree-loop-vectorize -fno-tree-slp-vectorize -Iinclude -S benchmarks/count_codegen.cpp -o build/count_no_vector.s
```

Inspect the `stree_count_lookup` symbol and its internal-level loop, not just
the presence of any vector instruction elsewhere in the assembly. The
vectorization-disabled build is a control for this source and compiler setup,
not a claim to be the best possible hand-optimized scalar reduction.

## Tradeoffs

Counting always processes all sixteen slots. Early exit can be cheaper when
queries repeatedly qualify at the beginning of nodes or branches are predictable.
The new method is therefore explicit rather than replacing the original API.
No persistent storage or construction changes are needed; both methods query
the same object. Benchmark results and validation are in `docs/count-results.md`.
