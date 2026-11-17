# Batched lower-bound search

The fixed-size overload processes independent queries level by level:

```cpp
sst::STree<> tree(std::vector<std::uint32_t>{2, 5, 5, 9, 14});
const std::array<std::uint32_t, 4> queries{6, 5, 20, 0};
const auto answers = tree.lower_bound_batch(queries);
// answers: {3, 1, 5, 0}
```

Each query keeps a separate node position, initially zero. For every internal
level, all queries find their respective child before advancing to the next
level. A position is local to its level; the physical node is
`nodes_[offsets_[level] + position]`. Its child position becomes
`position * fanout + child`. The existing packed comparison helper selects the
child. At the leaf, `position * KeysPerNode + slot`, clamped to logical size,
returns the original sorted-array lower-bound index.

The result for a duplicate is its first sorted occurrence. A missing lower bound
returns `size()`. Empty trees return zero for every query. Query order is
preserved. All unsigned 32-bit values remain valid keys and queries.

The span overload handles arbitrary query counts and caller-owned output:

```cpp
std::vector<std::uint32_t> queries{6, 5, 20, 0, 14};
std::vector<std::size_t> answers(queries.size());
tree.lower_bound_batch<4>(queries, answers);
```

It copies each complete group into the fixed-size overload, writes that group's
results, and handles the final incomplete group with individual packed lookups.
An empty query span writes nothing. Query and output spans must be equally long
and must not overlap. Unequal lengths throw `std::invalid_argument` before any
output write. The caller owns and keeps both spans valid for the call.

Both overloads require a positive compile-time batch size. The fixed-size
overload is `noexcept`; the span overload can throw for a length mismatch. There
is no default batch size: choose it explicitly and measure the intended workload.
The methods allocate no heap storage and do not change index storage. Local
arrays scale with batch size; the benchmark covers sizes 1 through 128. The tree
is read-only and concurrent calls must use separate output storage.

Batching exposes independent traversals so the CPU can overlap memory requests.
It targets throughput and does not promise lower latency for an isolated query.
SIMD still operates within each node. There are no additional threads, explicit
prefetch hints, new layouts, or query-splat hoisting in this change. Generic,
backend-disabled, and non-sixteen-key nodes reuse the existing portable helper.

## Validation and measurement

`tests/batch_test.cpp` checks fixed arrays and arbitrary spans, sizes
1/2/3/4/8/16/32/64/128, query counts surrounding batch boundaries, empty and
moved-from trees, maximum padding, duplicate separators, output guard values,
unchanged inputs, rejected length mismatches, copy/move ownership, and four
concurrent readers. All node widths 1..16 are checked, with the complete batch
matrix on widths 2 and 16. Twenty seeds and million-element arrays supplement
the structural cases. Every result is compared against the standard reference
and individual packed lookup, with lower-bound bracket checks.

The optional dataset argument runs separate full real-key validation:

```bash
build/validation/batch_avx2 data/chr22.u32
ASAN_OPTIONS=detect_leaks=0 build/validation/batch_sanitized data/chr22.u32
```

Run the complete release/backend/sanitizer suite with
`bash tools/run_validation.sh`. The separate real-data invocation checks the
full sorted chromosome-derived dataset with mixed, hot, sorted, and boundary
queries. Input provenance is recorded in `benchmarks/real_dataset.json`.

```bash
bash tools/run_batch_benchmarks.sh
```

The benchmark compares standard search, counting, individual packed search, and
eight batch sizes from 1 through 128. It uses 131,075 queries, deliberately
leaving tails for all tested batches above size one. Each method gets one full
warm-up and eleven measured trials, rotating through every run-order position.
Generation, sorting, index construction, expected results, and validation are
outside the query timer. Input copies, local batch state, tail handling, and
output stores are timed. Every output is checked after every timed/warm-up pass.
CSV files retain raw trials and summary statistics, observed hits, result
checksums, build time, and storage bytes. Build time is a single observation.

Synthetic cases cover 1,024 through 33,554,555 keys, uniform/clustered/equal/dense
data, mixed/hot/ordered/hit/miss queries. Real cases use 1,048,576, 8,388,608, and
39,159,057 genuine 16-mer keys, with generated mixed, hot, and sorted queries.
The runner performs targeted AVX2/POPCNT, a targeted repeat, and a generic build
serially. Earlier benchmark files are retained; use the same-run baselines when
evaluating batching because the query count and method order differ.

Measurements describe resident-index throughput on the tested machine. They do
not include cold-cache or dependent-query latency, thread scaling, or sorting
cost for an application that would need to reorder probes. No frequency locking,
CPU pinning, or huge-page setup is applied. ThreadSanitizer remains unavailable
in this environment; leak checking is disabled for ASan runs. Refer to
`docs/batch-results.md` for observed results and limits.
