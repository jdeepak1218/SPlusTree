#!/usr/bin/env python3
"""Check raw benchmark coverage, summaries, and cross-build result agreement."""

import csv
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]
METHODS = {"std", "binary", "eytzinger", "stree_early", "stree_count",
           "stree_mask", "stree_popcount", "stree_packed"}


def expected_cases(real):
    if real:
        return {("chr22_16mers", n, p) for n in (1048576, 8388608, 39159057)
                for p in ("mixed", "hot", "sorted")}
    cases = {("uniform", n, p) for n in (1024, 65536) for p in ("mixed", "hot")}
    cases |= {(d, 1048576, p) for d in ("dense", "clusters", "uniform", "equal")
              for p in ("mixed", "hot", "sorted", "reverse", "boundaries")}
    cases |= {("dense", 1048576, p) for p in ("hits", "misses")}
    cases |= {(d, n, p) for d in ("uniform", "clusters")
              for n in (8388731, 33554555) for p in ("mixed", "hot")}
    return cases


def main():
    agreement = {}
    rows_checked = measured = warmups = 0
    for config in ("avx2", "avx2_repeat", "generic"):
        for source in ("synthetic", "real"):
            path = ROOT / "benchmarks" / f"workloads_{config}_{source}.csv"
            groups = {}
            with path.open(newline="") as file:
                for row in csv.DictReader(file):
                    case = (row["dataset"], int(row["n"]), row["pattern"])
                    method = row["method"]
                    if method in groups.setdefault(case, set()):
                        raise RuntimeError(f"Duplicate row: {path}: {case}/{method}")
                    groups[case].add(method)
                    samples = [float(row[f"trial_{i}_ns"]) for i in range(8)]
                    if int(row["trials"]) != 8 or int(row["queries"]) != 131072:
                        raise RuntimeError("Wrong trial/query count")
                    if not all(math.isfinite(x) and x > 0 for x in samples):
                        raise RuntimeError("Invalid raw timing")
                    for column, value in (("median_ns", statistics.median(samples)),
                                          ("min_ns", min(samples)), ("max_ns", max(samples))):
                        if abs(float(row[column]) - value) > 0.0011:
                            raise RuntimeError(f"Summary mismatch: {path}: {column}")
                    signature = tuple(row[k] for k in ("checksum", "hits", "queries", "unique_keys"))
                    if agreement.setdefault(case, signature) != signature:
                        raise RuntimeError(f"Results differ across methods/builds: {case}")
                    hits, queries = int(row["hits"]), int(row["queries"])
                    if not 0 <= hits <= queries or not 1 <= int(row["unique_keys"]) <= case[1]:
                        raise RuntimeError("Invalid hit or unique-key count")
                    if case[2] == "hits" and hits != queries:
                        raise RuntimeError("Hit-only workload contains misses")
                    if case[2] == "misses" and hits != 0:
                        raise RuntimeError("Miss-only workload contains hits")
                    rows_checked += 1
                    measured += queries * 8
                    warmups += queries
            if set(groups) != expected_cases(source == "real"):
                raise RuntimeError(f"Missing or unexpected cases: {path}")
            if any(methods != METHODS for methods in groups.values()):
                raise RuntimeError(f"Missing or unexpected methods: {path}")
    print(f"PASS rows={rows_checked} distinct_cases={len(agreement)} "
          f"measured_searches={measured} checked_warmup_searches={warmups}")


if __name__ == "__main__":
    main()
