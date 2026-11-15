# Binary search baseline

Run commands from the project root:

```bash
mkdir -p build
g++ -std=c++20 -Wall -Wextra -Wpedantic -g -Iinclude tests/reference_test.cpp -o build/reference_test
./build/reference_test
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude benchmarks/search_benchmark.cpp -o build/search_benchmark
./build/search_benchmark > benchmarks/baseline.csv
```

The header in `include/static_search_tree.hpp` currently contains only the
standard-library reference and our binary-search baseline. A tree implementation
will follow in later stages.

## Workload and measurement

- Six input sizes from 1,024 to 33,554,555 keys (about 128 MiB of keys at the
  largest size). The two largest sizes are deliberately not powers of two.
- Sorted uniformly sampled 31-bit unsigned keys, with duplicates allowed, and
  independent uniformly sampled queries. This follows the blog's initial data
  distribution; correctness tests separately cover full-range unsigned values.
- Fixed seed `42` for each dataset. Reproduction of the random sequence assumes
  the same standard-library implementation.
- One million queries per pass, using the same data and queries for both searches.
- One full warm-up per implementation, followed by seven measured trials each.
  Execution order alternates between trials.
- Timings include search and writing each result to preallocated output storage.
  Generation, sorting, allocation, validation, and CSV printing are outside the
  measured region.
- Every trial's outputs are compared element by element, even with `-DNDEBUG`.
  A final checksum is printed for each implementation.
- CSV records median, minimum, and maximum nanoseconds per query. These measure
  independent-query throughput, not dependent single-query latency.

The full run executes 84 million measured searches and 12 million warm-up
searches. More repetitions help expose variation; they do not remove systematic
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

The common timing helper accepts a function pointer; depending on compiler
optimization, call overhead may affect small-input timings. This is an initial
baseline harness, not evidence of intrinsic instruction-level performance.
Broader distributions, sizes around cache boundaries, and optimized tree
implementations will be added as we follow the blog.
