# Benchmarks

Build and run independently of sanitizer tests:

```bash
mkdir -p build results
g++ -std=c++20 -O3 -DNDEBUG -mavx2 -mpopcnt -Iinclude benchmarks/batch_benchmark.cpp -o build/batch_benchmark
./build/batch_benchmark --prefetch > results/batch-prefetch-synthetic.csv
./build/batch_benchmark --prefetch data/chr22.u32 > results/batch-prefetch-real.csv
```

AVX2 binaries require compatible hardware. Omit `-mavx2 -mpopcnt` for a generic
build. The input file is optional; without it the executable runs synthetic cases.

## Comparisons and measurement

The batch harness compares `std::lower_bound`, counting, packed SIMD, batch sizes
1–128, matching prefetched batches, and **Eytzinger with four-level prefetching**.
Eytzinger is an adapted comparison baseline, not an external library dependency;
its implementation and notices are in [baselines](baselines/eytzinger.hpp) and
[third_party](../third_party/README.md). All methods return the same sorted index.

Each case has 131,075 independent probes, one warm-up, and twelve measured trials.
The prefetch mode shuffles method order each trial; ordinary mode rotates it.
Every result is checked against `std::lower_bound` and lower-bound brackets after
timing. Output is poisoned before each pass to catch skipped writes. CSVs retain
all trials, medians, min/max, checksums, hit counts, build time, and storage bytes.

Timing includes output stores and batch copies/tails. Generation, sorting,
construction, and validation are excluded. Input data cover uniform, clustered,
dense, and equal keys; probes cover mixed, hot, sorted, hit, and miss cases.
The owning facade's allocating overload is not timed by this backend harness.

For a complete generic/AVX2/repeat matrix with real input:

```bash
bash tools/run_batch_prefetch_benchmarks.sh data/chr22.u32
```

Other runners cover ordinary batching and individual-backend workloads.
`search_benchmark.cpp` retains focused modes (`indices`, `values`, `counting`,
`mask`, `popcount`, `packed`); `count_codegen.cpp` provides assembly inspection symbols.
Generated CSVs go in ignored `results/`, keeping the source tree small.

## Recorded results

Two runs on 2026-11-17, full chr22 index (39,159,057 keys), mixed queries:

| Backend | First run (ns/query) | Repeat (ns/query) |
| --- | ---: | ---: |
| `std::lower_bound` | 684.400 | 621.375 |
| Eytzinger + prefetch | 311.973 | 289.279 |
| Counting | 129.151 | 115.960 |
| Packed SIMD | 134.958 | 117.451 |
| Batch 32 | 51.929 | 44.391 |
| Batch 32 + prefetch | 37.900 | 35.192 |

Batch 32 with prefetch had about 18× standard-search throughput and 8.2×
Eytzinger throughput in both runs. Both indexes return sorted positions;
Eytzinger retains a position map and its storage cost is included in the CSV.
These ratios exclude construction and output allocation.

[First-run raw data](https://github.com/jdeepak1218/static-search-tree/blob/results-archive/results/library-polish/comparison-avx2-real.csv)
and [repeat raw data](https://github.com/jdeepak1218/static-search-tree/blob/results-archive/results/library-polish/comparison-avx2-repeat-real.csv)
are stored on `results-archive`, alongside the historical experiment files.
Both CSVs contain all 20 methods across three input sizes and three query patterns.
All 613,431,000 warm-up/measured outputs were checked. CSV method coverage,
trial summaries, and cross-method/repeat checksums were also verified.

The runs used GCC 13.3, `-O3 -DNDEBUG -mavx2 -mpopcnt`, on an i7-12700H
under WSL2. They had no affinity, frequency lock, explicit huge pages, or cold-cache
control. They measure throughput on resident data. Nearby methods can have
overlapping trial ranges. Historical raw CSVs and reports remain available in
[revision b11610e](https://github.com/jdeepak1218/static-search-tree/tree/b11610eda455449983d5e93c52f9705ee45dec80/benchmarks).

On native Linux, use `lscpu -e` and the host's CPU topology to identify a P-core,
then run `taskset -c <cpu> ./build/batch_benchmark --prefetch ...`. WSL2 exposes
virtual CPUs; pinning a guest CPU alone does not guarantee a physical P-core.

## Real input

[data manifest](real_dataset.json) records the source URL, SHA-256 hashes, and
encoding for 39,159,057 valid forward 16-mers from hg38 chromosome 22. Encode
A/C/G/T as 0/1/2/3 into little-endian uint32 values, resetting on ambiguous bases
and FASTA headers. The executable sorts the values and retains duplicates.
`data/chr22.u32` is an ignored local file, not a repository dependency.
The query traffic is generated, even when the indexed keys are DNA-derived.
