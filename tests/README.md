# Correctness checks

From the project root:

```bash
mkdir -p build
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/reference_test.cpp -o build/reference_test
./build/reference_test
g++ -std=c++20 -O2 -D_GLIBCXX_ASSERTIONS -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/eytzinger_test.cpp -o build/eytzinger_test
./build/eytzinger_test
g++ -std=c++20 -O1 -g -D_GLIBCXX_ASSERTIONS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Iinclude tests/eytzinger_test.cpp -o build/eytzinger_test_sanitized
./build/eytzinger_test_sanitized
g++ -std=c++20 -O2 -DNDEBUG -DSST_DISABLE_PREFETCH -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/eytzinger_test.cpp -o build/eytzinger_test_fallback
./build/eytzinger_test_fallback
```

If LeakSanitizer reports that it cannot run under ptrace, use
`ASAN_OPTIONS=detect_leaks=0 ./build/eytzinger_test_sanitized`.
This retains address and undefined-behavior checks but does not check for leaks.

Keep assertions enabled in `reference_test.cpp`. The dedicated Eytzinger suite
uses explicit failure checks that remain active even with `-DNDEBUG`.

## Coverage

- The existing reference checks compare all Eytzinger methods as well.
- All 286 sorted arrays of length zero through ten over `{0, 1, UINT32_MAX}`.
- Lengths near powers of two through `2^17`, including incomplete last levels.
- Every length from zero through 513, probing every key and adjacent values.
  This covers the first lookahead thresholds and incomplete descendant blocks.
- 500 seeded randomized cases: full-range keys, small alphabets, all zero,
  all maximum, and duplicate-heavy keys close to the maximum.
- A 1,000,003-element random array and an 8,388,731-element duplicate-heavy
  array with an extreme final key.
- Queries for zero, maximum, the signed/unsigned boundary, random values, sampled
  exact keys, and their neighbors. Sampling includes large duplicate runs.
- Binary search and all index-returning Eytzinger methods compared to `std::lower_bound`;
  the value-returning optimized method is checked against the corresponding reference key/sentinel:
  867,795 query cases across 1,391 datasets.
- Four invalid unsorted arrays must throw `std::invalid_argument`.
- Ownership after source mutation, copying, moving, and copy assignment.
- The aligned allocator is checked at several allocation sizes.
- optimized traversal is checked with and without prefetching; its value API is checked
  alongside its original-position API.
- The same suite passes with hints disabled and with `-DNDEBUG`; explicit
  differential checks do not depend on assertions.

Construction validates sorted input once. The baseline binary-search functions
still have a sorted-input precondition; invalid arrays are not passed to them.

During development, three temporary header mutations were compiled and run
against this suite: changing `>=` to `>`, returning a tree position rather than
the original sorted index, and skipping the final tree position. All three were
detected. These temporary mutations are not present in the project.

This coverage does not prove correctness for every possible input. Allocation
failure injection, physically unallocatable lengths, concurrent execution, and
other compiler/platform combinations have not been tested in this stage.

The optimized port additionally passed a standalone-header, repeated-inclusion, and
two-translation-unit compilation/link/run check with GCC 13.3.

## Scalar S+ tree

```bash
g++ -std=c++20 -O2 -D_GLIBCXX_ASSERTIONS -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/stree_test.cpp -o build/stree_test
./build/stree_test
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/stree_test.cpp -o build/stree_test_release
./build/stree_test_release
g++ -std=c++20 -O1 -g -D_GLIBCXX_ASSERTIONS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Iinclude tests/stree_test.cpp -o build/stree_test_sanitized
ASAN_OPTIONS=detect_leaks=0 ./build/stree_test_sanitized
```

The initial scalar suite checked 4,657,931 queries across 3,697 datasets, using node widths
2 and 16. It includes every length from zero through 1,025, exhaustive sorted
small-alphabet inputs through length ten, capacities near fanout powers, 500
randomized distributions, full-range keys, maximum-only input, duplicate runs,
and arrays with 1,000,003 and 8,388,731 keys. Explicit failure checks survive
`-DNDEBUG`. Unsorted input, leaf spill, first duplicates, source ownership,
copying, moving, and assignment are checked separately.

Temporary changes to equality comparison, separator selection, and leaf spill
were each detected by the suite. A standalone-header, repeated-inclusion, and
two-translation-unit check also passed, including a cross-leaf query. No temporary
mutants are retained. Sanitizer success is evidence for tested executions, not
proof of correctness for all inputs. Leak detection remains unavailable here;
allocation failure, concurrency, and other platforms remain untested.

## Counting search

The current suite checks both early-exit and counting search: 4,697,951 query
cases across 3,777 datasets. Additional widths 1, 3, 7, 8, and 15 exercise small
reductions and vector tails alongside the existing widths 2 and 16. Generic,
AVX2, vectorization-disabled, and ASan/UBSan builds pass on the tested machine.
CPU AVX2 support was checked before executing AVX2 binaries.

```bash
g++ -std=c++20 -O3 -mavx2 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/stree_test.cpp -o build/stree_test_avx2
./build/stree_test_avx2
g++ -std=c++20 -O3 -DNDEBUG -fno-tree-loop-vectorize -fno-tree-slp-vectorize -Iinclude tests/stree_test.cpp -o build/stree_test_no_vector
./build/stree_test_no_vector
```

For an AVX2 sanitizer build, add `-mavx2` to the sanitizer command above.
Three temporary counting mutations were detected: using `<=`, comparing keys
as signed integers, and overwriting rather than accumulating the count. Header
integration checks pass with the new method. The code-generation probe and
inspection instructions are in `docs/stree-counting.md`.

## Ordered-mask search

The suite now checks early exit, counting, and mask search against the same
reference for all 4,697,951 query cases across 3,777 datasets. An additional 51
explicit assertions cover every suffix mask of a sixteen-key node: all-match,
no-match, positions across the 7/8 half boundary, and full-range maximum keys.

Generic, AVX2 release, explicit-backend-disabled, and AVX2 ASan/UBSan builds pass.
To exercise the portable mask path while keeping other AVX2 code enabled:

```bash
g++ -std=c++20 -O3 -mavx2 -DSST_DISABLE_MASK_SIMD -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/stree_test.cpp -o build/stree_test_mask_fallback
./build/stree_test_mask_fallback
```

Temporary mutations reversing upper-half placement, removing the end marker,
and using signed SIMD minimum were detected. No mutants are retained. Header
integration checks also pass with the mask API. Sanitizer limitations and
untested platform/allocation-failure cases remain as described above.

## Population-count search

The current suite checks early exit, counting, trailing-zero masks, and population
counts against the reference for 4,697,951 query cases across 3,777 datasets.
There are 51 new explicit population assertions for every possible lane count,
zero/full masks, half-vector boundaries, and maximum keys, alongside the existing
51 trailing-zero assertions. Ownership, move, and assignment calls include the
new API.

```bash
g++ -std=c++20 -O3 -mavx2 -mpopcnt -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/stree_test.cpp -o build/stree_test_popcount
./build/stree_test_popcount
g++ -std=c++20 -O3 -mavx2 -mno-popcnt -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/stree_test.cpp -o build/stree_test_software_popcount
./build/stree_test_software_popcount
g++ -std=c++20 -O3 -mavx2 -mpopcnt -DSST_DISABLE_MASK_SIMD -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/stree_test.cpp -o build/stree_test_popcount_fallback
./build/stree_test_popcount_fallback
```

Add `-mavx2 -mpopcnt` to the sanitizer command for the explicit backend. Generic,
hardware-popcount, software-popcount, backend-disabled, and ASan/UBSan builds
pass. Existing reference and Eytzinger checks pass. Mutations carrying an end
marker into the count, overlapping half masks, and using a 31-bit-only signed
comparison were detected. Header-integration checks pass. No temporary mutants
are retained. Existing sanitizer/platform limitations still apply.

## Packed comparison masks

All five S+ query methods are checked against the reference for 4,697,951 cases
across 3,777 datasets. Another 51 packed assertions cover every lane count,
empty/full masks, half-vector boundaries, maximum keys, and the highest mask bit.
The existing explicit mask/population assertions remain active.

```bash
g++ -std=c++20 -O3 -mavx2 -mpopcnt -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/stree_test.cpp -o build/stree_test_packed
./build/stree_test_packed
g++ -std=c++20 -O3 -mavx2 -mno-popcnt -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/stree_test.cpp -o build/stree_test_packed_software
./build/stree_test_packed_software
g++ -std=c++20 -O3 -mavx2 -mpopcnt -DSST_DISABLE_MASK_SIMD -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/stree_test.cpp -o build/stree_test_packed_fallback
./build/stree_test_packed_fallback
```

Generic optimized and AVX2 ASan/UBSan configurations pass too. Use the existing sanitizer
command with `-mavx2 -mpopcnt` for the explicit backend. Temporary mutants missing
the divide-by-two operation, using unsigned saturation, and dropping bit 31 were
caught. Header, repeated-inclusion, and two-translation-unit checks pass. No
mutants are retained. Existing environment and sanitizer limitations still apply.

## Comprehensive workload validation

Run `bash tools/run_validation.sh` for all existing suites, all 16 node widths,
20 random seeds, realistic query patterns, concurrent reads, data-input checks,
four release configurations, and ASan/UBSan. See
[coverage and reproduction](../docs/workload-validation.md) and the
[measured report](../docs/workload-results.md). Release smoke checks remain active
under `-DNDEBUG`. Raw validation output is in
`benchmarks/workload-validation.txt`. ThreadSanitizer could not start in this
WSL environment; leak checking is disabled for sanitizer runs.

## Batched lookup

`batch_test.cpp` verifies fixed arrays and arbitrary spans against the reference
and individual packed lookup. It covers all node widths, batch sizes 1 through
128 plus size 3, tails, guards, invalid output lengths, ownership, and concurrent
readers. The validation runner includes it in all four release configurations
and ASan/UBSan. An optional dataset argument checks the full real-key index.
See [batch behavior and commands](../docs/stree-batching.md) and
[measured results](../docs/batch-results.md).

## Batched prefetch validation

The batch suite now checks both ordinary and prefetched fixed-array/span APIs:
8,425,692 results per configuration. The runner also covers
`SST_DISABLE_PREFETCH`. Separate release and ASan/UBSan runs check 9,439,464
results each on all 39,159,057 real keys. Code-generation inspection confirms
hints are emitted and disappear under the disable macro. See
[behavior and commands](../docs/stree-batch-prefetch.md) and
[results](../docs/batch-prefetch-results.md).
