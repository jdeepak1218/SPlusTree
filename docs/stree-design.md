# Scalar S+ tree

`include/stree.hpp` is self-contained. Copy that header and use
`sst::STree<> tree(sorted_values)` followed by `tree.lower_bound(query)`.
It returns the original sorted index or `tree.size()` when no key qualifies.

## Shape and separators

The default node stores 16 unsigned 32-bit keys in a 64-byte-aligned block.
An internal node has up to 17 children. All original data lives in the leaves;
internal separator keys are copies used for navigation. This duplication is
what makes the structure an S+ tree.

The teaching specialization `STree<2>` stores two keys per node and has up to
three children. For sorted input `[2, 5, 8, 11, 14, 17]`:

```text
                   [8, 14]
                  /   |    \
              [2,5] [8,11] [14,17]
```

Each separator is the minimum key in the child subtree immediately to its right.
The root contains 8 (minimum of child 1) and 14 (minimum of child 2).

`find_linear` returns the first slot whose key is at least the query, or the
number of keys per node if every key is smaller. In internal nodes this slot
selects a child. In leaves it selects a position in the sorted data.

For query 9, the root returns slot 1, selecting `[8,11]`. The leaf returns slot 1,
so the sorted result index is `1 * 2 + 1 = 3`, containing 11.

For query 8, equality selects child 0, `[2,5]`. The leaf returns slot 2, which
intentionally advances to the next leaf: sorted index `0 * 2 + 2 = 2`, containing
8. This conservative equality routing preserves first-duplicate semantics when
equal keys occur in more than one child. A spill can also cross a parent boundary.

For query 18, child 2 returns leaf slot 2. The result is `2 * 2 + 2 = 6`, equal
to the input size: no qualifying key exists.

## Packed levels

The example stores four nodes in one vector:

```text
Absolute node 0: root [8,14]
Absolute node 1: leaf [2,5]
Absolute node 2: leaf [8,11]
Absolute node 3: leaf [14,17]

Level offsets: [0,1]
```

During traversal, `node` is an index relative to its level. The selected child
at the next level is:

```cpp
node = node * (KeysPerNode + 1) + child;
```

The absolute vector position is `offsets_[level] + node`. No per-child pointers
are stored. Once in a leaf, its sorted position is:

```cpp
node * KeysPerNode + slot
```

## Construction

1. Validate nondecreasing input; reject unsorted keys with `std::invalid_argument`.
2. Count leaves using ceiling division by the node width.
3. Repeatedly ceiling-divide by the fanout until there is one root.
4. Reverse the counts and calculate packed level offsets, checking total capacity.
5. Allocate aligned nodes and initialize every key slot to `UINT32_MAX`.
6. Copy the original keys into the leaves in sorted order.
7. Work upward, setting each parent's separators to the minima of its right
   child subtrees. Carry the first child's minimum upward for the next level.

Missing separator slots use `UINT32_MAX`. This is also valid user data. A query
equal to it routes conservatively left; the logical input size distinguishes
actual maximum keys from unused leaf slots. Final positions are capped at that
size, so padding is not reported as a real result.

Empty input allocates no nodes. The two-key specialization is for explanation
and tests; it still uses 64-byte node blocks, so it is not space-efficient.

## Safety, ownership, and costs

- Construction owns its data and searches are read-only with no allocation.
- Ceiling division avoids `n + divisor - 1`, which could overflow.
- Total node counts are checked against vector capacity limits before allocation.
- Internal padding ensures a full-range query selects an existing child, even
  in an incomplete final group. This invariant is exercised by differential tests.
- Leaf spill uses a sorted index, not pointer arithmetic across separate objects.
- `alignas(64)` on the node type makes `std::vector<Node>` use suitable alignment
  under C++20. Each default node is exactly 64 bytes.
- Construction is O(n); search visits O(log_17(n)) levels for the default width,
  with up to 16 scalar comparisons per node. Recursive stacks are not required.
- Large default trees approach about 6.25% node-storage overhead over their key
  array, plus final-node padding and level offsets. Small inputs have more overhead.

This is the initial early-exit scalar node scan. It contains no explicit SIMD,
tree prefetching, batching, or partitioning. Later changes can be measured against
this version one at a time.

The additional `lower_bound_count` method uses the same layout and counts keys
below the query instead of stopping early. It permits compiler auto-vectorization;
see `docs/stree-counting.md` for code and `docs/count-results.md` for measurements.

`lower_bound_mask` instead forms ordered comparison bits and locates the first
set bit with a trailing-zero count. It has an AVX2 backend for sixteen-key nodes
and a portable fallback. See `docs/stree-mask.md` and `docs/mask-results.md`.
