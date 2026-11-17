#!/usr/bin/env python3
"""Validate the recorded batching benchmark matrix and raw-trial summaries."""

import csv
import math
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[1]
METHODS = {"std": 0, "count": 0, "packed": 0,
           **{f"batch_{size}": size for size in (1, 2, 4, 8, 16, 32, 64, 128)}}


def expected_cases(real):
    if real:
        return {("chr22_16mers", n, p) for n in (1048576, 8388608, 39159057)
                for p in ("mixed", "hot", "sorted")}
    cases = {("uniform", n, p) for n in (1024, 65536, 1048576, 8388731, 33554555)
             for p in ("mixed", "hot", "sorted")}
    cases |= {("clusters", n, p) for n in (1048576, 33554555) for p in ("mixed", "hot")}
    return cases | {("equal", 1048576, "mixed"), ("dense", 1048576, "hits"), ("dense", 1048576, "misses")}


def main():
    compare_prefetch = len(sys.argv) == 2 and sys.argv[1] == "--prefetch"
    if len(sys.argv) > 1 and not compare_prefetch:
        raise RuntimeError("Usage: check_batch_results.py [--prefetch]")
    methods = dict(METHODS)
    if compare_prefetch:
        methods.update({f"prefetch_{size}": size for size in (1, 2, 4, 8, 16, 32, 64, 128)})
    trial_count = 12 if compare_prefetch else 11
    prefix = "batch_prefetch" if compare_prefetch else "batch"
    signatures = {}
    rows_checked = measured = warmups = 0
    profiles = [(config, source) for config in ("avx2", "avx2_repeat", "generic")
                for source in ("synthetic", "real")]
    if compare_prefetch and (ROOT / "benchmarks/batch_prefetch_generic_disabled_real.csv").exists():
        profiles.append(("generic_disabled", "real"))
    for config, source in profiles:
        path = ROOT / "benchmarks" / f"{prefix}_{config}_{source}.csv"
        groups = {}
        with path.open(newline="") as file:
            for row in csv.DictReader(file):
                case = (row["dataset"], int(row["n"]), row["pattern"])
                method = row["method"]
                if method not in methods or int(row["batch_size"]) != methods[method]:
                    raise RuntimeError(f"Invalid method: {path}: {method}")
                if method in groups.setdefault(case, set()):
                    raise RuntimeError(f"Duplicate row: {path}: {case}/{method}")
                groups[case].add(method)
                if int(row["trials"]) != trial_count or int(row["queries"]) != 131075:
                    raise RuntimeError("Incorrect trial/query count")
                samples = [float(row[f"trial_{i}_ns"]) for i in range(trial_count)]
                if not all(math.isfinite(x) and x > 0 for x in samples):
                    raise RuntimeError("Invalid timing")
                for field, want in (("median_ns", statistics.median(samples)),
                                    ("min_ns", min(samples)), ("max_ns", max(samples))):
                    if abs(float(row[field]) - want) > 0.0011:
                        raise RuntimeError(f"Summary mismatch: {path}: {field}")
                signature = tuple(row[k] for k in ("checksum", "queries", "hits"))
                if signatures.setdefault(case, signature) != signature:
                    raise RuntimeError(f"Results differ across methods/builds: {case}")
                hits, queries = int(row["hits"]), int(row["queries"])
                if not 0 <= hits <= queries or (case[2] == "hits" and hits != queries) or (case[2] == "misses" and hits != 0):
                    raise RuntimeError("Hit/miss property violated")
                rows_checked += 1
                measured += queries * trial_count
                warmups += queries
        if set(groups) != expected_cases(source == "real") or any(found != set(methods) for found in groups.values()):
            raise RuntimeError(f"Missing or unexpected cases/methods: {path}")
    print(f"PASS rows={rows_checked} distinct_cases={len(signatures)} "
          f"measured_searches={measured} checked_warmup_searches={warmups}")


if __name__ == "__main__":
    main()
