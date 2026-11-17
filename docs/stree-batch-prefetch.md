# Batched prefetch lookup

`STree::lower_bound_batch_prefetch` advances independent queries level by level,
using the same packed comparison and original sorted-index result as
`lower_bound_batch`. After choosing each query's child, it requests the next
node while the remaining queries at the current level are processed.

```cpp
sst::STree<> tree(std::vector<std::uint32_t>{2, 5, 5, 9, 14});
const std::array<std::uint32_t, 4> queries{6, 5, 20, 0};
const auto positions = tree.lower_bound_batch_prefetch(queries);
// positions: {3, 1, 5, 0}

std::vector<std::uint32_t> probes{6, 5, 20, 0, 14};
std::vector<std::size_t> output(probes.size());
tree.lower_bound_batch_prefetch<4>(probes, output);
```

Every query keeps its own position. The child update is followed by:

```cpp
const auto next_offset = offsets_[level + 1];
if (positions[i] < nodes_.size() - next_offset) {
    __builtin_prefetch(nodes_.data() + next_offset + positions[i], 0, 3);
}
```

The subtraction-based check happens before forming a pointer and keeps the hint
inside the initialized node vector. The traversal determines a valid child;
the hint itself does not participate in answer selection. Argument zero requests
read access and locality three requests high temporal locality. The tested x86
compiler emits `prefetcht0`. This is a hint, not a guarantee of cache residency
or a synchronization primitive.

There is one hint after each internal-level child selection, including the
transition to a leaf. There are no hints beyond the leaf, for empty trees, or
when the tree has just a leaf level. The final incomplete span batch uses the
same individual packed lookup as before and receives no extra hints.

The original and prefetched APIs share a private implementation selected with
`if constexpr`; the original path has no runtime prefetch flag or hint branch.
Fixed arrays return fixed arrays and are `noexcept`. Span input/output lengths
must match and storage must not overlap. A length mismatch throws before output
writes. Both variants preserve first-duplicate results, support every uint32
key including the maximum, return `size()` past the end, and write zeros for
empty trees. No persistent or per-call heap storage is added. Concurrent calls
need separate output buffers.

Hints use GCC/Clang's builtin. Other compilers, or `SST_DISABLE_PREFETCH`, compile
the prefetched API to the ordinary traversal. No AVX2 requirement is introduced
by prefetching itself; node comparisons retain their existing backend selection.
All translation units must use consistent target flags and selection macros.
There is no new automatic choice of method or batch size.

## Validation and reproduction

```bash
mkdir -p build
bash tools/run_validation.sh
build/validation/batch_avx2 data/chr22.u32
ASAN_OPTIONS=detect_leaks=0 build/validation/batch_sanitized data/chr22.u32
bash tools/run_batch_prefetch_benchmarks.sh
```

The full batch matrix exercises both methods: all node widths, batch sizes
1/2/3/4/8/16/32/64/128, structural transitions, unsigned boundaries, duplicates,
partial batches, empty/moved-from trees, copies, query subspans, output canaries,
invalid lengths, and concurrent reads. Release configurations include generic,
AVX2/POPCNT, mask backend disabled, software population count, and hints disabled.
ASan/UBSan is run separately, including the full real-key dataset. Leak checking
is disabled; ThreadSanitizer remains unavailable in this environment.

The code-generation probe in `benchmarks/count_codegen.cpp` permits inspecting
the new method with and without `-DSST_DISABLE_PREFETCH`. Removing a hint should
not change search answers, so instruction inspection complements correctness
testing rather than expecting result comparisons to detect missing hints.

The benchmark's `--prefetch` mode compares standard search, counting, individual
packed lookup, eight normal batch sizes and eight matching prefetched sizes.
It keeps the existing 31-case key/query matrix and 131,075 probes per case.
Every method has a warm-up plus twelve measured trials. Method order is shuffled
deterministically each trial. Copies, tail handling, bounds checks, and output
stores are timed; generation, construction, sorting, and validation are not.
Every output buffer is poisoned outside timing before each pass and every answer
is checked afterward. CSV files retain all trials, medians, minima/maxima, hit
counts, checksums, construction observations, and index allocation bytes.

The runner executes AVX2/POPCNT, an independent repeat, and generic builds
serially. It writes six `batch_prefetch_*` CSVs and preserves historical results.
Real keys are the chromosome-derived 16-mers described in `real_dataset.json`;
their query probes are generated rather than captured production traffic.

These are resident-index throughput measurements under WSL, with no frequency
lock, affinity, huge-page configuration, or cold-cache control. Small differences
and overlapping ranges do not establish statistical significance. The experiment
does not measure dependent-query latency or thread scaling. The measured report
is in `docs/batch-prefetch-results.md`.
