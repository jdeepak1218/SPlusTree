# Search benchmarks

Run commands from the project root:

```bash
mkdir -p build
g++ -std=c++20 -Wall -Wextra -Wpedantic -g -Iinclude tests/reference_test.cpp -o build/reference_test
./build/reference_test
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark
./build/search_benchmark uniform > benchmarks/prefetch_uniform.csv
./build/search_benchmark duplicates > benchmarks/prefetch_duplicates.csv
./build/search_benchmark equal > benchmarks/prefetch_equal.csv
./build/search_benchmark uniform > benchmarks/prefetch_uniform_repeat.csv
```

The header in `include/static_search_tree.hpp` currently contains only the
standard-library reference and our binary-search baseline. `include/eytzinger.hpp`
contains both plain Eytzinger search and four-level lookahead prefetch search.
GCC/Clang use `__builtin_prefetch`; other compilers use the same traversal
without hints. Define `SST_DISABLE_PREFETCH` consistently across translation
units to disable hints explicitly.

## Workload and measurement

- Six input sizes from 1,024 to 33,554,555 keys (about 128 MiB of keys at the
  largest size). The two largest sizes are deliberately not powers of two.
- Sorted uniformly sampled 31-bit unsigned keys, with duplicates allowed, and
  independent uniformly sampled queries. This follows the blog's initial data
  distribution; correctness tests separately cover full-range unsigned values.
- Fixed seed `42` for each dataset. Reproduction of the random sequence assumes
  the same standard-library implementation.
- One million queries per pass, using the same data and queries for all searches.
- One full warm-up per implementation, followed by eight measured trials each.
  Execution order rotates across four implementations, giving each every
  execution position twice. The median is the mean of the middle two timings.
- Timings include search and writing each result to preallocated output storage.
  Generation, sorting, allocation, validation, and CSV printing are outside the
  measured region.
- Every trial's outputs are compared element by element, even with `-DNDEBUG`.
  A final checksum is printed for each implementation.
- CSV records median, minimum, maximum, and all eight raw trial timings in
  nanoseconds per query. These measure
  independent-query throughput, not dependent single-query latency.

The uniform run executes 192 million measured searches and 24 million warm-up
searches. The `duplicates` and `equal` runs each use the 1,048,576 and 33,554,555
key sizes with the same query counts and trial counts. Duplicate input contains
eight key clusters spanning almost the full unsigned range. Equal input contains
only `0x80000000`. For these stress workloads, half the queries are sampled
exact hits and half are full-range random queries. Each separate CSV filename
identifies its workload. Each stress run executes 64 million measured searches
and eight million warm-up searches. Repeating the uniform run gives a total of
512 million measured searches and 64 million warm-up searches across the four
commands above. Run these commands sequentially; parallel timing runs compete
for CPU and memory resources. More repetitions help expose variation; they do not remove systematic
bias or make a workload representative of every application.

## Recorded baseline environment

`baseline.csv` was generated on 2026-11-15 with:

- GCC 13.3.0, Ubuntu package `13.3.0-6ubuntu2~24.04.1`.
- Flags: `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude`.
- Reported CPU: Intel Core i7-12700H, x86-64.
- Microsoft hypervisor, with two logical CPUs visible to this environment.
- Reported caches: 48 KiB L1 data, 1.3 MiB L2, 24 MiB L3.

No CPU affinity, fixed frequency, explicit huge-page allocation, or cold-cache
flushing is configured. Run on an otherwise idle machine and retain the timing
spread when comparing results. Virtualization and host activity can affect these
measurements. Data size is the key storage size, not total process memory.

The current timing helper is templated so all four search callables can be
inlined. The old `baseline.csv` used a function-pointer helper. The older
`eytzinger*.csv` files used three methods and seven trials. Compare implementations
within the current `prefetch*.csv` files, rather than comparing different harness
versions. These timings do not isolate individual instruction costs.

`storage_bytes` reports the vector buffer capacities required by each search:
the sorted input for the baselines, or Eytzinger's keys plus sorted-position map.
It excludes allocator metadata and query/result buffers. The benchmark retains
the sorted input as a correctness oracle, so its process memory is larger.
Eytzinger uses approximately three times the key-only storage on this 64-bit
platform. Its mapping preserves original sorted indices, which differs from
the blog's value-returning lookup. `build_ms` is one construction measurement,
including sorted-input validation and allocation, and excludes input sorting;
it is not a repeated construction benchmark. Baselines have no extra build step.
