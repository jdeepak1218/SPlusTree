# Library comparison, 2026-11-17

Source revision: `5da8d488bf2b264dbf8c6c5576d43575647425ff` on main.

GCC 13.3.0, C++20, `-O3 -DNDEBUG -mavx2 -mpopcnt`, Intel i7-12700H,
WSL2 with two visible logical CPUs. No CPU pinning or frequency lock;
guest CPU affinity does not identify a physical P-core.

Two serial invocations of `batch_benchmark --prefetch data/chr22.u32`.
See the source revision's `benchmarks/README.md` and `real_dataset.json`
for build commands, data provenance, and timing scope.

Each CSV has 180 rows: 20 methods, three real-data input sizes, three query
patterns. Each method has one warm-up and twelve measured trials, with
131,075 queries per pass and shuffled method order. All 613,431,000 outputs
were checked across the two runs. CSV coverage, min/max/medians, and result
checksums were independently checked across methods and repeats.

Local verification for the source revision passed all 22 release CTest cases,
all 22 ASan/UBSan CTest cases (leak detection disabled), the external CMake
consumer, the README example, and ordinary/prefetch benchmark smoke cases.
Hosted CI and MSVC for this revision require pushing main.
