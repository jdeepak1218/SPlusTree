# Counting search and auto-vectorization results

## Change

`STree::lower_bound_count` replaces the per-node early-exit scan with a fixed-size
count of keys strictly below the query. Sorted keys make that count equal to
the first qualifying position. The same operation selects internal children
and leaf positions. Equality, duplicate routing, leaf spill, full-range unsigned
keys, and the past-the-end result preserve the existing contract.

The original `lower_bound` remains available. Construction, padding, packed
levels, alignment, and persistent storage are unchanged; both methods query
the same object. There are no explicit SIMD intrinsics, batching, runtime ISA
dispatch, or additional prefetch requests. `docs/stree-counting.md` explains the
code and its mathematical equivalence with examples.

## Compiler verification

The fixed reduction is eligible for auto-vectorization, but that is not a
guarantee. Initial GCC 13.3 output vectorized the leaf but scalarized internal
reductions after early unrolling. A conditional `#pragma GCC unroll 1` preserves
the loop for the vectorizer. Other compilers see the ordinary C++ reduction.

Final standalone probe diagnostics report two vectorized scans: 16-byte vectors
for generic `-O3` and 32-byte vectors for `-O3 -mavx2`. Inspection of the internal
loop and final leaf code confirms SIMD in both locations. The actual AVX2
benchmark also reports both scans vectorized, not just initialization loops.
Generated AVX2 instructions include `vpminud`, `vpcmpeqd`, `vpcmpgtd`, `vpandn`,
`vpsubd`, and `vpaddd`, with query broadcast and horizontal reduction.

The comparison remains unsigned in C++. GCC's transformations preserve that
semantics even where signed SIMD comparison instructions appear. AVX2 handles
eight 32-bit lanes per 256-bit vector; the default node contains sixteen keys.
The generic x86-64 build uses 128-bit SIMD. A separate build disables both loop
and SLP vectorization as a control. That control applies to the full executable
and is not the fastest possible hand-tuned scalar implementation.

`benchmarks/count_codegen.cpp` provides stable symbols for repeating inspection.
Commands and compiler details are in `docs/stree-counting.md`.

## Correctness and safety

The dedicated suite passes 4,697,951 query cases across 3,777 datasets, checking
both methods against `std::lower_bound`. Generic, AVX2 release, vectorization-
disabled, and AVX2 ASan/UBSan builds pass. Existing reference and Eytzinger
checks also pass. AVX2 capability was checked before executing those binaries.

Coverage includes every length through 1,025, exhaustive short sorted arrays,
random distributions, duplicate boundaries, incomplete levels, all-zero and
all-maximum keys, values around the signed boundary, and arrays exceeding eight
million keys. Widths 1, 2, 3, 7, 8, 15, and 16 exercise small counts and vector
tails. Explicit failures remain enabled under `-DNDEBUG`.

Three temporary mutations were detected: `<=` instead of `<`, signed comparison
of unsigned keys, and overwriting instead of accumulating the count. Header
integration, repeated inclusion, and two-translation-unit checks pass. No
temporary variants are retained.

Each contribution is zero or one, with at most sixteen contributions, so the
32-bit accumulator cannot overflow and its conversion to `size_t` is exact.
Padding keys are initialized and never smaller than a valid query. No manual
vector pointer casts or loads are introduced. The original layout bounds and
logical-size clamp remain in force.

No address or undefined-behavior errors were observed in sanitizer executions.
Leak detection is disabled because it is unsupported under this environment's
tracing setup. Sanitized and optimized builds can emit different code, so both
were executed; sanitizer success is not a proof of correctness for all possible
inputs. Allocation-failure injection, concurrency, and other compilers and
architectures remain untested.

## Benchmark method

Focused comparisons use four methods: standard lower bound, optimized Eytzinger,
early-exit S+ search, and counting S+ search. Each pass contains one million
independent queries. One full warm-up precedes twelve trials; execution order
rotates so each method runs in every position three times. Medians average the
middle two sorted timings. Raw files retain all trials and min/max.

Every output is compared element by element with the reference outside the
timed regions, with checks active under `-DNDEBUG`. Timing includes search and
preallocated output writes, excluding generation, sorting, construction,
allocation, validation, and printing. Both S+ methods share the same storage
and construction-time observation. No new persistent memory is needed.

Generic and AVX2 builds run six uniform sizes through 33,554,555 keys, plus
duplicate-heavy and all-equal workloads at 1,048,576 and 33,554,555 keys. The AVX2
uniform run repeats in another process. A vectorization-disabled generic build
also runs all six uniform sizes. All runs execute sequentially with fixed seed
42; the distributions match the earlier benchmark matrix.

Total work: 1,536 million measured searches and 128 million warm-up searches.
The base build uses GCC 13.3.0 with `-std=c++20 -O3 -DNDEBUG -Wall -Wextra
-Wpedantic -Wconversion -Iinclude`. AVX2 adds `-mavx2`; the scalar control adds
`-fno-tree-loop-vectorize -fno-tree-slp-vectorize`. These flags affect all methods
in each executable, not just the counting method.

Machine: reported i7-12700H, x86-64 WSL2, two visible logical CPUs. No affinity,
fixed frequency, requested/verified huge pages, or cold-cache flushing. Host
activity affects timings. Within-build comparisons are primary; cross-build
numbers also include different code generation and process variation. Hardware
counters were not collected. These are throughput measurements, not dependent
single-query latency. Commands are in `benchmarks/README.md`.

## Timing results

All entries are median ns/query; lower is better. The last column is early-exit
time divided by counting time: values above one favor counting.

### Generic uniform

Source: `benchmarks/count_generic_uniform.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Early / count ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 58.717 | 9.299 | 40.928 | 12.055 | 3.40x |
| 65,536 | 109.128 | 18.935 | 68.636 | 19.996 | 3.43x |
| 1,048,576 | 212.245 | 70.087 | 106.411 | 33.250 | 3.20x |
| 8,388,608 | 463.398 | 160.282 | 183.720 | 86.199 | 2.13x |
| 16,777,339 | 507.601 | 184.838 | 201.198 | 106.745 | 1.88x |
| 33,554,555 | 650.708 | 222.290 | 229.744 | 141.860 | 1.62x |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 573.138–666.608.
- `optimized_eytzinger`: 216.699–227.283.
- `stree_scalar`: 219.963–234.034.
- `stree_count`: 138.693–149.574.

### Generic duplicate-heavy

Source: `benchmarks/count_generic_duplicates.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Early / count ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 36.672 | 26.595 | 25.183 | 23.589 | 1.07x |
| 33,554,555 | 40.396 | 72.152 | 30.919 | 38.528 | 0.80x |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 38.853–43.202.
- `optimized_eytzinger`: 64.228–78.225.
- `stree_scalar`: 30.111–33.684.
- `stree_count`: 37.325–42.196.

### Generic all equal

Source: `benchmarks/count_generic_equal.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Early / count ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 20.616 | 34.567 | 10.580 | 24.229 | 0.44x |
| 33,554,555 | 22.691 | 55.798 | 13.567 | 37.584 | 0.36x |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 22.091–24.056.
- `optimized_eytzinger`: 55.392–58.493.
- `stree_scalar`: 13.177–14.787.
- `stree_count`: 36.989–39.168.

### AVX2 uniform

Source: `benchmarks/count_avx2_uniform.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Early / count ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 60.309 | 9.235 | 40.005 | 9.758 | 4.10x |
| 65,536 | 109.370 | 18.667 | 68.055 | 15.261 | 4.46x |
| 1,048,576 | 213.239 | 61.931 | 105.917 | 25.731 | 4.12x |
| 8,388,608 | 443.604 | 156.915 | 176.110 | 56.453 | 3.12x |
| 16,777,339 | 496.104 | 184.143 | 196.874 | 69.731 | 2.82x |
| 33,554,555 | 615.534 | 217.814 | 225.590 | 99.974 | 2.26x |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 587.381–667.657.
- `optimized_eytzinger`: 215.091–290.719.
- `stree_scalar`: 220.650–257.450.
- `stree_count`: 94.728–111.442.

### AVX2 duplicate-heavy

Source: `benchmarks/count_avx2_duplicates.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Early / count ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 36.700 | 27.145 | 24.422 | 19.643 | 1.24x |
| 33,554,555 | 38.676 | 74.252 | 29.530 | 29.430 | 1.00x |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 37.997–39.499.
- `optimized_eytzinger`: 73.288–75.500.
- `stree_scalar`: 29.099–30.063.
- `stree_count`: 28.880–31.720.

### AVX2 all equal

Source: `benchmarks/count_avx2_equal.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Early / count ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 21.685 | 34.123 | 9.945 | 19.632 | 0.51x |
| 33,554,555 | 25.016 | 83.262 | 12.475 | 29.494 | 0.42x |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 24.434–26.831.
- `optimized_eytzinger`: 82.337–85.058.
- `stree_scalar`: 12.223–13.563.
- `stree_count`: 29.123–30.171.

### AVX2 uniform, repeat process

Source: `benchmarks/count_avx2_uniform_repeat.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Early / count ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 59.795 | 9.336 | 40.441 | 9.987 | 4.05x |
| 65,536 | 109.462 | 19.428 | 67.491 | 15.206 | 4.44x |
| 1,048,576 | 214.338 | 57.799 | 105.347 | 25.724 | 4.10x |
| 8,388,608 | 447.308 | 156.383 | 179.580 | 57.086 | 3.15x |
| 16,777,339 | 483.312 | 188.610 | 197.892 | 69.801 | 2.84x |
| 33,554,555 | 598.237 | 217.970 | 223.828 | 98.250 | 2.28x |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 554.444–616.111.
- `optimized_eytzinger`: 215.292–231.457.
- `stree_scalar`: 220.851–233.883.
- `stree_count`: 93.000–118.025.

### Vectorization-disabled uniform

Source: `benchmarks/count_no_vector_uniform.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Early / count ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 60.288 | 9.474 | 40.477 | 21.983 | 1.84x |
| 65,536 | 109.142 | 18.956 | 67.874 | 33.270 | 2.04x |
| 1,048,576 | 215.205 | 56.901 | 106.667 | 51.137 | 2.09x |
| 8,388,608 | 439.289 | 153.111 | 176.135 | 144.092 | 1.22x |
| 16,777,339 | 469.637 | 185.103 | 197.466 | 190.733 | 1.04x |
| 33,554,555 | 616.146 | 218.281 | 226.432 | 257.875 | 0.88x |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 569.583–667.828.
- `optimized_eytzinger`: 214.111–246.421.
- `stree_scalar`: 223.115–284.135.
- `stree_count`: 249.169–302.846.

## Interpretation

All runs completed with zero output mismatches. All 128 rows passed checks for
trial counts, raw-trial medians/min/max, finite positive times, matching
checksums, and identical persistent S+ storage. Total measured searches were
1,536,000,000, plus 128,000,000 warm-up searches.

At 33,554,555 uniform keys, the first AVX2 process measured 99.974 ns/query for
counting versus 225.590 for early exit. The repeat measured 98.250 versus 223.828:
about 56.1% less time, or 2.28 times throughput. The early/count trial ranges at
that size do not overlap in either AVX2 process. The repeated counting method
also delivers about 6.09 times standard-library throughput (598.237 / 98.250)
and 2.22 times optimized Eytzinger throughput (217.970 / 98.250).

The generic build measured 141.860 ns/query for counting versus 229.744 for
early exit at the largest size. Disabling vectorization measured 257.875 for
counting versus 226.432 for early exit. These observations, alongside inspected
SIMD code, show that a fixed count alone does not guarantee a win and compiler
vectorization matters for this implementation. Comparisons between builds
include all code-generation differences and process variation; they do not
isolate a single instruction or vector width's exact contribution.

On largest duplicate-heavy AVX2 input, early exit and counting were essentially
tied by median: 29.530 and 29.430 ns/query with overlapping ranges. On all-equal
input, early exit is faster: 12.475 versus 29.494. Counting always evaluates all
slots, while early exit can finish quickly on predictable paths. This explains
why the original method remains available; no automatic workload selection or
default API change is introduced. Hardware counters were not measured, so
branch-misprediction and cache effects are not separately quantified.

Largest S+ vector-buffer storage remains 142,607,168 bytes (about 136 MiB),
identical for both methods. The sorted baseline uses 134,218,220 bytes, and mapped
Eytzinger uses 402,654,672 bytes. The counting method adds no persistent buffers
or construction work. Total process memory is larger because all representations,
queries, and output buffers coexist. Peak process/temporary memory was not measured.

The next node-search experiments are mask-based position extraction using
trailing-zero counts, then population counts and explicit SIMD. Tree-level
batching and prefetching remain separate later stages. This experiment implements
only the counting reduction and verifies the compiler-generated vectorization.
