# Search benchmarks

From the project root, compile the benchmark independently of sanitizer builds:

```bash
mkdir -p build
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark
./build/search_benchmark uniform indices > benchmarks/stree_uniform.csv
./build/search_benchmark duplicates indices > benchmarks/stree_duplicates.csv
./build/search_benchmark equal indices > benchmarks/stree_equal.csv
./build/search_benchmark uniform indices > benchmarks/stree_uniform_repeat.csv
```

Run the commands sequentially on an otherwise idle machine. Correctness and
sanitizer commands are in `tests/README.md`. The default workload is `uniform`
and the default result mode is `indices`.

## What is compared

Index mode compares seven methods: standard lower bound, binary search, original
candidate-tracking Eytzinger, the earlier two-hint/T2 experiment, the optimized
T0-prefetched traversal, that same optimized traversal without prefetching, and a scalar S+ tree.
All Eytzinger methods now use the same 64-byte-aligned key storage and position
map. The S+ tree stores leaves and internal separators without a position map.
Comparing the two optimized methods isolates prefetching within that traversal.
The map is read once after the optimized traversal, preserving our index contract.

Value mode separately compares standard lower bound returning a key/sentinel
with the optimized value-returning operation. This mode does not read the
position map during queries, though the object still retains it for index-mode
support. Outputs are stored in the same `size_t` buffers for both methods.
Do not compare value-mode checksums or timings as though they were index-mode
results; their contracts differ.

## Measurement

- One million independent queries per pass; fixed seed 42 for data and queries.
- One full warm-up, then fourteen measured trials per implementation.
- Index-mode order rotates across seven methods; each gets every execution
  position twice. Value-mode order alternates across two methods.
- Timings include search and preallocated result-buffer writes. Generation,
  sorting, construction, allocation, result comparisons, and printing are outside
  the timed region. Every warm-up and trial output is compared element by element
  with the reference, even with `-DNDEBUG`.
- Median is the mean of the middle two sorted timings. CSVs retain median,
  minimum, maximum, and all fourteen original trial timings in ns/query.
- These are independent-query throughput measurements, not dependent-query latency.
- Six uniform input sizes: 1,024; 65,536; 1,048,576; 8,388,608; 16,777,339;
  33,554,555 keys. Uniform keys and queries are 31-bit samples, matching the
  initial uniform distribution. Full-range values are covered in correctness tests.
- Duplicate/equal modes use the 1,048,576 and 33,554,555 sizes. Duplicates have
  eight distinct values spanning almost the full unsigned range; equal keys are
  `0x80000000`. Half the queries are sampled hits; half are full-range random.
- The four commands execute 1,568 million measured searches and 112 million
  warm-up searches. The repeated uniform process checks whether conclusions
  persist across separate executions with identical generated data.

`storage_bytes` reports vector buffer capacities: sorted keys for baselines,
aligned keys plus original-position map for Eytzinger, or aligned nodes and
level offsets for S+ trees. It excludes allocator
metadata, vector objects, queries, outputs, and temporary storage. The benchmark
retains the input and both indexes as well as output buffers, so process memory is
larger. Both optimized wrappers share the same storage. `build_ms` is one observation,
including sorted-input validation and allocation, excluding sorting. It is not
a repeated construction benchmark.

## Reproduction limits

Recorded runs use GCC 13.3.0 on x86-64 WSL2, reported i7-12700H, two visible logical
CPUs, and previously reported 48 KiB L1 data, 1.3 MiB L2, and 24 MiB L3 caches.
There is no CPU affinity, fixed frequency, explicit huge-page allocation, or
cold-cache flushing. Transparent huge pages are not requested or verified.
Results depend on the machine and allocation setup. Host activity and virtualization can change timings.

The key buffer is explicitly aligned to 64 bytes. The optimized prefetch path emits
T0 hints on the tested GCC x86-64 build; confirm with:

```bash
objdump -d build/search_benchmark | rg 'prefetcht0'
```

`SST_DISABLE_PREFETCH` disables hints and must be consistent across translation
units. Unsupported compilers also retain traversal without hints. Other compiler
and architecture combinations have not been validated.

Historical `baseline.csv`, `eytzinger*.csv`, `prefetch*.csv`, and `optimized*.csv`
files retain older harness/layout results. Compare methods within the current
`stree*.csv` files: earlier files used different trial counts and method sets.
The value mode remains available but does not yet include S+ tree searches.
Upstream source links, license, and retrieved-file hashes are in `third_party/`.

## Counting and auto-vectorization comparison

This focused mode compares standard lower bound, optimized Eytzinger, early-exit
S+ search, and counting S+ search. It uses the same tree for both S+ methods,
with no additional persistent storage. Twelve trials rotate four methods so
each gets every execution position three times. All outputs are checked against
the standard reference outside timed regions. Existing `indices` and `values`
modes retain their fourteen-trial behavior.

Build three executables with the same base options:

```bash
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark
g++ -std=c++20 -O3 -mavx2 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark_avx2
g++ -std=c++20 -O3 -DNDEBUG -fno-tree-loop-vectorize -fno-tree-slp-vectorize -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark_no_vector
./build/search_benchmark uniform counting > benchmarks/count_generic_uniform.csv
./build/search_benchmark duplicates counting > benchmarks/count_generic_duplicates.csv
./build/search_benchmark equal counting > benchmarks/count_generic_equal.csv
./build/search_benchmark_avx2 uniform counting > benchmarks/count_avx2_uniform.csv
./build/search_benchmark_avx2 duplicates counting > benchmarks/count_avx2_duplicates.csv
./build/search_benchmark_avx2 equal counting > benchmarks/count_avx2_equal.csv
./build/search_benchmark_avx2 uniform counting > benchmarks/count_avx2_uniform_repeat.csv
./build/search_benchmark_no_vector uniform counting > benchmarks/count_no_vector_uniform.csv
```

Execute AVX2 binaries only on an AVX2-capable CPU/OS. Support was checked on the
recorded machine. Commands run sequentially: 1,536 million measured searches
and 128 million warm-up searches in total. Generic and AVX2 workloads both cover
uniform, duplicate-heavy, and all-equal input. The AVX2 uniform workload repeats
in a separate process. The vectorization-disabled control uses uniform input.
Each build applies its flags to all methods, so comparisons across builds also
include other compiler-code-generation changes and separate-process variation.
Within-build early/count comparisons are the primary performance evidence.

Compiler diagnostics and assembly checks are described in
`docs/stree-counting.md`; detailed timing tables are in `docs/count-results.md`.
The counting loop uses no SIMD intrinsics. GCC's unrolling hint allows its
vectorizer to see reductions in both internal nodes and leaves. No runtime ISA
dispatch is added. Other compilers may choose different code generation.

## Ordered-mask comparison

The `mask` mode compares standard lower bound, optimized Eytzinger, early-exit
S+ search, counting S+ search, and ordered-mask S+ search. Each uses one million
queries per pass, one warm-up, and ten trials. Rotating five methods gives each
every execution position twice. Outputs are checked element by element after
every trial; medians, min/max, and all ten raw timings are saved.

```bash
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark
g++ -std=c++20 -O3 -mavx2 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark_avx2
g++ -std=c++20 -O3 -mavx2 -DSST_DISABLE_MASK_SIMD -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark_mask_fallback
./build/search_benchmark uniform mask > benchmarks/mask_generic_uniform.csv
./build/search_benchmark duplicates mask > benchmarks/mask_generic_duplicates.csv
./build/search_benchmark equal mask > benchmarks/mask_generic_equal.csv
./build/search_benchmark_avx2 uniform mask > benchmarks/mask_avx2_uniform.csv
./build/search_benchmark_avx2 duplicates mask > benchmarks/mask_avx2_duplicates.csv
./build/search_benchmark_avx2 equal mask > benchmarks/mask_avx2_equal.csv
./build/search_benchmark_avx2 uniform mask > benchmarks/mask_avx2_uniform_repeat.csv
./build/search_benchmark_mask_fallback uniform mask > benchmarks/mask_fallback_avx2_uniform.csv
```

Run sequentially and only execute AVX2 binaries on a compatible CPU/OS. These
commands execute 1,600 million measured searches and 160 million warm-up searches.
The distributions and sizes are unchanged from the counting comparison. The
fallback control disables only the explicit mask backend; the compiler can still
auto-vectorize its ordinary C++ loop and the counting method. It is not a global
no-SIMD build. Compile all translation units with consistent target flags and
`SST_DISABLE_MASK_SIMD` settings; runtime ISA dispatch is not provided.

The AVX2 mask backend is selected for sixteen-key nodes; other widths and generic
builds use the portable ordered-bit loop. SIMD extracts one bit per 32-bit lane
using register casts, not floating-point arithmetic. No extra persistent storage
is allocated. See `docs/stree-mask.md` and `docs/mask-results.md` for details.

## Population-count comparison

The `popcount` mode compares six methods: standard lower bound, optimized
Eytzinger, early-exit S+, counting S+, trailing-zero S+, and population-count S+.
One million queries per pass, one warm-up, and twelve trials rotate every method
through all six execution positions twice. Raw files retain all trials, min/max,
and medians. Every output is compared against the reference outside timing.

```bash
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark
g++ -std=c++20 -O3 -mavx2 -mpopcnt -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark_popcount
g++ -std=c++20 -O3 -mavx2 -mno-popcnt -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark_software_popcount
./build/search_benchmark uniform popcount > benchmarks/popcount_generic_uniform.csv
./build/search_benchmark duplicates popcount > benchmarks/popcount_generic_duplicates.csv
./build/search_benchmark equal popcount > benchmarks/popcount_generic_equal.csv
./build/search_benchmark_popcount uniform popcount > benchmarks/popcount_avx2_uniform.csv
./build/search_benchmark_popcount duplicates popcount > benchmarks/popcount_avx2_duplicates.csv
./build/search_benchmark_popcount equal popcount > benchmarks/popcount_avx2_equal.csv
./build/search_benchmark_popcount uniform popcount > benchmarks/popcount_avx2_uniform_repeat.csv
./build/search_benchmark_software_popcount uniform popcount > benchmarks/popcount_software_uniform.csv
```

Execute sequentially and only run targeted binaries on a compatible CPU/OS.
These eight commands perform 2,304 million measured searches and 192 million
warm-up searches. The same workload matrix is retained. Hardware POPCNT is
verified in the primary code generation; the `-mno-popcnt` control retains AVX2
comparisons and calls a software helper for the bit count. On this GCC version,
`-mavx2` enables POPCNT, so the explicit disabling flag is necessary for the
control. CPU AVX2 and POPCNT support was checked before execution.

The comparison preserves the full unsigned key range via a sign-bit transform
before signed vector comparison. No marker bit is added. This stage retains the
two half-mask extracts; compressed packing is a later experiment. Persistent
storage and construction are shared across all S+ methods. Cross-build timing
changes include other compiler effects and process variation. See
`docs/stree-popcount.md` and `docs/popcount-results.md` for details.

## Packed comparison-mask experiment

The `packed` mode compares standard lower bound, optimized Eytzinger, early-exit
S+, counting S+, unpacked population-count S+, and packed population-count S+.
Trailing-zero search remains covered by correctness tests but is not retimed in
this focused mode. Twelve trials rotate six methods through all execution
positions twice, with one million queries per pass and one warm-up. All outputs
are checked against the standard reference outside timed regions.

```bash
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark
g++ -std=c++20 -O3 -mavx2 -mpopcnt -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark_packed
g++ -std=c++20 -O3 -mavx2 -mno-popcnt -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark_packed_software
./build/search_benchmark uniform packed > benchmarks/packed_generic_uniform.csv
./build/search_benchmark_packed uniform packed > benchmarks/packed_avx2_uniform.csv
./build/search_benchmark_packed duplicates packed > benchmarks/packed_avx2_duplicates.csv
./build/search_benchmark_packed equal packed > benchmarks/packed_avx2_equal.csv
./build/search_benchmark_packed uniform packed > benchmarks/packed_avx2_uniform_repeat.csv
./build/search_benchmark_packed_software uniform packed > benchmarks/packed_software_uniform.csv
```

Execute sequentially. These six runs perform 2,016 million measured searches
and 168 million warm-up searches. Uniform runs retain all six sizes. Stress
workloads run at one million and 33.6 million keys. The generic and software-
population-count controls use uniform inputs. Targeted executables require
compatible AVX2/POPCNT CPU and OS support. The software control keeps AVX2 and
explicitly disables POPCNT; generic packed search delegates to the existing
portable population-count helper. No runtime ISA dispatch is provided.

All S+ methods share the same buffers and construction. The selection between
the older population-count comparison and this packed comparison happens
outside timing; a compile-time lambda parameter chooses the query method with
no per-query mode branch. Historical result modes remain available. Source,
code-generation checks, and measurements are in `docs/stree-packed.md` and
`docs/packed-results.md`.

## Realistic workload matrix

`workload_benchmark.cpp` measures all eight index-search methods across uniform,
dense, clustered, all-equal, and real chromosome-derived keys. Query cases cover
hits, misses, hot keys, ordered probes, and boundaries, with every result checked.
Run `bash tools/run_workload_benchmarks.sh` after preparing the ignored dataset.
See [reproduction and methodology](../docs/workload-validation.md) and
[results](../docs/workload-results.md). CSV names start with `workloads_`;
`real_dataset.json` records the real input hashes and transformation.

## Batching

Run `bash tools/run_batch_benchmarks.sh` with the prepared chromosome dataset
available, then `python3 tools/check_batch_results.py`. The runner preserves
previous results and writes six `batch_*_real.csv` / `batch_*_synthetic.csv`
files. It compares standard search, counting, individual packed lookup, and
batch sizes 1/2/4/8/16/32/64/128 across 31 cases, with eleven trials and a
query count that exercises incomplete batches. See
[methodology](../docs/stree-batching.md) and [results](../docs/batch-results.md).

## Batched prefetch comparison

`bash tools/run_batch_prefetch_benchmarks.sh` compares matching ordinary and
prefetched batch sizes, plus the existing baselines, across the same 31 cases.
It uses twelve trials and preserves all older results. Run
`python3 tools/check_batch_results.py --prefetch` to check the six new CSVs;
if present, the additional generic hints-disabled real-data control is checked
too. See [methodology](../docs/stree-batch-prefetch.md) and
[measured results](../docs/batch-prefetch-results.md).
