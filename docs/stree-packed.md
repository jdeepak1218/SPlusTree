# Packed comparison-mask search

`STree::lower_bound_packed` uses the same packed tree levels, separators, leaf
order, ownership, and original-index contract as the existing query methods.
Only the reduction of a node's comparison results changes.

## What is packed

The keys stay unsigned 32-bit integers. The temporary comparison results are
packed in registers; stored keys are not compressed or changed.

The existing full-range sign-bit transformation produces two eight-lane vectors
whose lanes are zero for false and all ones (`-1` as signed 32-bit) for true.
Both zero and minus one fit exactly in a signed 16-bit lane:

```text
32-bit false: 0x00000000 -> packed false: 0x0000
32-bit true:  0xffffffff -> packed true:  0xffff
```

`_mm256_packs_epi32` performs signed saturating packing. Since the inputs are
only zero or minus one, saturation loses no comparison information. Unsigned
saturating packing would turn minus one into zero and is incorrect here.

## One mask, two bits per key

The packed vector contains sixteen 16-bit lanes, occupying 32 bytes.
`_mm256_movemask_epi8` extracts each byte's sign bit. Each true word contains two
`0xff` bytes and contributes two set bits; false contributes zero.

```cpp
const auto packed = _mm256_packs_epi32(low_below, high_below);
const auto mask = static_cast<std::uint32_t>(_mm256_movemask_epi8(packed));
return static_cast<std::size_t>(std::popcount(mask)) / 2;
```

Three smaller keys produce six set bits; `6 / 2` returns position three.
No smaller keys produce zero, which is a valid population-count input. Sixteen
smaller keys produce all 32 bits set, giving `32 / 2 = 16`. There is no end marker.

The mask must retain all 32 bits, including the highest bit. Converting the
intrinsic's signed integer return to `uint32_t` preserves its bit pattern for
population counting. Truncation would lose a contribution and can corrupt leaf
spill or child selection.

## Lane order

AVX2 packing operates within 128-bit halves. If inputs are `l0..l7` and `h0..h7`,
the sixteen packed word lanes are:

```text
l0 l1 l2 l3 h0 h1 h2 h3 l4 l5 l6 l7 h4 h5 h6 h7
```

The total count is unchanged by this permutation, so no order-restoring shuffle
is needed. This would not be valid for trailing-zero position extraction, where
bit positions must correspond to sorted key positions.

## Backends and bounds

The packed backend is compiled only with AVX2 on x86 and only for sixteen-key
nodes. `SST_DISABLE_MASK_SIMD`, generic targets, and other node widths reuse the
existing population-count helper. The fallback therefore has identical semantics
to unpacked population search, rather than pretending to pack arbitrary widths.
Hardware population count is selected by the compiler target; software counting
remains correct. Target flags and macros must be consistent across translation
units. No runtime ISA dispatch is provided.

Both 32-byte loads stay inside an initialized, 64-byte-aligned node. Full unsigned
ordering is preserved by the sign-bit transformation before signed comparison.
Padding `UINT32_MAX` never counts below a valid query. Packed masks have exactly
two bits per qualifying lane, so the population count is even and division gives
a result in zero through sixteen. The existing traversal and logical-size clamp
remain unchanged. The new method allocates no persistent or per-query buffers.

## Inspect generated code

```bash
g++ -std=c++20 -O3 -mavx2 -mpopcnt -Iinclude -S benchmarks/count_codegen.cpp -o build/packed_codegen.s
```

Inspect `stree_packed_lookup` in the internal-node loop and final leaf. On the
tested GCC 13.3 build, the reduction uses `vpackssdw`, one `vpmovmskb`, `popcntl`,
and a right shift for division. Two separate mask extractions and their shift/OR
combination are gone. The divide-by-two operation remains as a shift; it is not
claimed to disappear. The benchmark binary was also checked for these operations.

Fewer source operations or instructions do not guarantee a timing improvement.
The complete measurements and tradeoffs are in `docs/packed-results.md`.
