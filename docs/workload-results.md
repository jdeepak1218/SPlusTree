# Comprehensive validation results

Measured on 2026-11-16 using GCC 13.3.0, C++20, an Intel Core i7-12700H
exposed through WSL2 (two visible logical CPUs, approximately 3.8 GiB RAM).
The generic build uses `-O3 -DNDEBUG`; the targeted build also uses
`-mavx2 -mpopcnt`. Full methodology and reproduction commands are in
[workload-validation.md](workload-validation.md).

## Decision

The expanded checks found no incorrect search result and no ASan/UBSan finding.
One existing test defect was corrected: release builds could remove the original
smoke test's `assert` checks. Those 264 checks now remain active under `-DNDEBUG`.
The search implementations and default method are unchanged by this validation
work. These results support continuing development on this machine; they are not
proof of correctness or a complete cross-platform production qualification.

Counting and packed SIMD remain strong choices for large mixed-query indexes.
Their differences on real keys are small compared with some trial variation.
Hot queries favor packed SIMD in these measurements; all-equal data favors early
exit. A single aggregate speedup would conceal these different behaviors.

## Correctness evidence

| Suite | Coverage per configuration | Result |
| --- | --- | --- |
| Reference smoke | 264 explicit checks, including release builds | Pass |
| Existing Eytzinger | 1,391 datasets; 867,795 differential queries | Pass |
| Existing S+ | 3,777 datasets; 4,697,951 differential queries; all five methods | Pass |
| New robustness | 1,056 dataset/query combinations; 2,329,030 differential queries; 26,798,978 individual results including concurrent readers | Pass |
| Binary fixtures | Little-endian decode; full unsigned range; missing, empty, partial inputs; ordered queries | Pass |
| Encoder fixtures | Independent 16-mer oracle; ambiguity, case, line/header boundaries, short inputs | Pass |

The S+ and new robustness suites passed in four release configurations: generic,
AVX2/POPCNT, explicit mask backend disabled, and AVX2 with software population
count. All five C++ test programs also passed ASan/UBSan with library assertions;
leak detection was disabled. Raw output is in
[workload-validation.txt](../benchmarks/workload-validation.txt).

Every width 1..16 is exercised. Checks include lower-bound predecessor/successor
invariants, monotonicity, 20 random seeds, genuine maximum keys next to maximum
padding, and duplicate runs crossing structural boundaries. Four threads also
read shared copies after original trees are reassigned and source storage is
released. This is a correctness smoke test for concurrent reads, not a race
proof: ThreadSanitizer failed before execution with an unexpected memory mapping.

A temporary mutant that omitted the packed-mask divide by two was rejected:
`B=16 n=2 q=897878633 expected=1 actual=2`. The mutant was not retained.
No search implementation bug was found in this run.

## Benchmark volume and real input

There are 43 distinct dataset/query cases: 34 synthetic and nine real-data cases.
All eight methods run 131,072 probes per pass, eight measured passes and one
warm-up. AVX2, an independent AVX2 repeat, and generic builds produced 1,032 CSV
rows: **1,082,130,432 measured searches plus 135,266,304 warm-up searches**.
All **1,217,396,736 outputs** were checked individually outside the timed region.
Repeated probes are deliberately reused between passes; these are not 1.2 billion
unique queries. The recorded CSV audit verified coverage, raw-trial
summaries, hit/miss properties, and cross-method/build/repeat checksum agreement.

Real keys are 39,159,057 consecutive valid 16-base windows from UCSC hg38
chromosome 22, encoded as uint32 values and sorted without deduplication. There
are 29,318,528 distinct values. Smaller cases use actual prefixes of the sequence.
The recorded source and derived SHA-256 hashes are in
[real_dataset.json](../benchmarks/real_dataset.json). Queries over these real keys
are generated probes, not a captured application trace. Synthetic cases separately
model dense IDs, distant key clusters, uniform keys, and all-equal data.

## Full real-data index: mixed queries

All query numbers below are median **nanoseconds per search; lower is better**.
Trials include result-buffer stores but exclude input preparation, sorting,
construction, and validation. Comparisons should be made within a build.

| Method | AVX2 first | AVX2 repeat | Generic |
| --- | ---: | ---: | ---: |
| Standard lower bound | 513.297 | 537.739 | 535.063 |
| Manual binary search | 491.624 | 517.353 | 508.377 |
| Optimized Eytzinger | 239.866 | 240.579 | 239.764 |
| S+ early exit | 226.170 | 237.024 | 240.083 |
| S+ counting | 96.926 | 97.161 | 151.201 |
| S+ trailing-zero mask | 120.018 | 127.524 | 337.832 |
| S+ population count | 116.139 | 123.138 | 357.724 |
| S+ packed SIMD | 95.937 | 94.068 | 327.633 |

The first targeted run gives 5.35 times the standard method's throughput
for packed SIMD on this workload (81.3% less query time).
This comparison does not include index construction or establish an end-to-end
application speedup. The packed and counting trial ranges overlap:

- `stree_count`: 87.609–109.086 ns in the first run.
- `stree_packed`: 82.729–107.657 ns in the first run.

The generic packed method uses its portable population-count fallback. It does
not execute the targeted packed-SIMD instruction sequence. Its slower results
are a reason to select target flags and methods deliberately, not assume a
portable fallback has the same speed as AVX2.

## Real-data access order and locality

Same full index, first targeted run, with repeat medians in parentheses:

| Method | Hot queries | Sorted mixed queries |
| --- | ---: | ---: |
| Standard lower bound | 116.714 (128.197) | 137.202 (153.072) |
| Manual binary search | 135.717 (155.110) | 137.260 (153.445) |
| Optimized Eytzinger | 100.352 (107.652) | 116.908 (128.592) |
| S+ early exit | 81.402 (89.573) | 123.933 (135.645) |
| S+ counting | 41.297 (45.437) | 51.343 (52.746) |
| S+ trailing-zero mask | 48.502 (46.649) | 58.433 (61.507) |
| S+ population count | 51.489 (56.969) | 54.516 (57.577) |
| S+ packed SIMD | 36.034 (36.979) | 50.997 (53.803) |

Hot probes concentrate 90% of traffic on 64 sampled positions. Sorted queries
contain the same multiset as mixed queries, but their order improves locality
and branch behavior. Sorting cost is excluded: these measurements represent an
already ordered workload, not free query sorting for an application.

## Selected synthetic cases

First targeted run; the CSV files retain all methods and all 34 cases.

| Keys / queries | Size | Standard | Eytzinger | Early exit | Counting | Packed |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| uniform / mixed | 1,024 | 58.209 | 8.999 | 41.310 | 9.526 | 6.516 |
| dense / hits | 1,048,576 | 207.817 | 80.653 | 108.643 | 27.213 | 23.722 |
| dense / misses | 1,048,576 | 208.764 | 79.111 | 108.457 | 26.246 | 21.544 |
| equal / mixed | 1,048,576 | 19.299 | 35.185 | 10.581 | 18.725 | 13.304 |
| uniform / mixed | 33,554,555 | 572.661 | 219.289 | 231.126 | 93.146 | 94.028 |
| uniform / hot | 33,554,555 | 132.095 | 110.984 | 79.809 | 41.467 | 36.068 |
| clusters / mixed | 33,554,555 | 395.995 | 271.013 | 183.489 | 87.353 | 88.857 |
| clusters / hot | 33,554,555 | 85.004 | 68.692 | 50.544 | 32.143 | 24.074 |

The dense hit and miss cases are exactly 100% and 0% hits respectively. Mixed
cases alternate sampled hits and full-range random probes; their observed hit
rate can exceed 50%. All-equal early exit wins because each node can decide with
very few comparisons. Hot-query winners differ from broad random access, so
large random-array results should not be applied unchanged to a skewed workload.

## Allocation and construction

Full real-data index; bytes describe allocated index capacity, excluding input
vectors retained by the caller. Build times are single first-run observations.

| Index | Bytes | MiB | Build ms |
| --- | ---: | ---: | ---: |
| Sorted vector | 156,636,228 | 149.38 | 0.000 |
| Mapped Eytzinger | 469,908,696 | 448.14 | 765.791 |
| S+ (all five methods share storage) | 166,426,368 | 158.72 | 179.626 |

The standard method has no separate index build. S+ storage is approximately
6.25% above the key vector; Eytzinger also retains the sorted-position map.
Do not treat these numbers as peak process memory or total application memory.

## Artifacts and limits

The six `benchmarks/workloads_*.csv` files contain all raw trials, medians,
minimum/maximum values, query/hit counts, distinct-key counts, index sizes, build
times, and checksums. `workload-benchmark.txt` records completion of each case.
`workload-csv-check.txt` records the independent CSV consistency check.

Absolute timings vary in WSL. There is no frequency locking or CPU pinning;
some trial ranges are wide and overlap. These are resident-index, repeated-query
throughput measurements, not cold-cache latency, dependent-query latency, or
parallel throughput. Allocation failure, other compilers, operating systems,
and non-x86 machines remain unchecked. ThreadSanitizer could not start and leak
checking was disabled; ASan/UBSan success does not prove absence of undefined
behavior. The public value-returning API still uses UINT32_MAX as an ambiguous
no-result sentinel; all index-returning APIs retain exact lower-bound positions.
