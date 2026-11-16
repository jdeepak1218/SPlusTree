# Ordered comparison masks and trailing-zero search

`STree::lower_bound_mask` uses the same packed levels, separators, leaves, and
index-returning contract as the existing methods. Only the node-position
operation changes. Early exit and counting remain available for comparison.

## Algorithm

1. Compare every key with the query using `key >= query`.
2. Put the result for key i in bit i of an unsigned mask.
3. Set an extra marker at the node width.
4. Count trailing zeros to obtain the first qualifying position.

For `[2,5,8,11,14,17,20,23]` queried with 13, positions zero through three fail
and four through seven qualify. Printed in conventional high-bit-first order:

```text
Bit position: 7 6 5 4 3 2 1 0
Mask:         1 1 1 1 0 0 0 0
```

There are four trailing zeros, so the answer is position four, containing 14.
Duplicates work because equality qualifies; the first matching lane is selected.

If no lane qualifies, the real mask is zero. An end marker guarantees that the
answer becomes the node width rather than the integer-mask width:

```cpp
mask |= std::uint32_t{1} << KeysPerNode;
return static_cast<std::size_t>(std::countr_zero(mask));
```

For the default sixteen keys, the marker is bit 16 (`0x10000`). It also ensures
that a zero mask never reaches the final bit scan. `std::countr_zero` itself is
defined for zero, but would then return 32 for this mask type, which is not our
required no-match result.

## C++20 backend

C++20 has no standard portable SIMD-mask API. The implementation provides an
explicit AVX2 backend for sixteen-key nodes when the x86 target enables AVX2.
Other builds and smaller node widths use the ordinary C++ mask loop:

```cpp
for (std::size_t i = 0; i < KeysPerNode; ++i) {
    mask |= static_cast<std::uint32_t>(node.keys[i] >= query) << i;
}
```

The fallback may itself be auto-vectorized by the compiler. It is a portable
expression of the same algorithm, not a guarantee of scalar machine code.

The AVX2 backend:

- Broadcasts the query's unchanged bit pattern to eight 32-bit lanes.
- Loads keys 0–7 and 8–15 from the aligned 64-byte node.
- Uses unsigned `min(key, query) == query` to implement `key >= query` across
  the full unsigned range. No 31-bit restriction or signed reinterpretation
  of the comparison is introduced.
- Extracts one bit per lane using `movemask_ps` on a register cast. This cast
  does not perform floating-point arithmetic or convert integer values.
- Combines `low_mask | (high_mask << 8)`, preserving original key order.
- Adds the end marker and scans for the first set bit.

This expresses the compare/mask/first-set operation directly with two ordered
eight-lane masks. It does not introduce byte/word packing, lane reshuffling,
popcount, prefetching, batching, or a different tree layout.

`SST_DISABLE_MASK_SIMD` disables the explicit backend for testing or integration.
The macro and target flags must be consistent across translation units. It does
not disable compiler auto-vectorization of the fallback or other methods.
AVX2 binaries require a capable CPU and OS; there is no runtime ISA dispatch.
Other compiler/architecture combinations have not been validated.

## Bounds and defined behavior

- Explicit loads are compiled only for sixteen-key nodes. Both 32-byte loads
  fit completely inside the sixteen-key array.
- Node alignment is 64 bytes; offsets zero and 32 satisfy the load alignment.
- Smaller nodes read exactly their declared key slots through the fallback.
- All key slots, including padding, are initialized during construction.
- Query broadcast uses `std::bit_cast<int32_t>` to preserve bits; unsigned
  comparison happens through the unsigned minimum intrinsic.
- Bit shifts are unsigned, with real lane indices at most 15 and a marker at
  most 16. No shift reaches the 32-bit word width.
- The first-set position is always in `[0, KeysPerNode]`; existing child-index
  bounds, duplicate routing, leaf spill, and final logical-size clamp still apply.

## Inspect code generation

```bash
g++ -std=c++20 -O3 -mavx2 -Iinclude -S benchmarks/count_codegen.cpp -o build/mask_codegen_avx2.s
```

Inspect `stree_mask_lookup`, including internal levels and the final leaf scan.
On GCC 13.3 the sequence includes `vpminud`, `vpcmpeqd`, `vmovmskps`, an end-marker
OR, and `rep bsfl`. The latter encoding can execute as TZCNT on capable CPUs;
for nonzero inputs the first-set answer agrees with BSF. No additional BMI target
flag is required by this build. Actual benchmark disassembly also contains the
ordered mask extractions. Results are recorded in `docs/mask-results.md`.
