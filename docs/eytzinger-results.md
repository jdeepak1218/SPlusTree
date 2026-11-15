# Plain Eytzinger results

Recorded on 2026-11-15. This stage implements inorder construction into a
one-based array and lower-bound lookup returning original sorted positions.
It includes no prefetching or SIMD. Construction rejects unsorted input.

## Correctness

- Existing reference/binary checks pass, with 50 Eytzinger comparisons added.
- Dedicated suite passes: 877 datasets and 338,250 differential queries against
  `std::lower_bound`, also checking the binary-search baseline.
- Large correctness inputs contain 1,000,003 and 8,388,731 keys.
- Exhaustive small-alphabet inputs, incomplete trees, duplicates, integer limits,
  invalid unsorted input, ownership, copying, and moving are covered.
- GCC builds completed without warnings. The dedicated suite was also compiled
  with `-Wconversion`.
- AddressSanitizer and UndefinedBehaviorSanitizer passed. LeakSanitizer could not
  run under the environment's tracing setup; leak detection was disabled for the
  successful sanitizer run. No leak-check result is claimed.
- Three deliberately faulty temporary versions (wrong equality handling, wrong
  result index, and wrong traversal bound) were all rejected by the suite.

See `tests/README.md` for commands, coverage, and limits.

## Search benchmark

One million queries per pass, one warm-up, seven measured trials per
implementation. Uniform input uses six sizes. Duplicate-heavy and all-equal
inputs use two sizes each. In total: 210 million measured searches and 30 million
warm-up searches. Every pass is compared element by element against the reference
outside the timed region. No result mismatches occurred.

Median ns/query at 33,554,555 keys (lower is better):

| Workload | std::lower_bound | Binary search | Eytzinger |
| --- | ---: | ---: | ---: |
| Uniform 31-bit keys and queries | 787.872 | 783.384 | 401.367 |
| Eight distinct keys, half exact-hit queries | 49.316 | 49.567 | 47.672 |
| All keys equal, half exact-hit queries | 28.474 | 32.218 | 36.663 |

On uniform data, Eytzinger is approximately 1.96 times as fast as the standard
reference in this run. Its timing range is 398.785–419.070 ns/query, versus
734.246–805.781 for the reference. Duplicate-heavy timings overlap substantially;
these measurements do not establish a clear advantage there. Eytzinger is slower
on all-equal input. These distributions reuse a small set of search paths, so
large input length alone does not imply a large active search working set.

## Memory and interpretation

At the largest size, baseline key storage is 134,218,220 bytes (about 128 MiB).
Eytzinger's key and position buffers occupy 402,654,672 bytes (about 384 MiB).
The extra mapping supports our index-returning contract. This is not a direct
reproduction of the blog's value-returning search. The benchmark retains both
representations for validation; process memory also includes queries and output.

The largest uniform tree took about 478 ms to construct, including validation
and allocation. This is a single observation, not a construction benchmark.

The runs used GCC 13.3.0 with `-O3 -DNDEBUG`, on a reported i7-12700H under a
Microsoft hypervisor. CPU frequency and affinity were not fixed. Full CSVs
include min/max timing spread. These are independent-query throughput
measurements with output writes, not dependent single-query latency.

Raw results: `benchmarks/eytzinger.csv`,
`benchmarks/eytzinger_duplicates.csv`, and `benchmarks/eytzinger_equal.csv`.
Reproduction instructions are in `benchmarks/README.md`.

This testing does not cover every possible input, allocation failure, every
compiler, or every machine. The next performance experiment is Eytzinger
prefetching, measured separately before moving to S+ trees.
