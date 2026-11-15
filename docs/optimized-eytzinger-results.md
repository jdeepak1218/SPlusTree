# Optimized Eytzinger results

## What changed

- Added `lower_bound_optimized` (prefetched), `lower_bound_optimized_no_prefetch`, and
  `search_value_optimized` to `include/eytzinger.hpp`.
- Child selection is `2 * node + (query > keys[node])`.
- A separate initial loop performs four-level lookahead. The remaining steps
  run in a second loop with no prefetch requests.
- Prefetch happens after the child update, using
  a single locality-3/T0 hint.
- The answer is recovered after traversal by removing trailing one bits and the
  preceding zero bit from the exit index.
- Index-returning wrappers perform one original-position-map lookup after that
  recovery. They no longer read the map repeatedly while tracking candidates.
- The value-returning wrapper reads the recovered key or returns `UINT32_MAX`.
  The value/sentinel contract includes an ambiguity
  between a real maximum key and an absent lower bound.
- Key buffers are now explicitly 64-byte aligned with matching aligned
  allocation/deallocation. All old and new methods in each comparison share
  that same allocation, so the main timing comparison does not mix alignments.
- Original candidate-tracking methods remain available for comparison.

## Necessary C++ safety adaptations

The traversal and answer recovery must preserve our input contract:

1. The prefetch target is calculated after the node update. Near the
   bottom it can exceed the logical array length. The implementation skips such
   hints before forming the pointer. This adds a bound check to the prefetched loop.
2. Loop guards use division (`node <= n / 16`) instead of multiplying in the
   condition. They are equivalent to `16 * node < len` for valid indices, while
   avoiding overflow. Only the valid hint target is multiplied by 16.
3. Construction limits ensure `2 * node + 1` is representable for every visited
   node. Child indices may be beyond the logical array only as integers; those
   indices are never dereferenced.
4. Recovery guards a shift equal to or larger than the C++ word width. Such
   shifts are undefined in C++.
5. Empty input, duplicates, full-range unsigned keys, and index-based absence
   remain supported. Sorted input is checked during construction.

The key buffer is aligned, but huge-page allocation is neither explicitly
requested nor verified. The value wrapper does not access the position map,
though the object retains that map to support the index wrappers.

## Verification

- Existing reference checks pass with both new index methods included.
- The dedicated suite passes 867,795 query cases across 1,391 datasets. Each
  query checks the original methods, both optimized index methods, the optimized value
  method, and binary search against the standard reference.
- Coverage includes empty/singleton input, all small tree lengths through 513,
  incomplete levels, full-range unsigned values, duplicates across subtree
  boundaries, all-zero/all-maximum arrays, seeded randomized inputs, and arrays
  containing 1,000,003 and 8,388,731 keys.
- Unsorted inputs are rejected; ownership, copying, moving, assignment, and
  several allocation alignment sizes are checked.
- AddressSanitizer, UndefinedBehaviorSanitizer with recovery disabled, and
  libstdc++ assertions pass on the final port. Leak detection is disabled because
  it is unsupported under the environment's tracing setup.
- The same suite passes in a release build with hints disabled.
- Standalone-header, repeated-inclusion, and two-translation-unit link/run checks
  pass. GCC builds complete without warnings, including `-Wconversion`.
- Disassembly contains `prefetcht0` and `setb` instructions for the optimized
  traversal. C++ comparison expressions do not themselves guarantee branchless
  machine code; the generated code was inspected on this toolchain.

No address or undefined-behavior errors were observed in tested executions.
Allocation-failure injection, physically unallocatable inputs, concurrency,
other compilers, and other architectures remain untested.

## Benchmark method and environment

Index mode compares six methods with one million queries per pass, one warm-up,
and twelve measured trials. Execution order rotates so every method occupies
each position twice. Value mode compares two value-returning methods separately,
alternating order over twelve trials. The median averages the middle two sorted
timings; raw CSVs retain every trial and min/max.

Uniform input uses six sizes from 1,024 through 33,554,555 keys. The complete
uniform index run is repeated in a separate process. Duplicate-heavy and
all-equal workloads use 1,048,576 and 33,554,555 keys. All input generation is
seeded; input/query distributions are described in `benchmarks/README.md`.

Every warm-up and measured output is compared element by element against the
reference outside the timing region, including release builds. Timings include
result-buffer writes and exclude allocation, generation, sorting, construction,
validation, and printing. Value-mode results also use `size_t` output slots for
both methods so output width is the same. All runs execute sequentially.

Environment: GCC 13.3.0, `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic
-Wconversion -Iinclude`, x86-64 WSL2 Linux on a reported i7-12700H with two visible
logical CPUs. No CPU affinity, fixed frequency, explicit huge pages, or
cold-cache flushing is configured. Host activity and virtualization affect
absolute timings. These are independent-query throughput measurements, not
dependent single-query latency or a guarantee for other machines.

Compare methods within each current run. Historical CSVs used different
alignment, method sets, and trial counts.

## Results

All timings are median ns/query; lower is better. Index and value results have
different contracts and are reported separately.

### Uniform indices, first process

Source: `benchmarks/optimized_uniform.csv`.

| Elements | Standard | Binary | Original Eytzinger | Earlier T2 | optimized without hints | optimized T0 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 58.755 | 59.302 | 49.746 | 55.710 | 8.779 | 9.195 |
| 65,536 | 106.936 | 106.795 | 85.365 | 95.539 | 20.985 | 17.974 |
| 1,048,576 | 224.491 | 218.708 | 130.994 | 145.949 | 84.757 | 62.663 |
| 8,388,608 | 437.110 | 418.132 | 242.570 | 271.088 | 264.583 | 150.175 |
| 16,777,339 | 507.064 | 479.733 | 284.227 | 320.469 | 362.254 | 178.448 |
| 33,554,555 | 584.864 | 546.978 | 325.704 | 365.227 | 513.942 | 211.031 |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 552.872–694.404.
- `binary_lower_bound`: 515.078–639.539.
- `eytzinger`: 289.014–330.916.
- `eytzinger_prefetch`: 343.336–398.302.
- `optimized_eytzinger_no_prefetch`: 427.266–620.147.
- `optimized_eytzinger`: 200.161–324.785.

### Uniform indices, repeat process

Source: `benchmarks/optimized_uniform_repeat.csv`.

| Elements | Standard | Binary | Original Eytzinger | Earlier T2 | optimized without hints | optimized T0 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 58.833 | 60.308 | 50.155 | 55.861 | 9.029 | 9.233 |
| 65,536 | 107.771 | 106.713 | 85.482 | 95.463 | 20.623 | 18.086 |
| 1,048,576 | 232.532 | 232.717 | 126.403 | 130.970 | 66.956 | 42.790 |
| 8,388,608 | 435.343 | 415.608 | 244.131 | 269.900 | 269.715 | 149.681 |
| 16,777,339 | 465.891 | 459.340 | 284.552 | 316.474 | 364.335 | 180.933 |
| 33,554,555 | 644.963 | 586.282 | 321.845 | 358.541 | 483.262 | 202.618 |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 584.819–686.984.
- `binary_lower_bound`: 492.326–627.080.
- `eytzinger`: 288.949–330.139.
- `eytzinger_prefetch`: 331.326–368.159.
- `optimized_eytzinger_no_prefetch`: 465.853–513.935.
- `optimized_eytzinger`: 199.491–210.057.

### Duplicate-heavy indices

Source: `benchmarks/optimized_duplicates.csv`.

| Elements | Standard | Binary | Original Eytzinger | Earlier T2 | optimized without hints | optimized T0 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 35.451 | 38.035 | 27.451 | 35.972 | 32.599 | 26.885 |
| 33,554,555 | 39.007 | 39.776 | 32.820 | 43.789 | 56.483 | 76.917 |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 38.367–44.052.
- `binary_lower_bound`: 38.640–42.684.
- `eytzinger`: 32.313–34.076.
- `eytzinger_prefetch`: 43.299–44.689.
- `optimized_eytzinger_no_prefetch`: 54.663–60.307.
- `optimized_eytzinger`: 76.111–86.842.

### All-equal indices

Source: `benchmarks/optimized_equal.csv`.

| Elements | Standard | Binary | Original Eytzinger | Earlier T2 | optimized without hints | optimized T0 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 19.981 | 20.057 | 16.017 | 27.453 | 36.457 | 34.533 |
| 33,554,555 | 24.410 | 25.053 | 20.757 | 36.579 | 69.574 | 82.646 |

Largest-input trial ranges (ns/query):

- `std_lower_bound`: 23.928–26.177.
- `binary_lower_bound`: 24.637–26.443.
- `eytzinger`: 20.349–21.654.
- `eytzinger_prefetch`: 36.249–38.974.
- `optimized_eytzinger_no_prefetch`: 68.666–71.553.
- `optimized_eytzinger`: 81.392–87.376.

### Uniform values

Source: `benchmarks/optimized_values_uniform.csv`.

| Elements | Standard value | optimized value | Standard / optimized throughput ratio |
| ---: | ---: | ---: | ---: |
| 1,024 | 56.558 | 9.248 | 6.12x |
| 65,536 | 107.867 | 17.028 | 6.33x |
| 1,048,576 | 215.397 | 31.683 | 6.80x |
| 8,388,608 | 446.239 | 79.483 | 5.61x |
| 16,777,339 | 491.668 | 95.321 | 5.16x |
| 33,554,555 | 658.009 | 132.317 | 4.97x |

Largest-input trial ranges (ns/query):

- `std_lower_bound_value`: 596.143–700.095.
- `optimized_eytzinger_value`: 124.399–142.748.

### Duplicate-heavy values

Source: `benchmarks/optimized_values_duplicates.csv`.

| Elements | Standard value | optimized value | Standard / optimized throughput ratio |
| ---: | ---: | ---: | ---: |
| 1,048,576 | 35.952 | 26.787 | 1.34x |
| 33,554,555 | 38.150 | 66.478 | 0.57x |

Largest-input trial ranges (ns/query):

- `std_lower_bound_value`: 37.466–39.840.
- `optimized_eytzinger_value`: 58.484–70.888.

### All-equal values

Source: `benchmarks/optimized_values_equal.csv`.

| Elements | Standard value | optimized value | Standard / optimized throughput ratio |
| ---: | ---: | ---: | ---: |
| 1,048,576 | 21.161 | 34.626 | 0.61x |
| 33,554,555 | 24.148 | 54.257 | 0.45x |

Largest-input trial ranges (ns/query):

- `std_lower_bound_value`: 23.724–25.118.
- `optimized_eytzinger_value`: 53.576–55.618.

## Interpretation

All runs completed with zero output mismatches. CSV validation checked all 116
rows for consistent trial counts, finite positive timings, medians, min/max, and
matching checksums within each dataset/result contract. Total work was 1,392
million measured searches plus 116 million warm-up searches.

The faithful traversal port makes a substantial difference on large uniform
input. At 33,554,555 keys, the optimized T0 index method used 211.031 ns/query in the
first process and 202.618 ns/query in the repeat, versus 325.704 and 321.845 for
our original Eytzinger. That is approximately 35–37% less time per query. In the
repeat it delivers 3.18 times standard-library throughput.

Comparing the two optimized index methods within the repeat, T0 prefetching reduces
median time from 483.262 to 202.618 ns/query, about 58% less time. Their trial
ranges do not overlap there. Thus the earlier T2 experiment's regression did not
predict the behavior of the optimized traversal with its actual hint and access
pattern. The port changes several things together; this comparison does not
isolate T0 versus T2 alone or attribute all improvement to one instruction.

The separate value/sentinel benchmark reaches 132.317 ns/query at the largest
uniform size versus 658.009 for the standard value lookup, approximately 4.97
times throughput. It avoids the final position-map access. Because value mode is
a different process and contract, subtracting its timings from index-mode
numbers would not establish the isolated cost of that map access.

The optimized method is not universally fastest: see duplicate-heavy and all-equal
measurements above. Repeated paths offer different caching and branch-prediction
conditions. Hardware counters were not collected, so microarchitectural
explanations remain hypotheses rather than diagnoses. The original method is
retained and the port is exposed explicitly; no distribution-sensitive automatic
selection is introduced.

At the largest size, every Eytzinger variant occupies 402,654,672 bytes of vector
buffers (about 384 MiB), versus 134,218,220 bytes of sorted baseline keys (about
128 MiB). Alignment does not change the reported element capacities. Allocator
metadata and alignment slack are excluded. The value query touches no position
map, but this shared object still retains it. Construction timing is only a
single observation per dataset, not a rigorous build-time comparison.

The next main stage is the scalar S+ tree followed by node-search
optimizations, SIMD, batching, and interleaving. The optimized Eytzinger method
now provides a substantially stronger comparison baseline for those experiments.
