# Scalar S+ tree results

## Implementation

`include/stree.hpp` is a self-contained header with an owning, immutable
`sst::STree<>` index. The default uses 16 keys per 64-byte-aligned node and up to
17 children per internal node. `STree<2>` is available for worked examples and
tests. Keys live in sorted order in leaves; each internal separator is a copy
of the minimum key in its right child subtree. Levels are packed contiguously
with explicit offsets. Child locations use arithmetic rather than pointers.

The node search is the initial scalar, early-exit linear scan. There is no
explicit SIMD, prefetching, batching, partitioning, or alternative separator
convention in this stage. The result is the first original sorted position at
least the query, or the input size. Sorted input is validated at construction.

The complete design and a six-key example are in `docs/stree-design.md`.

## Correctness and safety checks

- 4,657,931 differential queries across 3,697 datasets passed against
  `std::lower_bound`, testing node widths 2 and 16.
- Coverage includes every size from zero through 1,025; exhaustive sorted arrays
  through length ten over `{0, 1, UINT32_MAX}`; 500 randomized cases checked with
  both widths; complete-tree capacities and neighboring sizes across multiple
  heights; full-range unsigned keys; all-zero/all-maximum arrays; duplicate runs;
  exact hits and neighboring misses; and incomplete leaves and internal groups.
- Large correctness arrays contain 1,000,003 and 8,388,731 keys. A separate
  million-key maximum-only case checks that real maximum keys are not padding.
- Unsorted input, leaf spill, first duplicates, source mutation, copying,
  moving, and assignment are explicitly checked.
- Optimized normal and `-DNDEBUG` builds pass. Existing reference and Eytzinger
  tests also pass.
- AddressSanitizer, UndefinedBehaviorSanitizer with recovery disabled, and
  libstdc++ assertions pass. Leak detection is disabled because the environment
  cannot run LeakSanitizer under its tracing setup.
- Temporary wrong-equality, wrong-separator, and disabled-spill mutations are
  each caught by the test suite. No mutants are retained.
- Standalone-header, repeated-inclusion, and two-translation-unit compilation,
  linking, and execution pass, including a cross-leaf query.

Padding uses `UINT32_MAX`, but this remains valid data. Since no query exceeds
that value, a missing separator cannot select a nonexistent child: the first
padded separator selects the last existing child, or an earlier real separator
already selects an existing child. At leaves, positions are capped by the
logical input size, distinguishing real keys from unused slots. Leaf spill
calculates a sorted index, avoiding pointer arithmetic across node objects.

Ceiling division avoids overflowing `n + divisor - 1`. Total node counts are
checked before allocation. `alignas(64)` makes the vector honor node alignment.
Searches neither allocate nor modify storage. No address or undefined-behavior
errors were detected; this does not prove correctness for every possible input.
Allocation-failure injection, concurrency, other template widths, compilers,
and architectures remain outside the tested matrix.

## Benchmark method

Seven index-returning methods are compared within each process. Each uses the
same million queries per dataset, one full warm-up, and fourteen measured
trials. Rotation gives each method every execution position twice. Median is
the average of the middle two sorted timings; CSVs retain all raw trials and
min/max. Every output is compared element by element with reference results
outside timed regions, including in release builds.

Uniform runs use six input sizes through 33,554,555 keys, with uniform 31-bit
keys and queries. The full uniform run is repeated in a separate process.
Duplicate-heavy and all-equal inputs use the 1,048,576 and 33,554,555 sizes with
half sampled-hit and half full-range random queries. Generation is seeded with
42. Runs execute sequentially. Total work is 1,568 million measured searches
and 112 million warm-up searches.

Timing includes search and preallocated output writes, excluding generation,
sorting, construction, allocation, comparisons, and printing. Construction time
is one observation per dataset, not a repeated construction benchmark. Storage
reports persistent vector capacities; temporary construction buffers, allocator
metadata, query/output storage, and object headers are excluded. Both the input
and the search indexes remain in memory for validation.

Environment: GCC 13.3.0 with `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic
-Wconversion -Iinclude`, x86-64 WSL2 on a reported i7-12700H with two visible
logical CPUs. No affinity, fixed frequency, requested/verified huge pages, or
cold-cache flushing. Host activity can change absolute timings. These are
independent-query throughput measurements, not dependent single-query latency.
Reproduction commands are in `benchmarks/README.md` and `tests/README.md`.

## Results

Median ns/query, lower is better. All methods return original sorted positions.

### Uniform, first process

Source: `benchmarks/stree_uniform.csv`.

| Elements | Standard | Binary | Original Eytzinger | Earlier T2 | Optimized Eytzinger | Optimized without hints | Scalar S+ |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 58.165 | 59.785 | 50.440 | 57.638 | 9.453 | 8.838 | 40.482 |
| 65,536 | 107.707 | 107.279 | 86.122 | 97.586 | 18.603 | 21.000 | 66.884 |
| 1,048,576 | 210.199 | 215.305 | 129.187 | 136.147 | 48.978 | 72.473 | 106.623 |
| 8,388,608 | 430.591 | 411.814 | 244.944 | 274.892 | 151.469 | 267.018 | 175.782 |
| 16,777,339 | 507.030 | 485.143 | 285.381 | 323.473 | 181.700 | 371.767 | 194.480 |
| 33,554,555 | 627.997 | 618.010 | 325.925 | 361.726 | 219.761 | 499.682 | 223.920 |

Largest-input timing ranges (ns/query):

- `std_lower_bound`: 559.705–707.771.
- `optimized_eytzinger`: 196.093–251.487.
- `stree_scalar`: 211.256–237.284.

### Uniform, repeat process

Source: `benchmarks/stree_uniform_repeat.csv`.

| Elements | Standard | Binary | Original Eytzinger | Earlier T2 | Optimized Eytzinger | Optimized without hints | Scalar S+ |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 57.781 | 59.833 | 50.453 | 57.516 | 9.341 | 8.864 | 40.844 |
| 65,536 | 108.606 | 107.884 | 86.137 | 97.304 | 18.487 | 21.306 | 67.702 |
| 1,048,576 | 211.049 | 214.958 | 131.790 | 140.246 | 52.311 | 79.218 | 106.540 |
| 8,388,608 | 433.363 | 414.826 | 246.637 | 274.123 | 151.334 | 272.985 | 174.021 |
| 16,777,339 | 480.194 | 444.795 | 283.863 | 320.478 | 178.979 | 365.791 | 195.711 |
| 33,554,555 | 607.456 | 598.989 | 325.729 | 360.801 | 210.445 | 499.204 | 222.823 |

Largest-input timing ranges (ns/query):

- `std_lower_bound`: 541.592–658.277.
- `optimized_eytzinger`: 208.012–241.171.
- `stree_scalar`: 219.056–238.301.

### Duplicate-heavy

Source: `benchmarks/stree_duplicates.csv`.

| Elements | Standard | Binary | Original Eytzinger | Earlier T2 | Optimized Eytzinger | Optimized without hints | Scalar S+ |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 35.846 | 38.489 | 27.654 | 36.855 | 26.891 | 33.190 | 24.685 |
| 33,554,555 | 38.842 | 39.938 | 33.544 | 44.948 | 74.928 | 51.338 | 30.116 |

Largest-input timing ranges (ns/query):

- `std_lower_bound`: 37.661–46.333.
- `optimized_eytzinger`: 73.899–79.201.
- `stree_scalar`: 29.229–33.227.

### All equal

Source: `benchmarks/stree_equal.csv`.

| Elements | Standard | Binary | Original Eytzinger | Earlier T2 | Optimized Eytzinger | Optimized without hints | Scalar S+ |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 21.042 | 20.026 | 16.235 | 27.824 | 34.382 | 38.064 | 10.023 |
| 33,554,555 | 24.610 | 25.224 | 21.299 | 37.579 | 83.165 | 71.926 | 12.342 |

Largest-input timing ranges (ns/query):

- `std_lower_bound`: 24.080–25.584.
- `optimized_eytzinger`: 80.106–87.386.
- `stree_scalar`: 11.938–13.081.

## Interpretation

All runs completed with zero result mismatches. All 112 CSV rows passed
checks for trial counts, medians, min/max, finite positive timings, and matching
checksums within each dataset. Total measured searches: 1,568,000,000; warm-up
searches: 112,000,000.

At 33,554,555 uniform keys, scalar S+ takes 223.920 ns/query in the first
process and 222.823 ns/query in the repeat. Standard lower bound takes 627.997
and 607.456 respectively. The repeat gives approximately 2.73 times
standard-library throughput and 31.6% less time than original Eytzinger.

Optimized Eytzinger's median is 219.761 ns/query in the first process and
210.445 in the repeat. Scalar S+ is about 5.9% slower by the repeated median,
but their trial ranges overlap. These observations do not establish statistical
equivalence or a universal performance ordering. Smaller-input comparisons
and other distributions differ; use the complete tables rather than selecting
one favorable size.

At the largest duplicate-heavy size, scalar S+ takes 30.116 ns/query versus
38.842 for the standard reference and 33.544 for original Eytzinger. On all-equal
input it takes 12.342 ns/query versus 24.610 and 21.299 respectively. Repeated
paths have different cache and branch-prediction behavior from uniform input.
Hardware counters were not collected, so those mechanisms are explanations to
investigate rather than measured causes.

Largest persistent vector-buffer storage:

| Representation | Bytes | MiB |
| --- | ---: | ---: |
| Sorted keys | 134,218,220 | 128.000 |
| Eytzinger keys and original-position map | 402,654,672 | 384.001 |
| Scalar S+ nodes and level offsets | 142,607,168 | 136.001 |

The scalar tree adds about 6.25% over sorted key storage and
uses about 64.6% less storage than the mapped Eytzinger
representation. The benchmark process retains all representations plus query
and output buffers; these figures are not total resident process memory.

The largest repeated scalar construction took 143.285 ms, versus
346.946 ms for Eytzinger. Each is one observation, so
this is not a rigorous construction-time comparison. Peak construction memory
was not measured.

This is a correctness-tested scalar foundation. The next experiment replaces
the early-exit node scan with counting keys below the query, then measures
auto-vectorization. Explicit SIMD and batching remain later stages.
