# Packed population-count search results

## Change

`STree::lower_bound_packed` keeps the existing packed levels, separators, leaves,
full unsigned key range, and first-index result contract. Its sixteen-key AVX2
backend compares keys below the query with the established sign-bit transform.
The 32-bit comparison outputs are packed into sixteen-bit lanes, a single byte
mask is extracted, and its population count is divided by two.

Each true comparison is all ones. Signed saturation therefore preserves it as
`0xffff`, contributing two byte-sign bits. False comparisons contribute zero.
Packing permutes lanes within the vector, but population counting is insensitive
to that permutation. Actual keys remain 32-bit and are not compressed. All
32 mask bits are retained, including bit 31 when all lanes qualify.

Generic targets, other node widths, and `SST_DISABLE_MASK_SIMD` reuse the existing
population-count helper without an extra division. No construction, persistent
storage, runtime dispatch, batching, or prefetching changes are made. Existing
query methods and defaults remain intact. `docs/stree-packed.md` explains the
code, mask layout, safety properties, and worked examples.

## Correctness and code generation

- 4,697,951 differential query cases across 3,777 datasets pass, checking all
  five S+ methods against `std::lower_bound`.
- Another 51 packed assertions cover all seventeen possible qualifying-lane
  counts, zero/full masks, half-vector boundaries, and maximum unsigned keys.
  Existing trailing-zero and population assertions remain active.
- Generic optimized, AVX2/POPCNT release, AVX2 software-population-count,
  explicit-backend-disabled, and AVX2 sanitizer builds pass.
- Coverage includes empty arrays, incomplete nodes and levels, duplicate runs,
  integer extremes, several node widths, and arrays exceeding eight million keys.
- Existing ownership/copy/move checks and reference/Eytzinger suites pass.
- Three temporary mutations are detected: missing division by two, unsigned
  saturation, and loss of bit 31. No mutants are retained.
- Standalone-header, repeated-inclusion, and two-translation-unit execution pass.
- GCC builds complete without warnings, including `-Wconversion`.

ASan, UBSan with recovery disabled, and libstdc++ assertions report no errors.
Leak detection is disabled because it is unsupported under this environment's
tracing setup. No claim is made that these tests prove correctness for every
input; allocation failure, concurrency, other compilers and architectures remain
untested.

Both vector loads are aligned and stay inside one initialized 64-byte node.
The vector backend is excluded at compile time for smaller arrays. Comparison
masks contain only zero or minus-one lanes, so signed packing loses no truth
values. The 32-bit byte mask has exactly twice as many set bits as qualifying
keys. Its even population count is at most 32, and division gives an index from
zero through sixteen. Padding never contributes. Existing traversal bounds,
leaf spill, and logical-size clamping are preserved.

Probe and actual benchmark disassembly contain `vpackssdw` and `vpmovmskb`.
The hardware query uses one byte-mask extraction, `popcntl`, and a right shift
at each node scan. Two separate extracts and their shift/OR combination are
gone. Division remains as a shift on this compiler; it is not claimed to have
disappeared. The software control disables POPCNT while retaining AVX2.

## Benchmark method

Focused comparisons include six methods: standard lower bound, optimized
Eytzinger, early-exit S+, counting S+, unpacked population-count S+, and packed
population-count S+. Each uses identical inputs and one million queries per
pass, one warm-up, and twelve timed trials. Rotation gives every method every
execution position twice. Median is the mean of the middle two sorted timings;
CSVs retain all raw trials and min/max. Every output is checked against the
standard reference outside timed regions, including release builds.

Uniform runs use six sizes through 33,554,555 keys. The hardware build also runs
duplicate-heavy and all-equal workloads at 1,048,576 and 33,554,555 keys. Its full
uniform run repeats in another process. Generic and hardware-population-count-
disabled controls run all uniform sizes. Distributions and seed 42 are unchanged.
Six runs execute sequentially: 2,016 million measured searches and 168 million
warm-up searches. Older trailing-zero comparisons are not retimed in this mode.

Timing includes searches and preallocated result writes, excluding generation,
sorting, construction, allocation, validation, and printing. All S+ methods share
their index buffers and a single construction-time observation. Input, indexes,
queries and outputs coexist in the process. Buffer figures exclude allocator
metadata, object headers, and temporary storage; peak process memory is not measured.

GCC 13.3.0 base flags: `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic
-Wconversion -Iinclude`. Hardware adds `-mavx2 -mpopcnt`; the software control adds
`-mavx2 -mno-popcnt`. Generic search reuses the ordinary population-count path.
Flags affect the entire executable. Cross-build comparisons include other code
generation changes and process variation; within-build comparisons are primary.

Environment: x86-64 WSL2, reported i7-12700H, two visible logical CPUs. No affinity,
fixed frequency, requested/verified huge pages, or cold-cache flushing. Host
activity affects elapsed times. These are independent-query throughput timings,
not isolated lookup latency. Hardware counters were not collected. Commands are
in `benchmarks/README.md` and `tests/README.md`.

## Results

Median ns/query, lower is better. The final ratio is unpacked-population time
divided by packed time: values above one favor packing.

### Generic uniform

Source: `benchmarks/packed_generic_uniform.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Unpacked popcount | Packed | Unpacked / packed ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 58.431 | 9.266 | 40.194 | 11.777 | 33.382 | 33.332 | 1.00x |
| 65,536 | 105.490 | 19.004 | 66.230 | 19.037 | 50.545 | 50.113 | 1.01x |
| 1,048,576 | 209.680 | 58.666 | 106.729 | 32.816 | 82.426 | 80.198 | 1.03x |
| 8,388,608 | 429.573 | 154.521 | 173.141 | 84.159 | 200.485 | 200.810 | 1.00x |
| 16,777,339 | 465.181 | 180.061 | 193.238 | 104.750 | 242.669 | 242.410 | 1.00x |
| 33,554,555 | 594.683 | 211.893 | 217.842 | 138.574 | 320.172 | 314.735 | 1.02x |

Largest-input trial ranges (ns/query):

- `stree_count`: 127.002–146.230.
- `stree_popcount`: 296.826–367.668.
- `stree_packed`: 288.429–339.254.

### AVX2 and POPCNT uniform

Source: `benchmarks/packed_avx2_uniform.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Unpacked popcount | Packed | Unpacked / packed ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 57.187 | 9.111 | 39.176 | 9.134 | 7.486 | 6.639 | 1.13x |
| 65,536 | 106.046 | 18.761 | 65.819 | 14.882 | 11.368 | 10.913 | 1.04x |
| 1,048,576 | 204.511 | 58.002 | 102.827 | 24.049 | 20.425 | 18.112 | 1.13x |
| 8,388,608 | 434.472 | 153.296 | 172.308 | 56.034 | 63.038 | 54.926 | 1.15x |
| 16,777,339 | 479.534 | 181.487 | 195.246 | 66.370 | 75.025 | 65.937 | 1.14x |
| 33,554,555 | 611.061 | 215.304 | 217.853 | 96.236 | 106.352 | 98.458 | 1.08x |

Largest-input trial ranges (ns/query):

- `stree_count`: 82.335–107.403.
- `stree_popcount`: 95.164–111.156.
- `stree_packed`: 88.592–113.254.

### AVX2 duplicate-heavy

Source: `benchmarks/packed_avx2_duplicates.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Unpacked popcount | Packed | Unpacked / packed ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 36.350 | 26.254 | 23.953 | 18.842 | 14.183 | 13.572 | 1.05x |
| 33,554,555 | 37.832 | 69.874 | 28.887 | 28.124 | 23.063 | 21.857 | 1.06x |

Largest-input trial ranges (ns/query):

- `stree_count`: 27.831–29.770.
- `stree_popcount`: 22.687–24.419.
- `stree_packed`: 21.592–24.605.

### AVX2 all equal

Source: `benchmarks/packed_avx2_equal.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Unpacked popcount | Packed | Unpacked / packed ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 19.948 | 33.157 | 9.788 | 18.773 | 14.165 | 13.665 | 1.04x |
| 33,554,555 | 22.027 | 57.852 | 12.177 | 27.920 | 23.003 | 22.272 | 1.03x |

Largest-input trial ranges (ns/query):

- `stree_count`: 27.496–29.705.
- `stree_popcount`: 22.506–24.210.
- `stree_packed`: 21.610–23.022.

### AVX2 uniform, repeat process

Source: `benchmarks/packed_avx2_uniform_repeat.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Unpacked popcount | Packed | Unpacked / packed ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 56.959 | 9.224 | 39.225 | 9.283 | 7.466 | 6.688 | 1.12x |
| 65,536 | 106.535 | 19.161 | 65.330 | 14.862 | 11.539 | 10.821 | 1.07x |
| 1,048,576 | 206.345 | 59.814 | 104.462 | 24.642 | 21.382 | 19.435 | 1.10x |
| 8,388,608 | 439.616 | 151.562 | 171.722 | 54.751 | 62.566 | 55.672 | 1.12x |
| 16,777,339 | 472.364 | 181.778 | 194.698 | 65.710 | 79.442 | 67.669 | 1.17x |
| 33,554,555 | 660.722 | 239.860 | 233.900 | 101.143 | 117.664 | 106.477 | 1.11x |

Largest-input trial ranges (ns/query):

- `stree_count`: 92.200–120.926.
- `stree_popcount`: 101.996–133.035.
- `stree_packed`: 91.578–120.680.

### AVX2 with software population count

Source: `benchmarks/packed_software_uniform.csv`.

| Elements | Standard | Eytzinger | Early exit | Counting | Unpacked popcount | Packed | Unpacked / packed ratio |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 57.799 | 9.271 | 39.609 | 9.279 | 14.976 | 14.740 | 1.02x |
| 65,536 | 108.027 | 19.526 | 68.188 | 14.864 | 24.374 | 23.534 | 1.04x |
| 1,048,576 | 207.994 | 67.938 | 111.660 | 26.187 | 46.171 | 43.963 | 1.05x |
| 8,388,608 | 437.321 | 153.384 | 174.072 | 56.839 | 116.316 | 112.163 | 1.04x |
| 16,777,339 | 466.535 | 180.268 | 196.052 | 66.094 | 133.669 | 130.597 | 1.02x |
| 33,554,555 | 641.280 | 225.739 | 232.817 | 99.199 | 181.294 | 181.274 | 1.00x |

Largest-input trial ranges (ns/query):

- `stree_count`: 92.690–138.649.
- `stree_popcount`: 173.800–202.425.
- `stree_packed`: 164.726–244.007.

## Interpretation

Every run completed with no output mismatch. All 168 CSV rows passed checks for
trial counts, medians, min/max, finite positive timings, within-dataset checksums,
identical S+ storage, and cross-build reference checksums for the same workload
and size. Total work was 2,016,000,000 measured searches and 168,000,000 warm-ups.

At 33,554,555 uniform keys, packed search measured 98.458 ns/query in the first
hardware process versus 106.352 for unpacked population counting, about 7.4%
less time. The repeat measured 106.477 versus 117.664, about 9.5% less time.
Trial ranges overlap, so these are observed median improvements, not a statistical
confidence claim. Counting measured 96.236 in the first run and 101.143 in the
repeat. The packed repeated median is about 5.3% higher than counting, again
with overlapping ranges. No universal replacement is selected.

In the repeated largest uniform case, packed search delivers about 6.21 times
standard-library throughput (660.722 / 106.477) and 2.20 times early-exit S+
throughput (233.900 / 106.477). Compare methods within this run rather than
mixing historical medians from different builds or host conditions.

On largest duplicate-heavy input, packed search takes 21.857 ns/query versus
23.063 for unpacked population count and 28.124 for counting. On all-equal input,
it takes 22.272 versus 23.003 and 27.920, while early exit is faster at 12.177.
The full tables retain small-input and control results, including regressions
and near ties, rather than only favorable cases.

The generic implementation delegates to the existing population helper; its
largest medians are 314.721 and 320.172 ns/query for the two public entry points.
These are not independent algorithms in that configuration, and the small
variation is not evidence of a packing benefit without the packed backend.
With AVX2 but software population counting, packed and unpacked are essentially
tied by median: 181.274 and 181.294 ns/query. The flags affect the entire
executable; cross-build changes also include host and code-generation variation.

Packing changes instruction dependencies as well as instruction count. The
observed sequence uses a packed byte extraction and retains a divide-by-two
shift; it should not be described as a free or guaranteed improvement. Hardware
counters were not collected, so individual bottlenecks are not quantified.

Persistent S+ vector buffers remain 142,607,168 bytes (about 136 MiB) at the
largest size, versus 134,218,220 bytes for sorted keys and 402,654,672 for mapped
Eytzinger. No persistent or per-query buffers or construction work are added.
These figures exclude the rest of the benchmark process and peak memory.

The node-search stage now has scalar, auto-vectorized, ordered-mask, unpacked
population-count, and packed population-count comparisons. The next main
experiment is processing independent queries in batches to overlap traversal
and memory accesses; it remains separate from this node-level change.
