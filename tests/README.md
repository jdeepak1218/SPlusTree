# Correctness checks

From the project root:

```bash
mkdir -p build
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/reference_test.cpp -o build/reference_test
./build/reference_test
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Iinclude tests/eytzinger_test.cpp -o build/eytzinger_test
./build/eytzinger_test
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude tests/eytzinger_test.cpp -o build/eytzinger_test_sanitized
./build/eytzinger_test_sanitized
```

If LeakSanitizer reports that it cannot run under ptrace, use
`ASAN_OPTIONS=detect_leaks=0 ./build/eytzinger_test_sanitized`.
This retains address and undefined-behavior checks but does not check for leaks.

Keep assertions enabled in `reference_test.cpp`. The dedicated Eytzinger suite
uses explicit failure checks that remain active even with `-DNDEBUG`.

## Coverage

- The existing reference checks now compare Eytzinger results as well.
- All 286 sorted arrays of length zero through ten over `{0, 1, UINT32_MAX}`.
- Lengths near powers of two through `2^17`, including incomplete last levels.
- 500 seeded randomized cases: full-range keys, small alphabets, all zero,
  all maximum, and duplicate-heavy keys close to the maximum.
- A 1,000,003-element random array and an 8,388,731-element duplicate-heavy
  array with an extreme final key.
- Queries for zero, maximum, the signed/unsigned boundary, random values, sampled
  exact keys, and their neighbors. Sampling includes large duplicate runs.
- Both binary search and Eytzinger compared to `std::lower_bound`: 338,250 query
  comparisons across 877 datasets.
- Four invalid unsorted arrays must throw `std::invalid_argument`.
- Ownership after source mutation, copying, moving, and copy assignment.

Construction validates sorted input once. The baseline binary-search functions
still have a sorted-input precondition; invalid arrays are not passed to them.

During development, three temporary header mutations were compiled and run
against this suite: changing `>=` to `>`, returning a tree position rather than
the original sorted index, and skipping the final tree position. All three were
detected. These temporary mutations are not present in the project.

This coverage does not prove correctness for every possible input. Allocation
failure injection, physically unallocatable lengths, concurrent execution, and
other compiler/platform combinations have not been tested in this stage.
