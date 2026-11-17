#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
dataset="${1:-data/chr22.u32}"
test -r "$dataset"
mkdir -p build results
cxx="${CXX:-g++}"
common=(-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude)
"$cxx" "${common[@]}" benchmarks/workload_benchmark.cpp -o build/workload_generic
"$cxx" "${common[@]}" -mavx2 -mpopcnt benchmarks/workload_benchmark.cpp -o build/workload_avx2
# Run serially: concurrent processes would contaminate the timing comparison.
for config in avx2 avx2_repeat generic; do
  binary=build/workload_avx2
  if [[ "$config" == generic ]]; then binary=build/workload_generic; fi
  "$binary" > "results/workloads_${config}_synthetic.csv"
  "$binary" "$dataset" > "results/workloads_${config}_real.csv"
done
