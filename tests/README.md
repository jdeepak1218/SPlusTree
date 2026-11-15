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

The scalar suite checks 4,657,931 queries across 3,697 datasets, using node widths
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
