#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
dataset="${1:-data/chr22.u32}"
test -r "$dataset"
mkdir -p build results
cxx="${CXX:-g++}"
common=(-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Iinclude)
"$cxx" "${common[@]}" benchmarks/batch_benchmark.cpp -o build/batch_benchmark_generic
"$cxx" "${common[@]}" -mavx2 -mpopcnt benchmarks/batch_benchmark.cpp -o build/batch_benchmark_avx2
for config in avx2 avx2_repeat generic; do
  binary=build/batch_benchmark_avx2
  if [[ "$config" == generic ]]; then binary=build/batch_benchmark_generic; fi
  "$binary" > "results/batch_${config}_synthetic.csv"
  "$binary" "$dataset" > "results/batch_${config}_real.csv"
done
