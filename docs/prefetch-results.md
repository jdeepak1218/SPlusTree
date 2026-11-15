# Eytzinger prefetch experiment

## Implementation

`Eytzinger::lower_bound_prefetch` preserves the same index-returning lower-bound
contract and candidate-tracking traversal as `lower_bound`. The original method
remains available and unchanged. Both methods share the same tree buffers.

Before each comparison, the new method requests the block four tree levels below
the current node: positions `[16 * node, 16 * node + 15]`, restricted to existing
positions. The key vector is not explicitly cache-line aligned. On the tested
64-byte-line machine this 64-byte block can occupy two cache lines, so the method
requests both endpoints. A partial block can produce redundant hints, which are
included in the measured overhead. The original-position map is not prefetched.

The hints use GCC/Clang's `__builtin_prefetch(address, 0, 1)`: read access with
locality hint 1. GCC emitted `prefetcht2` in the tested x86-64 binary. Other
compilers use the same traversal without hints. `SST_DISABLE_PREFETCH` allows an
explicit fallback build and must be defined consistently across translation units.

An initial implementation put the hints inside a small helper. GCC 13.3 removed
the hints in the optimized probe and benchmark binaries. Moving them directly
into the traversal loop retained them. Final disassembly confirms two
`prefetcht2` instructions in the benchmark. Correct answers alone would not have
detected a missing performance hint.

## Correctness and undefined-behavior review

All final checks passed:

- Original reference test, with both Eytzinger methods compared to the reference.
- 1,391 datasets and 867,795 differential query cases comparing both Eytzinger
  methods and binary search to `std::lower_bound`.
- Every length from zero through 513, including lookahead thresholds and partial
  descendant blocks, with exact-key and adjacent-value queries.
- Exhaustive small-alphabet sorted inputs, randomized distributions, duplicates,
  all-zero/all-maximum input, signed-boundary values, and incomplete last levels.
- Large correctness arrays of 1,000,003 and 8,388,731 keys, plus the larger
  benchmark arrays with every measured output compared to the reference.
- Unsorted-input rejection, source mutation, copying, moving, and assignment.
- The full dedicated suite with `SST_DISABLE_PREFETCH` and `-DNDEBUG`, verifying
  that the fallback works and differential checks survive release configuration.
- AddressSanitizer, UndefinedBehaviorSanitizer, and libstdc++ assertions on the
  final implementation. UBSan was configured to stop rather than recover.

Bounds reasoning for the new code:

1. Empty input skips the search loop, avoiding unsigned `n - 1` underflow.
2. The guard `node <= n / 16` precedes multiplication, proving `first = node * 16`
   is representable and no greater than `n`.
3. `last = first + min(15, n - first)` satisfies `first <= last <= n`, so its
   subtraction and addition cannot overflow.
4. The key vector has `n + 1` elements. Both prefetch pointers refer to actual
   elements; no out-of-range pointer is formed.
5. Existing child checks precede child-index multiplication. Searches allocate
   no storage and modify neither tree buffer.

No address or undefined-behavior errors were reported. This is evidence for the
tested executions, not a proof that no undefined behavior is possible. Prefetch
hints do not behave like ordinary loads, so sanitizer success alone does not
establish address validity; the bounds reasoning is necessary as well.
Leak detection was disabled because LeakSanitizer cannot run under this
environment's tracing setup. Allocation-failure injection, unallocatable sizes,
concurrent execution, other compilers, and other architectures remain untested.

## Benchmark method

- Four implementations run against identical inputs and queries within each
  process: `std::lower_bound`, binary search, Eytzinger, and Eytzinger prefetch.
- One million queries per pass; one full warm-up and eight timed trials per
  implementation. Rotation gives each method every execution position twice.
- Median is the mean of the middle two sorted timings. CSVs retain minimum,
  maximum, and each trial's original timing.
- Timed regions include result-buffer writes. Allocation, construction, input
  sorting, generation, comparisons, and printing are outside those regions.
- Every warm-up and timed pass is checked element by element against reference
  outputs, with explicit checks active under `-DNDEBUG`.
- Uniform keys/queries are 31-bit samples, seed 42, over six sizes from 1,024 to
  33,554,555 elements. The complete uniform run is repeated in a separate process.
- Duplicate-heavy input has eight distinct keys spanning almost the full unsigned
  range. All-equal input contains `0x80000000`. Each stress workload uses the
  1,048,576 and 33,554,555 sizes; half its queries are sampled hits and half are
  full-range random queries.
- Runs execute sequentially. Across the four runs: 512 million measured searches
  and 64 million warm-up searches.

Environment: GCC 13.3.0, flags `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic
-Wconversion -Iinclude`, x86-64 Linux `6.6.87.2-microsoft-standard-WSL2`, reported
Intel i7-12700H with two visible logical CPUs. Earlier reported caches were 48 KiB
L1 data, 1.3 MiB L2, and 24 MiB L3. No affinity, frequency lock, explicit huge
pages, or cold-cache flushing was configured. Host activity and virtualization
can affect measurements. These are independent-query throughput measurements,
not dependent single-query latency. Compare methods within the same run rather
than comparing absolute times with older CSVs.

No new persistent buffers are required for prefetching. At the largest size,
both Eytzinger methods use 402,654,672 bytes of vector buffers, versus 134,218,220
bytes for the sorted baseline. The benchmark retains both representations plus
query/output buffers; total process memory is greater. This position-mapped
layout is not a direct reproduction of the blog's value-returning implementation.

See `benchmarks/README.md` and `tests/README.md` for reproduction commands.

## Results

All timings below are median ns/query; lower is better. The final column is
the increase in query time relative to plain Eytzinger, not relative to binary
search.

### Uniform, first process

Source: `benchmarks/prefetch_uniform.csv`.

| Elements | Standard | Binary | Eytzinger | Prefetch | Prefetch time increase |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 59.397 | 58.627 | 50.032 | 57.703 | 15.3% |
| 65,536 | 105.821 | 104.713 | 83.307 | 95.570 | 14.7% |
| 1,048,576 | 221.163 | 213.954 | 143.717 | 152.508 | 6.1% |
| 8,388,608 | 483.144 | 470.722 | 266.040 | 294.552 | 10.7% |
| 16,777,339 | 504.019 | 485.833 | 298.438 | 353.655 | 18.5% |
| 33,554,555 | 661.703 | 628.112 | 347.748 | 399.018 | 14.7% |

Largest-input trial ranges: plain Eytzinger 343.144–368.365 ns/query; prefetch 390.618–411.553 ns/query.

### Uniform, repeated process

Source: `benchmarks/prefetch_uniform_repeat.csv`.

| Elements | Standard | Binary | Eytzinger | Prefetch | Prefetch time increase |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 58.640 | 58.247 | 49.262 | 56.820 | 15.3% |
| 65,536 | 106.223 | 105.465 | 84.335 | 96.270 | 14.2% |
| 1,048,576 | 220.264 | 215.385 | 144.779 | 153.705 | 6.2% |
| 8,388,608 | 471.200 | 453.176 | 260.180 | 298.695 | 14.8% |
| 16,777,339 | 490.217 | 471.456 | 304.480 | 340.141 | 11.7% |
| 33,554,555 | 626.409 | 597.209 | 342.740 | 396.162 | 15.6% |

Largest-input trial ranges: plain Eytzinger 338.953–349.141 ns/query; prefetch 393.130–433.467 ns/query.

### Duplicate-heavy

Source: `benchmarks/prefetch_duplicates.csv`.

| Elements | Standard | Binary | Eytzinger | Prefetch | Prefetch time increase |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 37.417 | 38.693 | 29.050 | 37.592 | 29.4% |
| 33,554,555 | 39.144 | 40.069 | 33.418 | 44.987 | 34.6% |

Largest-input trial ranges: plain Eytzinger 32.948–34.072 ns/query; prefetch 44.728–46.028 ns/query.

### All equal

Source: `benchmarks/prefetch_equal.csv`.

| Elements | Standard | Binary | Eytzinger | Prefetch | Prefetch time increase |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 21.546 | 18.466 | 17.066 | 29.656 | 73.8% |
| 33,554,555 | 24.575 | 24.897 | 24.730 | 39.552 | 59.9% |

Largest-input trial ranges: plain Eytzinger 24.316–25.109 ns/query; prefetch 38.703–41.335 ns/query.

## Interpretation and next steps

This prefetch variant was slower than plain Eytzinger in every tested size and
workload. At 33,554,555 uniform keys it increased median time by 14.7% in the first
process and 15.6% in the repeated process. The plain/prefetch trial ranges at that
size do not overlap in either run, supporting a repeatable regression in this
configuration. The plain method remains the existing `lower_bound` entry point;
the prefetch method is an explicit experiment.

The fixed four-level distance and locality hint are not universally optimal.
Additional bounds checks, hints, and fetching unused descendants can introduce
costs. Repeated-value workloads reuse a small set of paths and offer less
opportunity to hide memory latency. These are plausible explanations, not a
hardware-counter diagnosis: counters were not collected. These results do not
show that prefetching in general is ineffective, or predict performance on
another CPU, allocation layout, or search implementation.

The result is a reason to retain measurements rather than make the new method
the default. Optional follow-up experiments can vary locality, lookahead, explicit
alignment, and branchless traversal one at a time. The next main learning stage
from the blog is the scalar S+ tree: draw separators and leaves, build a compact
layout, verify lower-bound semantics including duplicates, then run this same
benchmark matrix. SIMD and batching come after the scalar tree is correct.
