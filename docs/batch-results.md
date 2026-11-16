# Batched search results

Measured on 2026-11-16 with GCC 13.3.0 / C++20 on an Intel Core i7-12700H
through WSL2, two visible logical CPUs and approximately 3.8 GiB RAM. Targeted
builds use `-O3 -DNDEBUG -mavx2 -mpopcnt`; generic builds omit the target flags.
See [API and measurement details](stree-batching.md).

## Change

Added a fixed-size `std::array` batch overload and a span overload for arbitrary
query counts. Independent queries advance through one tree level at a time.
Complete groups use the fixed-size core; remaining queries use individual packed
lookup. Output preserves input order and returns exact original lower-bound
indices. Unequal span lengths throw before writing; input/output storage must
not overlap. Neither method allocates heap storage or changes index size.

The existing packed node comparison, tree layout, unsigned range, duplicate
behavior, and individual methods are retained. There is no new default batch
size, prefetching, runtime ISA dispatch, or additional search thread. Generic
and disabled-backend builds use the existing portable node helper.

## Correctness

| Check | Observed result |
| --- | --- |
| New batch suite | 666 dataset/query combinations; 4,212,846 checked results per configuration |
| Four release builds | Generic, AVX2/POPCNT, explicit mask backend disabled, software population count: all pass |
| ASan/UBSan batch suite | All 4,212,846 results pass with library assertions enabled |
| Full real-key release test | 39,159,057 keys; 4,719,732 checked results across mixed/hot/sorted/boundary queries: pass |
| Full real-key ASan/UBSan test | Same 4,719,732 checked results: pass |
| Existing reference/Eytzinger/S+ suites | All pass, including 4,697,951 S+ differential queries |
| Existing robustness/data/encoder suites | All pass; robustness checks 26,798,978 individual results per configuration |
| Integration | Standalone header, repeated inclusion, two translation units, array/span examples, array `noexcept`: pass |
| Compile-time precondition | Zero-sized array batch rejected by static assertion |
| Mutation sensitivity | Skipped final tail output rejected with an exact result mismatch |

Batch tests cover sizes 1/2/3/4/8/16/32/64/128, all node widths 1..16, tree
capacity boundaries, query counts around every measured batch boundary, empty
and moved-from trees, unsigned extremes, duplicate separators, genuine maximum
keys next to padding, unchanged queries, output canaries, and length rejection
before writes. Twenty seeds, million-element arrays, copy/move ownership, and
four concurrent readers supplement the structural cases. Results are compared
with both `std::lower_bound` and individual packed lookup, with predecessor and
successor bounds checked independently. No search error or ASan/UBSan finding
was detected. This is evidence for tested cases, not a proof of correctness.

Raw outputs: `benchmarks/batch-validation.txt`, `batch-real-release.txt`,
`batch-real-sanitized.txt`, and `batch-mutation.txt`. Leak checking is disabled;
ThreadSanitizer could not start in this environment during the earlier audit
and was not rerun. No non-x86 or additional-compiler validation was performed.

## Benchmark scope

The matrix contains 31 cases: 22 synthetic and nine real-data cases. It compares
standard lower bound, individual counting, individual packed lookup, and eight
batch sizes. Each case has 131,075 deterministic queries, one warm-up, and eleven
measured trials. The odd query count exercises incomplete batches. Run order
rotates so each of eleven methods occupies every position once.

AVX2, an independent AVX2 repeat, and generic runs produced **1,023 CSV rows**:
**1,474,986,975 measured searches plus 134,089,725 warm-up searches**, with all
**1,609,076,700 outputs** checked individually outside timing. Probes are reused
between trials, not unique for every measured search. Output buffers are filled
with a non-answer marker before each pass, outside timing, to catch skipped
writes rather than reusing a previous method's answers.

All CSV coverage, raw medians/minima/maxima, hit/miss properties, and checksums
across methods, builds, and repeats passed `tools/check_batch_results.py`.
Negative checker fixtures with altered timing summaries and inconsistent
checksums were rejected too. Summary output is in `batch-csv-check.txt`.

Real keys are the same chromosome-derived 16-mers as the earlier audit: full
39,159,057 keys, plus prefixes of 1,048,576 and 8,388,608. Duplicates are retained;
queries are generated mixed/hot/sorted probes, not captured application traffic.
The derived dataset SHA-256 was rechecked against `real_dataset.json`:
`c446e22bf3256156a823157c069a3dbf15a60d3565d1e5aaa6098b0bdfd89d2c`.

All reported query times are median nanoseconds per search; lower is better.
Input copies, batch-local state, final incomplete groups, and output stores are
included. Generation, sorting, tree construction, and checks are excluded.

## Full real-key index: mixed queries

| Method | AVX2 first | AVX2 repeat | Generic |
| --- | ---: | ---: | ---: |
| `std` | 515.646 | 568.600 | 543.069 |
| `count` | 109.486 | 115.829 | 152.594 |
| `packed` | 114.435 | 128.861 | 370.312 |
| `batch_1` | 111.753 | 114.257 | 328.369 |
| `batch_2` | 88.798 | 97.783 | 204.747 |
| `batch_4` | 77.945 | 85.372 | 195.325 |
| `batch_8` | 48.883 | 58.836 | 194.951 |
| `batch_16` | 42.254 | 47.909 | 192.934 |
| `batch_32` | 41.529 | 42.591 | 196.640 |
| `batch_64` | 40.653 | 41.895 | 198.943 |
| `batch_128` | 38.731 | 40.814 | 200.822 |

Batch 128 gives **2.95x** individual packed throughput in the first targeted run
and **3.16x** in the repeat. Against counting, the corresponding gains are
**2.83x** and **2.84x**. Against standard lower bound, they are
**13.31x** and **13.93x**. These ratios describe same-run query throughput;
they exclude construction and do not describe total application speedup.

Batch 1 is a control, not an explicit memory-overlap technique. Its code and
compiler scheduling can differ from the individual lookup, so its timing need
not be identical. Increasing the batch beyond one exposes independent node
requests; gains taper as larger batches stop hiding additional latency and
incur more local-state/copy work.

The generic fallback remains correct but is not the fastest portable option:
counting beats every batched fallback on this full-index mixed workload. Do not
select the targeted method universally without checking CPU capabilities and
compiler flags. The library does not provide runtime ISA dispatch.

## Full real-key index: locality

First targeted run; repeat medians in parentheses:

| Method | Hot queries | Sorted mixed queries |
| --- | ---: | ---: |
| `count` | 58.535 (55.160) | 53.230 (55.189) |
| `packed` | 53.084 (55.185) | 52.206 (54.339) |
| `batch_8` | 28.645 (34.133) | 31.494 (32.849) |
| `batch_16` | 26.702 (29.838) | 30.302 (31.396) |
| `batch_32` | 21.423 (27.081) | 30.643 (31.865) |
| `batch_64` | 22.818 (27.912) | 29.524 (30.128) |
| `batch_128` | 19.396 (25.699) | 28.946 (29.974) |

Hot queries send 90% of probes to 64 sampled positions. Sorted queries reuse the
mixed-query multiset in increasing order; sorting cost is excluded. This models
already ordered input rather than claiming free query reordering.

## Real mixed queries across sizes

First targeted run:

| Keys | Standard | Counting | Individual packed | Batch 8 | Batch 32 | Batch 128 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,048,576 | 223.319 | 25.557 | 33.454 | 12.452 | 13.820 | 14.416 |
| 8,388,608 | 448.498 | 59.739 | 63.664 | 33.516 | 28.359 | 26.120 |
| 39,159,057 | 515.646 | 109.486 | 114.435 | 48.883 | 41.529 | 38.731 |

## Synthetic cases and non-universal winners

First targeted run; all batch sizes and repeat results remain in the CSV files.

| Keys / queries | Size | Counting | Individual packed | Batch 8 | Batch 32 | Batch 128 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| uniform / mixed | 1,024 | 9.314 | 6.571 | 3.911 | 5.030 | 4.765 |
| dense / hits | 1,048,576 | 30.115 | 35.837 | 17.018 | 14.618 | 13.318 |
| dense / misses | 1,048,576 | 28.599 | 34.281 | 14.433 | 14.980 | 14.341 |
| equal / mixed | 1,048,576 | 19.685 | 13.812 | 6.663 | 8.755 | 8.133 |
| uniform / mixed | 33,554,555 | 100.851 | 122.289 | 53.389 | 40.924 | 38.997 |
| uniform / hot | 33,554,555 | 60.753 | 59.252 | 34.381 | 26.976 | 27.667 |
| clusters / mixed | 33,554,555 | 93.451 | 95.511 | 48.435 | 33.259 | 30.200 |
| clusters / hot | 33,554,555 | 33.301 | 24.900 | 10.283 | 12.883 | 12.366 |

Batch 128 leads the large real mixed case, but batch 8 is faster on the large
clustered hot case and the all-equal case. Larger batches do not universally win.
There is no automatic batch-size selection or changed individual default.

## Storage, variation, and next experiment

Every S+ method shares the same index. On the full real data it allocates
166,426,368 bytes (158.72 MiB), approximately 6.25% above the sorted keys.
Batch positions, input copies, and answers use local arrays proportional to the
compile-time batch size; they add no persistent index or per-call heap storage.
Caller-owned query/output buffers are separate from these index bytes.

The first full real mixed run spans 34.197–42.754 ns/query for batch 128 and
108.259–123.870 for individual packed lookup. The repeat spans 33.945–48.923 and
111.049–276.515 respectively. Differences between nearby large batch sizes have
overlapping ranges. Host noise is visible, especially in some individual/hot
trials; no statistical-significance claim is made.

Measurements are serial resident-index throughput under WSL, with no frequency
lock, CPU affinity, huge-page setup, or cold-cache control. They do not measure
dependent-query latency, production request traces, allocation failure, or thread
scaling. Prior CSV files remain untouched; compare methods within these new runs
because query count, run order, and generated data differ from historical runs.

Reproduce with `bash tools/run_validation.sh`, the two real-data validation
commands in `stree-batching.md`, `bash tools/run_batch_benchmarks.sh`, and
`python3 tools/check_batch_results.py`. The six `benchmarks/batch_*_real.csv` /
`batch_*_synthetic.csv` files retain every raw trial. Large downloaded and derived
datasets remain ignored. The next isolated search experiment is explicit
prefetching of the selected next-level node during the batch loop.
