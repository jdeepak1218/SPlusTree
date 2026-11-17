# Batched prefetch results

Measured 2026-11-17 with GCC 13.3.0, C++20, Intel Core i7-12700H under WSL2,
two visible logical CPUs and approximately 3.8 GiB RAM. Generic builds use
`-O3 -DNDEBUG`; targeted builds add `-mavx2 -mpopcnt`.

## Implementation and meaning

Added `lower_bound_batch_prefetch` for fixed arrays and arbitrary input/output
spans. The C++ code selects each query's next child, then requests that node
before processing the remaining queries at the current level. This gives the
CPU an earlier opportunity to fetch the next level. The request is a read hint
with high temporal locality, not a guarantee that a cache miss is eliminated.

Both ordinary and prefetched APIs share a private traversal and span adapter,
selected at compile time. The ordinary API emits no hint or runtime selection
branch. A subtraction-based bounds check happens before forming the requested
pointer. Existing comparison backends, tree layout, first-duplicate answers,
full unsigned range, output order, and scalar tail handling are retained. No
persistent or per-call heap allocation is added. Index storage stays identical.
Prefetching remains an explicit method; no existing default is replaced.

GCC/Clang use the builtin; other compilers or `SST_DISABLE_PREFETCH` use the
ordinary traversal. Target and selection macros must agree across translation
units. Examples and reproduction are in [stree-batch-prefetch.md](stree-batch-prefetch.md).

## Correctness and code-generation evidence

| Check | Result |
| --- | --- |
| Combined ordinary/prefetched batch suite | 666 dataset/query combinations; 8,425,692 checked outputs per configuration |
| Five release configurations | Generic, AVX2/POPCNT, mask backend disabled, software population count, hints disabled: pass |
| ASan/UBSan | All six C++ test programs pass with library assertions; no finding detected |
| Full real-key release check | 39,159,057 keys; 9,439,464 ordinary/prefetched results: pass |
| Full real-key ASan/UBSan check | Same 9,439,464 results: pass |
| Header integration | Standalone/repeated include, two translation units, array/span calls, array noexcept: pass |
| Invalid inputs | Unequal lengths rejected before writes; zero batch size rejected at compile time |
| Code-generation control | Enabled probe contains prefetcht0; disabled probe contains none |
| Mutation | Deliberately shifted prefetched leaf output rejected with B=1, P=3, n=1, q=0, expected=0, actual=1 |
| CSV checker negative fixtures | Altered median and inconsistent checksum rejected; historical CSV checks still pass |

Coverage includes all node widths 1..16, batch sizes 1/2/3/4/8/16/32/64/128,
structural boundaries, duplicates, maximum padding, partial batches, empty and
moved-from trees, query subspans, output canaries, copy/move ownership, twenty
seeds, large arrays, and four concurrent readers. Results are compared with both
the reference and individual packed lookup, with independent predecessor and
successor checks. Existing S+ tests still pass 4,697,951 differential queries;
the broader robustness suite still checks 26,798,978 results per configuration.
No search-result failure or sanitizer finding was detected. That is evidence
for these cases, not proof of correctness or absence of all undefined behavior.

## Benchmark scope

The main matrix has 31 cases (22 synthetic, nine real), nineteen methods,
131,075 queries per pass, twelve measured trials, and one warm-up. Methods are
standard lower bound, individual counting, individual packed, and matched
ordinary/prefetched batch sizes 1 through 128. Run order is shuffled per trial.
AVX2, an independent AVX2 repeat, and generic builds run serially.

The main six CSVs contain **1,767 rows**, **2,779,314,300 measured searches**,
and **231,609,525 warm-up searches**. All **3,010,923,825 outputs** were checked
individually after timing. Repeated trials reuse probes; these are not billions
of distinct queries. Output buffers are poisoned before each timed pass so a
missing write cannot reuse a previous answer.

Copies, pointer checks, hints, tail handling and output stores are timed.
Generation, sorting, construction and validation are excluded. Every raw trial,
median, minimum/maximum, observed hit count, checksum, storage size and build
observation is retained. Compare methods within each run rather than historical
absolute times: the method order, sample count and execution environment vary.

The full real dataset is the same chromosome-derived uint32 16-mers described
in `benchmarks/real_dataset.json`; smaller real cases use actual prefixes.
Queries are generated mixed/hot/sorted probes rather than captured application
traffic. Its derived SHA-256 was verified again:
`c446e22bf3256156a823157c069a3dbf15a60d3565d1e5aaa6098b0bdfd89d2c`.

## Full real index: matching batch sizes, mixed queries

Median nanoseconds per search, lower is better. Positive time reduction means
the prefetched method used less time than the matching ordinary batch.

| Batch | Ordinary | Prefetched | Time reduction | Ordinary repeat | Prefetched repeat | Repeat reduction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 136.775 | 141.439 | -3.4% | 113.859 | 120.171 | -5.5% |
| 2 | 107.387 | 106.123 | +1.2% | 91.491 | 92.332 | -0.9% |
| 4 | 93.304 | 98.365 | -5.4% | 84.351 | 84.722 | -0.4% |
| 8 | 59.384 | 59.060 | +0.5% | 55.753 | 56.961 | -2.2% |
| 16 | 50.369 | 42.457 | +15.7% | 46.891 | 40.316 | +14.0% |
| 32 | 49.169 | 36.415 | +25.9% | 42.459 | 34.453 | +18.9% |
| 64 | 44.857 | 38.651 | +13.8% | 41.239 | 33.620 | +18.5% |
| 128 | 47.931 | 35.917 | +25.1% | 43.799 | 34.121 | +22.1% |

Batches 16 and larger improve consistently in these two full-index mixed runs.
Small batches show limited benefit or regress. Nearby large batch sizes have
overlapping trial ranges, so the fastest median does not establish a universal
best batch size. Prefetching is not selected automatically.

| Existing baseline | First run | Repeat |
| --- | ---: | ---: |
| std | 650.495 | 627.643 |
| count | 132.776 | 108.220 |
| packed | 122.623 | 110.664 |

## Locality and input size

First targeted run; full real index:

| Pattern | Ordinary 32 | Prefetched 32 | Ordinary 128 | Prefetched 128 |
| --- | ---: | ---: | ---: | ---: |
| mixed | 49.169 | 36.415 | 47.931 | 35.917 |
| hot | 19.604 | 17.879 | 19.581 | 18.870 |
| sorted | 32.627 | 26.057 | 30.089 | 23.986 |

Hot queries concentrate 90% of probes on 64 sampled positions. Sorted probes
reuse the mixed multiset in order; sorting cost is excluded. Benefits are
smaller or noisier for hot probes than for broad random access.

Selected targeted cases expose both gains and overhead:

| Data / queries | Keys | Ordinary 32 | Prefetched 32 | Ordinary 128 | Prefetched 128 |
| --- | ---: | ---: | ---: | ---: | ---: |
| uniform / mixed | 1,024 | 6.606 | 7.200 | 6.320 | 7.199 |
| equal / mixed | 1,048,576 | 10.364 | 11.289 | 10.107 | 11.263 |
| dense / misses | 1,048,576 | 15.860 | 12.290 | 15.821 | 12.645 |
| uniform / mixed | 33,554,555 | 42.355 | 34.705 | 43.431 | 34.244 |
| clusters / hot | 33,554,555 | 15.817 | 16.271 | 15.070 | 16.006 |
| chr22_16mers / mixed | 1,048,576 | 15.063 | 12.359 | 14.499 | 12.635 |
| chr22_16mers / mixed | 8,388,608 | 26.601 | 20.110 | 28.617 | 19.798 |

## Generic build and hints-disabled control

Full real mixed index, generic build:

| Method | Hints enabled | Control with hints compiled out |
| --- | ---: | ---: |
| count | 179.006 | 178.453 |
| batch_32 | 243.434 | 224.727 |
| prefetch_32 | 104.783 | 225.138 |
| batch_128 | 234.453 | 227.552 |
| prefetch_128 | 110.495 | 220.706 |

The large generic gain motivated a separate control build with
`-DSST_DISABLE_PREFETCH`. Its ordinary and prefetched methods have similar
timings once hints are removed, supporting the observed benefit of the
hint-enabled implementation. This comparison includes compiler scheduling and
the address-check code; it does not isolate the cost of one machine instruction.
Use within-run comparisons, since the control is another process.

The control covers all nine real cases and adds 171 rows, each with twelve
measured trials and checked warm-up outputs. The recorded CSV audit validated it,
including agreement with the main runs.

Reproduce the extra control after the main runner:

```bash
g++ -std=c++20 -O3 -DNDEBUG -DSST_DISABLE_PREFETCH -Iinclude benchmarks/batch_benchmark.cpp -o build/batch_prefetch_disabled
build/batch_prefetch_disabled --prefetch data/chr22.u32 > benchmarks/batch_prefetch_generic_disabled_real.csv
```

Counting can still win smaller generic workloads. The new method remains an
explicit choice rather than replacing all existing APIs.

## Limits and artifacts

The real index still owns 166,426,368 bytes (158.72 MiB), about 6.25% above the
sorted key vector. Methods share the same index; local batch arrays and caller
buffers are separate. Build times are single observations, not repeated studies.

The raw CSVs include overlapping ranges and occasional large outliers. There is
no CPU affinity, frequency lock, huge-page setup, or cold-cache control under
WSL. These are resident-index throughput results, not dependent-query latency,
multithread scaling, or guaranteed end-to-end application gains. Only GCC/x86
was executed. Leak checking was disabled, and ThreadSanitizer was not rerun after
its earlier startup failure. The ordinary method remains available for workloads
where hints add overhead.

Raw outputs and instruction/integration checks are under
`benchmarks/batch-prefetch-*.txt`. All six main CSVs and the separate control CSV
are retained. Large inputs and compiled artifacts stay ignored. No historical
benchmark data was overwritten. The next isolated experiment is preparing SIMD
query vectors once per batch before traversing the levels.
