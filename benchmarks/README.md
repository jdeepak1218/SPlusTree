# Search benchmarks

From the project root, compile the benchmark independently of sanitizer builds:

```bash
mkdir -p build
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark
./build/search_benchmark uniform indices > benchmarks/optimized_uniform.csv
./build/search_benchmark duplicates indices > benchmarks/optimized_duplicates.csv
./build/search_benchmark equal indices > benchmarks/optimized_equal.csv
./build/search_benchmark uniform indices > benchmarks/optimized_uniform_repeat.csv
./build/search_benchmark uniform values > benchmarks/optimized_values_uniform.csv
./build/search_benchmark duplicates values > benchmarks/optimized_values_duplicates.csv
./build/search_benchmark equal values > benchmarks/optimized_values_equal.csv
```

Run the commands sequentially on an otherwise idle machine. Correctness and
sanitizer commands are in `tests/README.md`. The default workload is `uniform`
and the default result mode is `indices`.

## What is compared

Index mode compares six methods: standard lower bound, binary search, original
candidate-tracking Eytzinger, the earlier two-hint/T2 experiment, the optimized
T0-prefetched traversal, and that same optimized traversal without prefetching.
All Eytzinger methods now use the same 64-byte-aligned key storage and position
map. Comparing the two optimized methods isolates prefetching within that traversal.
The map is read once after the optimized traversal, preserving our index contract.

Value mode separately compares standard lower bound returning a key/sentinel
with the optimized value-returning operation. This mode does not read the
position map during queries, though the object still retains it for index-mode
support. Outputs are stored in the same `size_t` buffers for both methods.
Do not compare value-mode checksums or timings as though they were index-mode
results; their contracts differ.

## Measurement

- One million independent queries per pass; fixed seed 42 for data and queries.
- One full warm-up, then twelve measured trials per implementation.
- Index-mode order rotates across six methods; each gets every execution
  position twice. Value-mode order alternates across two methods.
- Timings include search and preallocated result-buffer writes. Generation,
  sorting, construction, allocation, result comparisons, and printing are outside
  the timed region. Every warm-up and trial output is compared element by element
  with the reference, even with `-DNDEBUG`.
- Median is the mean of the middle two sorted timings. CSVs retain median,
  minimum, maximum, and all twelve original trial timings in ns/query.
- These are independent-query throughput measurements, not dependent-query latency.
- Six uniform input sizes: 1,024; 65,536; 1,048,576; 8,388,608; 16,777,339;
  33,554,555 keys. Uniform keys and queries are 31-bit samples, matching the
  initial uniform distribution. Full-range values are covered in correctness tests.
- Duplicate/equal modes use the 1,048,576 and 33,554,555 sizes. Duplicates have
  eight distinct values spanning almost the full unsigned range; equal keys are
  `0x80000000`. Half the queries are sampled hits; half are full-range random.
- All seven commands execute 1,392 million measured searches and 116 million
  warm-up searches. The repeated uniform process checks whether conclusions
  persist across separate executions with identical generated data.

`storage_bytes` reports vector buffer capacities: sorted keys for baselines,
or aligned keys plus original-position map for Eytzinger. It excludes allocator
metadata, vector objects, queries, outputs, and temporary storage. The benchmark
retains both representations as well as output buffers, so process memory is
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

Historical `baseline.csv`, `eytzinger*.csv`, and `prefetch*.csv` files retain
older harness/layout results. Compare methods within the current `optimized*.csv`
files: earlier files used different alignment, trial counts, and method sets.
Upstream source links, license, and retrieved-file hashes are in `third_party/`.
