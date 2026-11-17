# Upstream notices

The search layouts and optimization progression are informed by Ragnar Groot
Koerkamp's [Static search trees](https://curiouscoding.nl/posts/static-search-tree/)
and Algorithmica's [static B-trees](https://en.algorithmica.org/hpc/data-structures/s-tree/).

The optimized Eytzinger traversal is adapted from the MIT-licensed
[upstream project](https://github.com/RagnarGrootKoerkamp/static-search-tree),
specifically `src/eytzinger.rs` and `src/util.rs`. The S+ node-search and batching
techniques are translated into C++ with full unsigned comparisons and sorted-index
results. Upstream copyright and permission notices are retained in the adapted
headers and [static-search-tree.LICENSE](static-search-tree.LICENSE).
