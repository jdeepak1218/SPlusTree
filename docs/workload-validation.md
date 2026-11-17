# Workload validation

The validation adds independent boundary checks to differential testing, probes
every supported node width, and measures query distributions separately. It does
not change the search algorithms or choose a new default implementation.

## Reproduce

Requirements: C++20, GCC with x86 AVX2/POPCNT support, and Bash.
The targeted executable requires those CPU instructions. The generic executable
does not require AVX2. Run correctness checks before benchmarking; do not run
tests, compilation, or other benchmarks concurrently with timed measurements.

```bash
mkdir -p build
bash tools/run_validation.sh > build/validation.log 2>&1
```

For real-data benchmarks, supply `data/chr22.u32` matching the encoding and
hashes in `benchmarks/real_dataset.json`. The prepared dataset remains available
locally under ignored `data/`. Dataset conversion and standalone CSV audit tools
are no longer included. The C++ benchmark executables still validate every search
answer before reporting success.

```bash
bash tools/run_workload_benchmarks.sh
```

Large inputs and derived files live under ignored `data/`. The small provenance
manifest is retained as `benchmarks/real_dataset.json`. The benchmark script
replaces its six CSV files, so save an earlier run before comparing machines.

## Correctness coverage

- Existing reference, Eytzinger, and S+ suites remain part of the validation.
- `reference_test.cpp` uses explicit checks with source-line diagnostics, so
  `-DNDEBUG` cannot silently remove the test. The original assertions could be
  disabled in release builds; that test weakness is corrected.
- `robustness_test.cpp` checks every S+ node width from 1 through 16 around
  leaf and three-level capacity transitions, including incomplete final nodes.
  Duplicate runs span separators, include the unsigned sign boundary, and end
  in genuine `UINT32_MAX` keys that equal the padding value.
- Twenty deterministic seeds exercise uniform full-range keys, clustered IDs,
  dense even IDs, and all-equal keys. Queries include sampled hits, mixed probes,
  sorted and reverse order, hot keys, and integer extremes.
- Every result is compared with `std::lower_bound`. Independent conditions
  require `result <= size`, `values[result] >= query` when present, and
  `values[result - 1] < query` when present. Increasing query pairs must produce
  nondecreasing positions. The value-returning Eytzinger API is checked too.
- Four threads read the same trees, with 32,768 queries per thread and nine
  index APIs checked per query. Trees are copied, originals reassigned, and
  source storage released before the readers start.
- Binary-data fixtures verify byte order, zero, the unsigned sign boundary,
  maximum values, and rejection of empty, missing, or partial files. Ordered
  query generation and guaranteed hit/miss cases have their own checks.
- The recorded dataset preparation audit checked the DNA encoder against a
  separate window-by-window implementation. That historical check is not part
  of the current C++ validation runner.

Release tests cover generic code, AVX2 with hardware population count, AVX2 with
the explicit mask backend disabled, and AVX2 with software population count.
AddressSanitizer plus UndefinedBehaviorSanitizer checks all five test programs,
with library assertions enabled and sanitizer errors fatal. Leak detection is
disabled because this environment does not support the leak checker reliably.

The observed concurrent-read checks are not a substitute for a race detector.
ThreadSanitizer was attempted separately but failed at startup with
`FATAL: ThreadSanitizer: unexpected memory mapping`; it did not run the tests.

## Workloads

Synthetic keys model dense IDs, partitioned/clustered IDs with duplicates,
uniform full-range integers, and an all-equal worst case. These are simulated
workloads, not captured production traces. Sizes range from 1,024 through
33,554,555 keys, with non-power-of-two large arrays. Query patterns are:

| Pattern | Definition |
| --- | --- |
| hits | Sample stored keys; 100% hits |
| misses | Odd probes against dense even IDs; 0% hits |
| mixed | Alternate sampled stored keys and uniform 32-bit probes |
| hot | 90% of probes select among 64 fixed sampled positions |
| sorted / reverse | The same mixed queries sorted in either direction |
| boundaries | Alternate integer extremes and sampled keys plus one |

The exact observed hit count is recorded rather than assuming mixed means
exactly 50% hits: a random probe may itself be present. Hot queries can select
fewer than 64 distinct values when keys repeat. Dense miss probes are confined
to gaps between nearby IDs; they are not all trivial out-of-range misses.

Real keys come from the public UCSC hg38 chromosome 22 reference FASTA. The
encoder maps A/C/G/T to 0/1/2/3 and packs consecutive forward 16-base windows
into unsigned 32-bit keys. Ambiguous bases reset the window. Repeated windows
are retained; no sequence is duplicated to manufacture a larger dataset. This
produces 39,159,057 keys. Prefixes of 1,048,576 and 8,388,608 windows and the
entire dataset are independently sorted and benchmarked. Prefixes represent
the beginning of the sequence, not a random genome sample. Query probes over
these genuine keys are synthetic mixed, hot, and sorted queries; this is not
a measured genomics application query trace.

## Measurement design

Each of 43 dataset/query cases uses 131,072 deterministic probes, eight measured
passes per method, and one full warm-up pass. Eight methods participate:
standard lower bound, manual binary search, optimized Eytzinger, and all five
S+ implementations. Thus each method performs 1,048,576 measured searches per
case. Every timed output and warm-up output is checked individually after the
timer stops. Release failures abort instead of relying on assertions or merely
comparing checksums.

Methods are dispatched outside the timed loop. Their order starts from a
deterministic shuffle and rotates between trials; each method occupies every
run position once. Generation, sorting, tree construction, expected-result
calculation, validation, and printing are excluded from query timing. Timed
loops include the same result-buffer stores for all methods. CSV files retain
all eight trials, median, minimum, maximum, observed hit counts, distinct key
counts, order-sensitive result checksum, index allocation bytes, and build time.
Build time is a single observation per case, not a repeated construction study.
Storage is index-owned allocated capacity, not process RSS; callers may also
retain their input vector.

The runner performs AVX2/POPCNT, an independent repeat of that build, and a
generic build, serially. Compare methods within a build and workload. Target
flags affect the whole executable; cross-build differences cannot be attributed
solely to one intrinsic. Sanitized executables are never timed.

These measurements describe resident indexes and repeated independent-query
throughput. There is no CPU affinity, frequency lock, cache flushing, huge-page
configuration, dependent-query latency measurement, multithread throughput,
allocation-failure injection, or non-x86 validation. WSL scheduling and host
activity can move absolute timings. Eight passes plus a repeat help expose
variation, but do not establish statistical significance or a universal winner.

Measured results and conclusions are recorded in `workload-results.md`.
