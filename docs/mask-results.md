# Ordered-mask search results

## Implementation

Added `STree::lower_bound_mask`, preserving the existing packed levels, right
subtree minimum separators, leaves, full unsigned 32-bit domain, index results,
and logical-size clamp. Existing early-exit and counting methods remain intact.
The new operation compares keys with `>= query`, extracts an ordered candidate
mask, adds an end marker, and uses `std::countr_zero` for the first qualifying
position. All methods share the same index and persistent storage.

C++20 does not provide the portable SIMD mask interface used by some other
languages. The implementation therefore supplies a small AVX2 backend for the
default sixteen-key node and a plain C++ bit-loop fallback. AVX2 uses two eight-
lane unsigned-minimum/equality comparisons and two one-bit-per-lane extracts.
The upper eight bits are shifted by eight before combining. A marker at bit 16
returns sixteen when no real lane matches. No 31-bit signed-key restriction,
runtime dispatch, batching, or new prefetch requests are introduced.

`SST_DISABLE_MASK_SIMD` selects the portable loop even in an AVX2 build. The
compiler may still auto-vectorize that loop; this control does not disable all
SIMD. Non-sixteen-key nodes always use the portable path. All translation units
must use consistent feature macros and target flags.

Code, worked examples, safety reasoning, and compiler-inspection commands are
in `docs/stree-mask.md`. Default `lower_bound` and `lower_bound_count` behavior
are unchanged; no method is selected automatically from the input distribution.

## Correctness and safety

- The complete suite checks early exit, counting, and mask search against
  `std::lower_bound`: 4,697,951 query cases across 3,777 datasets per build.
- Another 51 explicit assertions cover all seventeen possible first-match
  positions in a sixteen-key node, including the no-match marker, the 7/8
  boundary between vectors, all-match input, and maximum unsigned keys.
- Generic, AVX2 release, explicit-backend-disabled, and AVX2 sanitizer builds
  pass. Existing reference and Eytzinger tests pass as well.
- Cases include empty input, singletons, duplicates across leaves/subtrees,
  incomplete levels, padding, signed-boundary values, maximum-only data, and
  arrays exceeding eight million keys. Widths 1, 2, 3, 7, 8, 15, and 16 are checked.
- Source ownership and the existing copy/move/assignment checks pass. Header
  integration, repeated inclusion, and two-translation-unit execution pass.
- Three temporary mutations are caught: wrong upper-half placement, a missing
  marker, and signed instead of unsigned SIMD minimum. No mutants are retained.
- GCC builds complete without warnings, including `-Wconversion`.

The explicit backend loads exactly two 32-byte portions of a valid, initialized
64-byte node. Their addresses are aligned to at least 32 bytes. Smaller arrays
are excluded from these loads at compile time. Register casts introduce no
floating-point arithmetic or memory aliasing casts to float arrays. Real mask
shifts range from zero to fifteen; the end-marker shift is at most sixteen.
The marker guarantees a nonzero input and a first-set position no greater than
the node width. Child routing and leaf spill retain their established bounds.

ASan, UBSan with recovery disabled, and libstdc++ assertions report no errors in
tested executions. Leak detection is disabled because the environment does not
support LeakSanitizer under tracing. Sanitizer success is not proof for every
possible input. Other compiler/architecture combinations, allocation failure,
and concurrent execution remain untested.

## Code generation

Inspection of `stree_mask_lookup` shows the expected operations in both internal
nodes and the leaf: `vpminud`, `vpcmpeqd`, `vmovmskps`, shift/OR combination, the
`0x10000` marker, and `rep bsfl`. The actual benchmark binary also contains the
mask extracts. The bit-scan input is nonzero; the build does not request a
separate BMI ISA flag. The AVX2 backend requires AVX2 CPU/OS support, which was
validated by running the AVX2 correctness suite before timing.

## Benchmark method

Five methods use identical input and queries within each process. One million
independent queries per pass, one warm-up, ten measured trials, and rotated
execution order give each method every position twice. The median averages
the middle two sorted timings. Raw files retain all ten trials and min/max.
Every output is checked element by element against the standard reference
outside the timed region, with explicit checks active in release builds.

Uniform runs use six sizes from 1,024 through 33,554,555 keys. Duplicate-heavy
and all-equal runs use 1,048,576 and 33,554,555 keys. Generic and AVX2 builds both
run all three workloads. The AVX2 uniform run repeats in a separate process,
and an AVX2 build with the explicit mask backend disabled runs all uniform sizes.
Generation uses seed 42 and the same distributions as earlier comparisons.

All eight runs execute sequentially: 1,600 million measured searches and 160
million warm-up searches. Timings include search and preallocated result writes,
excluding generation, sorting, construction, allocation, validation, and printing.
All S+ methods share their storage and construction-time observation. Storage
figures exclude metadata, temporary buffers, queries, and outputs; the process
retains the input and both index representations for validation.

GCC 13.3.0 base flags: `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic
-Wconversion -Iinclude`. AVX2 adds `-mavx2`; the backend control also adds
`-DSST_DISABLE_MASK_SIMD`. Flags affect the entire executable. Within-build
comparisons are primary; cross-build results also include code-generation
differences and separate-process variation.

Environment: x86-64 WSL2, reported i7-12700H, two visible logical CPUs. No
affinity, fixed frequency, requested/verified huge pages, or cold-cache flushing.
Host activity affects wall time. These are independent-query throughput
measurements, not isolated lookup latency. No hardware counters were collected.
Commands are in `benchmarks/README.md` and `tests/README.md`.

## Results

Median ns/query, lower is better. The last column is counting time divided by
mask time: values above one favor the mask method.

### Generic uniform

Source: `benchmarks/mask_generic_uniform.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Mask | Count / mask ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 59.475 | 9.352 | 40.850 | 12.101 | 26.345 | 0.46x |
| 65,536 | 107.961 | 19.002 | 66.841 | 20.144 | 41.137 | 0.49x |
| 1,048,576 | 209.157 | 60.003 | 103.386 | 32.276 | 62.637 | 0.52x |
| 8,388,608 | 429.062 | 146.223 | 173.015 | 82.855 | 177.545 | 0.47x |
| 16,777,339 | 474.309 | 181.963 | 196.033 | 103.929 | 239.645 | 0.43x |
| 33,554,555 | 620.508 | 219.448 | 219.819 | 133.669 | 293.755 | 0.46x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 198.560–261.578.
- `stree_count`: 121.306–159.445.
- `stree_mask`: 268.040–375.189.

### Generic duplicate-heavy

Source: `benchmarks/mask_generic_duplicates.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Mask | Count / mask ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 35.245 | 26.411 | 23.891 | 23.066 | 46.844 | 0.49x |
| 33,554,555 | 38.298 | 79.614 | 29.111 | 37.122 | 68.018 | 0.55x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 28.712–30.129.
- `stree_count`: 36.331–38.042.
- `stree_mask`: 67.370–69.106.

### Generic all equal

Source: `benchmarks/mask_generic_equal.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Mask | Count / mask ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 22.377 | 34.864 | 10.270 | 24.157 | 48.931 | 0.49x |
| 33,554,555 | 24.930 | 101.100 | 12.663 | 39.109 | 70.787 | 0.55x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 12.260–12.878.
- `stree_count`: 37.915–40.095.
- `stree_mask`: 69.065–73.099.

### AVX2 uniform

Source: `benchmarks/mask_avx2_uniform.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Mask | Count / mask ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 57.709 | 9.232 | 40.638 | 9.609 | 7.667 | 1.25x |
| 65,536 | 106.694 | 18.698 | 67.363 | 14.787 | 11.964 | 1.24x |
| 1,048,576 | 203.695 | 63.312 | 103.780 | 24.633 | 21.771 | 1.13x |
| 8,388,608 | 444.044 | 154.432 | 177.397 | 58.148 | 66.367 | 0.88x |
| 16,777,339 | 487.333 | 182.572 | 198.290 | 67.038 | 79.535 | 0.84x |
| 33,554,555 | 622.736 | 208.388 | 219.020 | 92.981 | 106.096 | 0.88x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 213.379–228.683.
- `stree_count`: 87.085–106.706.
- `stree_mask`: 101.404–114.180.

### AVX2 duplicate-heavy

Source: `benchmarks/mask_avx2_duplicates.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Mask | Count / mask ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 36.028 | 26.336 | 24.239 | 19.320 | 15.032 | 1.29x |
| 33,554,555 | 38.988 | 72.856 | 30.288 | 29.211 | 25.693 | 1.14x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 29.449–30.990.
- `stree_count`: 28.192–30.235.
- `stree_mask`: 24.892–26.625.

### AVX2 all equal

Source: `benchmarks/mask_avx2_equal.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Mask | Count / mask ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 19.081 | 33.071 | 9.825 | 18.940 | 14.931 | 1.27x |
| 33,554,555 | 22.603 | 54.441 | 12.966 | 28.638 | 25.256 | 1.13x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 12.559–14.515.
- `stree_count`: 28.330–30.075.
- `stree_mask`: 24.459–26.521.

### AVX2 uniform, repeat process

Source: `benchmarks/mask_avx2_uniform_repeat.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Mask | Count / mask ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 59.131 | 9.479 | 41.242 | 9.780 | 7.813 | 1.25x |
| 65,536 | 107.846 | 19.213 | 67.331 | 15.271 | 12.147 | 1.26x |
| 1,048,576 | 207.629 | 79.510 | 107.280 | 28.333 | 28.224 | 1.00x |
| 8,388,608 | 421.551 | 149.532 | 173.335 | 54.202 | 64.205 | 0.84x |
| 16,777,339 | 491.943 | 184.955 | 201.533 | 73.211 | 88.490 | 0.83x |
| 33,554,555 | 632.100 | 238.844 | 217.750 | 96.306 | 108.480 | 0.89x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 210.085–290.559.
- `stree_count`: 93.398–104.741.
- `stree_mask`: 100.452–124.567.

### AVX2 portable mask path

Source: `benchmarks/mask_fallback_avx2_uniform.csv`.

| Elements | Standard | Optimized Eytzinger | Early exit | Counting | Mask | Count / mask ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 59.185 | 9.512 | 40.657 | 9.606 | 25.868 | 0.37x |
| 65,536 | 105.983 | 18.303 | 67.428 | 14.937 | 40.468 | 0.37x |
| 1,048,576 | 210.420 | 72.632 | 108.078 | 26.207 | 68.519 | 0.38x |
| 8,388,608 | 439.175 | 153.278 | 175.688 | 56.652 | 175.843 | 0.32x |
| 16,777,339 | 456.807 | 174.708 | 191.621 | 64.357 | 211.658 | 0.30x |
| 33,554,555 | 585.096 | 208.110 | 222.151 | 92.282 | 289.817 | 0.32x |

Largest-input trial ranges (ns/query):

- `stree_scalar`: 215.138–228.119.
- `stree_count`: 88.665–99.754.
- `stree_mask`: 283.354–317.673.

## Interpretation

Every run completed without output mismatches. All 160 CSV rows passed checks
for trial counts, finite positive times, medians, min/max, within-dataset
checksums, identical S+ storage, and matching reference checksums across builds
for the same workload/size. Total work was 1,600,000,000 measured searches plus
160,000,000 warm-up searches.

On 33,554,555 uniform keys, AVX2 mask search took 106.096 ns/query in the first
process versus 92.981 for counting. The repeat measured 108.480 versus 96.306.
The mask median was about 12.6% higher in the repeat. This repeats the direction
of the observed regression, but trial ranges overlap; no statistical-confidence
claim is made. In the repeat, early exit took 217.750 and the standard reference
632.100 ns/query. Mask search still delivers about 5.83 times standard throughput,
while counting remains the better median in this workload.

For largest duplicate-heavy AVX2 input, mask search took 25.693 ns/query versus
29.211 for counting and 30.288 for early exit: about 12% less time than counting.
On all-equal input it took 25.256 versus 28.638 for counting, but early exit
remained faster at 12.966. This method is therefore kept as an explicit option,
not selected as the default or described as a universal improvement.

The generic portable mask loop took 293.755 ns/query on the largest uniform
input, versus 133.669 for counting in that process. The AVX2 build with the
explicit mask backend disabled measured 289.817 versus 92.282 for counting.
The ordinary bit-loop path is correct but substantially slower in this tested
configuration. The control can still use compiler auto-vectorization; it is not
a global scalar/no-SIMD build. Cross-build differences include code-generation
and host variation and do not isolate one instruction's cost.

Mask extraction, lane-order preservation, and the dependent final bit scan have
costs, while the counting implementation already has an efficient vector
reduction. These are plausible contributors to the observed tradeoff; hardware
counters were not collected, so no individual mechanism is quantified. Fewer
source operations or replacing a sum with a bit scan does not guarantee a win.

All three S+ methods use 142,607,168 bytes of vector buffers (about 136 MiB) at
the largest size, versus 134,218,220 sorted input bytes and 402,654,672 mapped
Eytzinger bytes. The new query method adds no persistent buffers or construction
work. The process retains all indexes, inputs, queries, and outputs, so these
numbers are not total resident process memory. Peak memory was not measured.

The next node-search experiment uses population counts. It changes the mask to
represent keys below the query and counts set bits, so lane order is no longer
needed for position extraction. That experiment remains separate from this
ordered-first-set implementation and must be measured on the same workloads.
