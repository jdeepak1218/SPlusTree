# Population-count node search

`STree::lower_bound_popcount` finds a position by counting set bits rather than
finding the first set bit. It uses the existing packed levels, separators,
aligned nodes, leaves, ownership, and sorted-index result contract.

## Algorithm

Set one bit for every key strictly below the query, then call `std::popcount`:

```text
Keys:        [2, 5, 8, 11]
Query:       7
Key < query: [1, 1, 0,  0]
Mask:        0011
Set bits:    2 -> first qualifying position is 2
```

For sorted keys, the number of smaller keys equals the first qualifying index.
Equal keys are excluded, preserving the first duplicate. No set bits returns
zero; all sixteen real bits set returns sixteen, selecting the last child or
spilling to the next leaf. There is no end marker: adding one would corrupt
the population count.

Bit order does not matter to the count, but every key still needs a unique bit.
This version keeps the existing two-half extraction layout. It does not yet
introduce compressed packing or the two-bits-per-key optimization.

## Full unsigned comparison

AVX2 signed greater-than can express `query > key` after flipping the sign bit
of both operands. Interpreting the transformed bits as signed maps the entire
unsigned range monotonically:

| Unsigned value | After sign-bit flip, interpreted as signed |
| --- | ---: |
| `0` | -2147483648 |
| `0x7fffffff` | -1 |
| `0x80000000` | 0 |
| `0xffffffff` | 2147483647 |

The implementation broadcasts `query ^ 0x80000000U`, flips each key vector's sign
bit, and compares the transformed query against the transformed keys. This is
an order-preserving transformation, not a restriction to 31-bit values. Query
bits are transferred with `std::bit_cast<int32_t>`.

Two eight-lane comparison vectors become two eight-bit masks through register
casts and `movemask`. The upper mask is shifted by eight before combining, so
bits do not overlap. No floating-point arithmetic occurs. Only the low sixteen
bits can be set; `std::popcount(mask)` therefore returns zero through sixteen.

The AVX2 backend applies only to sixteen-key nodes. Generic builds, other node
widths, and `SST_DISABLE_MASK_SIMD` use the ordinary C++ bit loop. The macro now
governs both explicit comparison-mask backends; it does not disable compiler
auto-vectorization. Target flags and macros must be consistent across translation
units. No runtime ISA dispatch is added.

## Hardware population count

The hardware instruction and vector comparisons are separate CPU capabilities.
The primary test build makes requirements explicit with `-mavx2 -mpopcnt`.
On GCC 13.3, `-mavx2` also enables POPCNT in the target-option output. The control
build therefore uses `-mavx2 -mno-popcnt` to request software population counting
while retaining the vector comparison backend. Both capabilities were checked
on the execution machine before timing.

`std::popcount` remains correct without a hardware instruction. Inspection shows
`popcntl` in the primary build and calls to `__popcountdi2` in the software control.
The intrinsic comparison backend still requires AVX2; the generic build remains
the portable C++ alternative.

## Inspect the code

```bash
g++ -std=c++20 -O3 -mavx2 -mpopcnt -Iinclude -S benchmarks/count_codegen.cpp -o build/popcount_codegen.s
g++ -std=c++20 -O3 -mavx2 -mno-popcnt -Iinclude -S benchmarks/count_codegen.cpp -o build/popcount_codegen_software.s
```

Inspect `stree_popcount_lookup` in both files, including internal levels and the
leaf. The hardware path contains sign-bit XOR, signed comparison, mask extraction,
combination, and POPCNT. No trailing-zero scan or marker is needed. The actual
benchmark binary was also checked for hardware POPCNT.

## Safety and storage

The full-node backend reads two aligned 32-byte halves of an initialized
64-byte node. Smaller arrays do not enter it. Generic shifts are unsigned and
at most fifteen bits; combining the upper half shifts only by eight. Padding
`UINT32_MAX` never compares below any valid query, so it contributes zero bits.
The original traversal bounds and final logical-size clamp remain unchanged.

The method allocates no query storage and adds no persistent buffers. Existing
methods remain available. Correctness coverage, build commands, raw timings,
and limitations are in `tests/README.md`, `benchmarks/README.md`, and
`docs/popcount-results.md`.
