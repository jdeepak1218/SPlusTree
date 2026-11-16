# Population-count search results

## Implementation

Added `STree::lower_bound_popcount`. The node operation constructs one bit per
key strictly below the query and returns `std::popcount(mask)`. On sorted keys,
that is the first qualifying position. Zero bits returns zero; sixteen true
bits returns sixteen. No marker is added. The packed tree, separators, leaf
spill, logical-size clamp, ownership, and persistent storage remain unchanged.
All existing methods stay available; no default selection changes.

The sixteen-key AVX2 backend preserves the full unsigned range by flipping the
sign bit of query and keys before signed greater-than comparisons. It extracts
two eight-bit masks and combines them without overlap. The plain C++ bit loop
handles generic builds and other widths. `SST_DISABLE_MASK_SIMD` disables both
explicit comparison-mask backends, but the compiler may still auto-vectorize
ordinary C++. All translation units must use consistent macros and target flags.

This step retains the two-half extraction layout. It does not implement compressed
packing or two bits per key. Those remain separate experiments. Bit order is
irrelevant to population count, but unique non-overlapping bit positions remain
essential. Explanation and code-generation commands are in `docs/stree-popcount.md`.

## Correctness and safety

- Generic, AVX2+POPCNT release, AVX2 with POPCNT disabled, explicit-backend-disabled,
  and AVX2 ASan/UBSan builds pass the complete suite.
- Each build checks 4,697,951 query cases across 3,777 datasets against
  `std::lower_bound`, comparing all four S+ query methods.
- Another 51 explicit population assertions cover every lane count, empty/full
  masks, the half-vector boundary, and maximum keys. Existing mask assertions
  remain active. Release builds retain explicit failure checks.
- Cases include empty input, incomplete levels, duplicates, full-range keys,
  all-maximum data, multiple node widths, and arrays exceeding eight million keys.
- Ownership, moving, and assignment are explicitly checked for the new API.
- Mutations adding a marker, overlapping half masks, and using an untransformed
  signed comparison are detected. No temporary mutants are retained.
- Header integration, repeated inclusion, and two-translation-unit checks pass.
- Existing reference and Eytzinger suites pass. GCC builds have no warnings with
  `-Wall -Wextra -Wpedantic -Wconversion`.

AVX2 and POPCNT support was checked before executing targeted builds. The node
backend reads exactly two aligned 32-byte halves of a valid initialized node.
Other widths use bounded C++ accesses. Query sign-bit conversion is bit-preserving;
signed comparisons are applied only after the order-preserving transform.
Mask shifts are unsigned and bounded; masks have no bits above the real slots.
The population count is therefore at most the node width. Padding maximum keys
never contribute, preserving valid child indices and end-of-input handling.

ASan, UBSan with recovery disabled, and libstdc++ assertions report no errors.
Leak detection is disabled because it cannot run under the environment's tracing
setup. This is evidence for tested executions, not a proof for all inputs.
Allocation-failure injection, concurrency, other compilers, and other architectures
remain untested.

## Code generation and ISA control

The primary probe and actual benchmark contain `popcntl`. Comparisons use sign-bit
XOR, `vpcmpgtd`, and one-bit-per-lane extraction through `vmovmskps`. The software
control emits calls to `__popcountdi2` in the query instead of a POPCNT instruction.
The primary build uses explicit `-mavx2 -mpopcnt` flags. On GCC 13.3, `-mavx2`
also enables POPCNT; therefore the control explicitly adds `-mno-popcnt` after it.
The control retains AVX2 comparisons. It is not a no-SIMD build.

## Benchmark method

Six methods use the same input and million queries per pass: standard lower bound,
optimized Eytzinger, early-exit S+, counting S+, trailing-zero S+, and population-
count S+. One warm-up precedes twelve timed trials; rotating execution order
puts every method in every position twice. The median averages the middle two
sorted timings. CSVs retain all raw trials and min/max. Explicit output comparisons
run outside every timed pass, including release builds.

Uniform input uses six sizes from 1,024 through 33,554,555 keys. Generic and
AVX2+POPCNT builds run uniform, duplicate-heavy, and all-equal workloads. The
uniform hardware run repeats in another process. An AVX2 build with POPCNT
disabled also runs all uniform sizes. Distributions and seed 42 are unchanged
from the earlier comparisons. Every command executes sequentially.

Total work: 2,304 million measured searches and 192 million warm-up searches.
Timing includes query execution and preallocated output writes, excluding
generation, sorting, construction, allocation, validation, and printing. All
S+ methods share storage and the same single construction-time observation.
Buffer capacities exclude allocator metadata, object headers, inputs/outputs,
and temporary storage. All indexes coexist in the benchmark process.

GCC 13.3.0 base flags: `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic
-Wconversion -Iinclude`. The hardware build adds `-mavx2 -mpopcnt`; the software
control adds `-mavx2 -mno-popcnt`. Flags affect the entire executable, so cross-build
results also include other code-generation differences and process variation.

Environment: x86-64 WSL2, reported i7-12700H, two visible logical CPUs. No affinity,
fixed frequency, requested/verified huge pages, or cold-cache flushing. Host
activity changes timings. These are independent-query throughput measurements,
not isolated lookup latency. No hardware counters were collected. Reproduction
commands are in `benchmarks/README.md` and `tests/README.md`.

## Results

Median ns/query, lower is better. The final ratio is trailing-zero time divided
by population-count time; values above one favor population counting.

### Generic uniform

Source: `benchmarks/popcount_generic_uniform.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Trailing zeros | Popcount | CTZ / popcount ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 57.181 | 9.275 | 40.352 | 11.961 | 25.684 | 33.226 | 0.77x |
| 65,536 | 107.082 | 18.295 | 67.329 | 20.004 | 40.347 | 51.228 | 0.79x |
| 1,048,576 | 217.437 | 61.635 | 108.454 | 33.801 | 66.514 | 84.622 | 0.79x |
| 8,388,608 | 427.342 | 148.377 | 172.680 | 79.457 | 175.072 | 194.394 | 0.90x |
| 16,777,339 | 512.297 | 190.600 | 206.547 | 111.924 | 254.932 | 284.876 | 0.89x |
| 33,554,555 | 633.554 | 232.074 | 224.139 | 139.903 | 314.721 | 339.922 | 0.93x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 219.598–241.686.
- `stree_count`: 133.022–158.347.
- `stree_mask`: 287.804–336.891.
- `stree_popcount`: 311.833–477.139.

### Generic duplicate-heavy

Source: `benchmarks/popcount_generic_duplicates.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Trailing zeros | Popcount | CTZ / popcount ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 35.782 | 25.602 | 24.320 | 22.890 | 45.954 | 59.273 | 0.78x |
| 33,554,555 | 38.389 | 67.841 | 30.068 | 36.846 | 67.713 | 87.036 | 0.78x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 29.102–31.474.
- `stree_count`: 35.864–38.082.
- `stree_mask`: 65.587–79.434.
- `stree_popcount`: 86.092–87.847.

### Generic all equal

Source: `benchmarks/popcount_generic_equal.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Trailing zeros | Popcount | CTZ / popcount ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 20.248 | 33.022 | 10.115 | 22.707 | 46.231 | 59.654 | 0.77x |
| 33,554,555 | 22.141 | 54.222 | 13.202 | 37.349 | 68.037 | 87.998 | 0.77x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 12.834–13.922.
- `stree_count`: 36.169–37.817.
- `stree_mask`: 67.163–70.690.
- `stree_popcount`: 86.047–89.810.

### AVX2 and POPCNT uniform

Source: `benchmarks/popcount_avx2_uniform.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Trailing zeros | Popcount | CTZ / popcount ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 59.199 | 9.402 | 40.833 | 9.829 | 7.830 | 7.444 | 1.05x |
| 65,536 | 106.550 | 17.936 | 69.233 | 15.238 | 12.425 | 11.496 | 1.08x |
| 1,048,576 | 214.938 | 82.602 | 113.339 | 28.609 | 29.564 | 26.697 | 1.11x |
| 8,388,608 | 450.259 | 158.070 | 179.446 | 58.267 | 69.843 | 68.853 | 1.01x |
| 16,777,339 | 500.500 | 196.591 | 200.044 | 71.430 | 90.527 | 83.321 | 1.09x |
| 33,554,555 | 634.983 | 222.999 | 228.854 | 98.238 | 116.990 | 109.487 | 1.07x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 207.858–245.119.
- `stree_count`: 91.139–108.122.
- `stree_mask`: 102.542–119.703.
- `stree_popcount`: 100.506–122.015.

### AVX2 and POPCNT duplicate-heavy

Source: `benchmarks/popcount_avx2_duplicates.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Trailing zeros | Popcount | CTZ / popcount ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 36.529 | 26.679 | 24.052 | 19.304 | 15.466 | 14.892 | 1.04x |
| 33,554,555 | 38.685 | 74.114 | 29.660 | 28.991 | 25.119 | 23.031 | 1.09x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 29.271–31.244.
- `stree_count`: 28.154–30.598.
- `stree_mask`: 24.548–25.903.
- `stree_popcount`: 22.819–23.986.

### AVX2 and POPCNT all equal

Source: `benchmarks/popcount_avx2_equal.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Trailing zeros | Popcount | CTZ / popcount ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 21.746 | 34.193 | 10.016 | 19.317 | 15.162 | 14.626 | 1.04x |
| 33,554,555 | 24.561 | 83.132 | 12.573 | 29.182 | 25.256 | 23.426 | 1.08x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 12.148–13.146.
- `stree_count`: 28.550–30.187.
- `stree_mask`: 24.783–25.899.
- `stree_popcount`: 23.075–25.098.

### AVX2 and POPCNT uniform, repeat

Source: `benchmarks/popcount_avx2_uniform_repeat.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Trailing zeros | Popcount | CTZ / popcount ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 58.626 | 9.207 | 40.045 | 9.820 | 7.909 | 7.482 | 1.06x |
| 65,536 | 107.355 | 18.709 | 67.586 | 14.916 | 12.311 | 11.673 | 1.05x |
| 1,048,576 | 212.293 | 70.702 | 109.212 | 27.098 | 31.023 | 25.276 | 1.23x |
| 8,388,608 | 441.850 | 155.646 | 177.317 | 57.751 | 67.478 | 64.344 | 1.05x |
| 16,777,339 | 449.605 | 173.110 | 190.204 | 67.883 | 80.475 | 77.404 | 1.04x |
| 33,554,555 | 644.745 | 231.284 | 233.906 | 102.675 | 120.909 | 114.899 | 1.05x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 219.495–279.188.
- `stree_count`: 88.488–111.208.
- `stree_mask`: 96.788–144.674.
- `stree_popcount`: 97.468–141.434.

### AVX2 with software population count

Source: `benchmarks/popcount_software_uniform.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Trailing zeros | Popcount | CTZ / popcount ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 59.531 | 9.436 | 40.145 | 9.767 | 7.830 | 15.180 | 0.52x |
| 65,536 | 104.716 | 18.426 | 65.307 | 14.712 | 12.082 | 23.294 | 0.52x |
| 1,048,576 | 212.955 | 65.787 | 107.040 | 26.088 | 23.336 | 44.013 | 0.53x |
| 8,388,608 | 451.327 | 156.567 | 176.660 | 58.229 | 68.732 | 117.413 | 0.59x |
| 16,777,339 | 463.094 | 181.676 | 195.479 | 68.781 | 81.665 | 137.928 | 0.59x |
| 33,554,555 | 640.469 | 243.919 | 236.074 | 108.759 | 122.414 | 190.176 | 0.64x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 224.481–284.131.
- `stree_count`: 96.613–133.008.
- `stree_mask`: 103.161–165.851.
- `stree_popcount`: 184.192–227.878.

## Interpretation

All runs completed with no result mismatches. All 192 CSV rows passed validation
of trial counts, medians, min/max, finite positive timings, matching checksums,
identical persistent S+ storage, and cross-build reference checksums for the
same workload/size. Total measured searches were 2,304,000,000 plus 192,000,000
warm-up searches.

At 33,554,555 uniform keys, population counting measured 109.487 ns/query in
the first hardware process versus 116.990 for trailing zeros and 98.238 for
counting. The repeat measured 114.899, 120.909, and 102.675 respectively.
Population counting improves the observed median over trailing zeros by about
5–6%, while remaining about 11.9% slower than counting in the repeat. Trial
ranges overlap substantially; no statistical-confidence claim is made. The
repeated standard reference takes 644.745 ns/query, so population counting
provides about 5.61 times its throughput on this dataset.

At the largest duplicate-heavy size, population counting takes 23.031 ns/query
versus 25.119 for trailing zeros and 28.991 for counting. On all-equal data it
takes 23.426 versus 25.256 and 29.182, but early exit remains faster at 12.573.
The complete tables include smaller sizes and other builds; no new method is
a universal replacement or automatically chosen as the default.

Generic population counting takes 339.922 ns/query at the largest uniform
size. Retaining AVX2 comparisons but disabling hardware POPCNT gives 190.176
ns/query. The primary repeated hardware result is 114.899. Code inspection
confirms the software-helper calls, but cross-build differences also include
other generated-code changes and host variation; these numbers do not isolate
one instruction's exact cost.

This version retains the two half-mask extractions and combines them before
counting. It does not yet exploit alternative packed layouts. Removing ordering
requirements permits future packing changes, but does not by itself guarantee
that this version beats the existing SIMD reduction. Hardware counters were
not collected, so no individual microarchitectural cause is quantified.

Persistent S+ buffers remain 142,607,168 bytes (about 136 MiB) at the largest
size. The sorted baseline occupies 134,218,220 bytes and mapped Eytzinger
402,654,672 bytes. The method adds no persistent buffers or construction work.
Process and peak construction memory are larger than these capacity figures
and were not measured.

The next experiment is explicit compressed packing: combine comparison masks
into sixteen-bit lanes, extract byte-sign bits, and account for two bits per
key. That optimization remains separate from this population-count baseline.
