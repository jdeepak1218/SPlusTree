#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/validation
cxx="${CXX:-g++}"
common=(-std=c++20 -Wall -Wextra -Wpedantic -Wconversion -pthread -Iinclude)
"$cxx" --version
python3 tools/prepare_genome.py
for name in reference eytzinger; do
  "$cxx" "${common[@]}" -O3 -DNDEBUG "tests/${name}_test.cpp" -o "build/validation/$name"
  "build/validation/$name"
done
"$cxx" "${common[@]}" -O3 -DNDEBUG tests/workload_data_test.cpp -o build/validation/data
build/validation/data
for config in generic avx2 fallback software_popcount no_prefetch; do
  flags=()
  case "$config" in
    avx2) flags=(-mavx2 -mpopcnt) ;;
    fallback) flags=(-mavx2 -mpopcnt -DSST_DISABLE_MASK_SIMD) ;;
    no_prefetch) flags=(-mavx2 -mpopcnt -DSST_DISABLE_PREFETCH) ;;
    software_popcount) flags=(-mavx2 -mno-popcnt) ;;
  esac
  for name in stree robustness batch; do
    echo "Checking $config / $name"
    "$cxx" "${common[@]}" -O3 -DNDEBUG "${flags[@]}" "tests/${name}_test.cpp" -o "build/validation/${name}_$config"
    "build/validation/${name}_$config"
  done
done
for name in reference eytzinger stree robustness workload_data batch; do
  echo "Checking ASan + UBSan / $name"
  "$cxx" "${common[@]}" -O1 -g -mavx2 -mpopcnt -D_GLIBCXX_ASSERTIONS \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    "tests/${name}_test.cpp" -o "build/validation/${name}_sanitized"
  ASAN_OPTIONS=detect_leaks=0 "build/validation/${name}_sanitized"
done
