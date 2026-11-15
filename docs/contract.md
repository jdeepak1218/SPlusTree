# Static search tree contract

This document defines the behavior that the reference implementation and every
optimized search implementation must share.

## Input

- Keys and queries use `std::uint32_t`.
- Input keys must be sorted in nondecreasing order.
- Duplicate keys and empty input are allowed.
- The full unsigned 32-bit range is valid, including `0` and `UINT32_MAX`.
- Sorted input is a precondition.

The Eytzinger implementation checks this precondition during construction and
throws `std::invalid_argument` for unsorted input. Its queries do not repeat
the validation. The standalone binary-search baselines require sorted input
and do not validate it.

## Search result

Search performs a lower-bound lookup: it returns the index of the first key
greater than or equal to the query.

- The result type is `std::size_t`.
- If multiple keys match, return the first matching index.
- If no key is greater than or equal to the query, return `size()`.
- For empty input, return `0` for every query.
- An index equal to `size()` is past the end and must not be dereferenced.

More precisely, for input length `n` and result `i`:

- `0 <= i <= n`.
- Every key before `i` is less than the query.
- If `i < n`, the key at `i` is greater than or equal to the query.

Indices refer to positions in the original sorted input, regardless of the
internal tree layout. A result need not be an exact match: when the query is
absent, a larger key may be its lower bound.

`UINT32_MAX` is a valid key, so it is not used as a special "not found" value.

## Examples

For keys `[2, 5, 5, 9, 14]`:

| Query | Result index | Meaning |
| --- | --- | --- |
| `0` | `0` | The first qualifying key is `2`. |
| `5` | `1` | The first occurrence of `5` is selected. |
| `6` | `3` | The first qualifying key is `9`. |
| `14` | `4` | The final key is an exact match. |
| `15` | `5` | No qualifying key exists. |

For keys `[UINT32_MAX]`, querying `UINT32_MAX` returns `0`.
For empty keys `[]`, every query returns `0`, which equals the input size.

## Ownership and immutability

- The eventual tree owns a copy of its input data. The caller may release or
  modify the source after construction completes.
- Searches do not modify the tree.
- Updating keys requires constructing a new tree.

## Batch searches

Each batch result must equal an individual lower-bound lookup for the
corresponding query. Results preserve query order. An empty batch produces no
results.

## Correctness reference

For valid sorted input, results must agree with
`std::lower_bound(values.begin(), values.end(), query) - values.begin()`,
converted to `std::size_t`.

Performance optimizations must preserve this behavior. This document does not
promise a particular speedup or select the final public API signatures.
