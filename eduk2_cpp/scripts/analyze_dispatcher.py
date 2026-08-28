#!/usr/bin/env python3
"""Evaluate the fixed, production dispatcher from ukp_kernel_bench CSV output."""

import argparse
import csv
import math
from collections import defaultdict


def family(name):
    for prefix, label in (("exnsd", "EXNSD"), ("hi_", "HI"),
                          ("nsds2_", "NSDS2"), ("saw_", "SAW"),
                          ("sc_", "SC")):
        if name.startswith(prefix):
            return label
    return "AUX"


def selects_tso(row):
    n = int(row["n_after_common"])
    capacity = int(row["C"])
    min_weight = int(row["min_weight"])
    if n <= 0 or min_weight <= 0:
        return False
    if n * min_weight <= capacity + min_weight:
        work_safe = capacity * n <= 25_000_000
    else:
        work_safe = capacity * (capacity + min_weight) <= 25_000_000 * min_weight
    memory_safe = row["median_tso_ns"] != "NA"
    return (memory_safe and n <= 5000 and
            int(row["near_best_efficiency_items"]) == n and
            capacity <= 50 * min_weight and work_safe)


def geomean(values):
    values = list(values)
    return math.exp(sum(math.log(value) for value in values) / len(values))


def percentile(values, quantile):
    ordered = sorted(values)
    index = math.ceil(quantile * len(ordered)) - 1
    return ordered[max(0, index)]


def summarize(rows):
    applicable = [row for row in rows if row["median_tso_ns"] != "NA"]
    selected = [row for row in rows if selects_tso(row)]
    false_tso = [row for row in selected
                 if float(row["median_tso_ns"]) >= float(row["median_eduk2_ns"])]
    false_eduk2 = [row for row in applicable if not selects_tso(row)
                   and float(row["median_tso_ns"]) < float(row["median_eduk2_ns"])]
    correct = len(applicable) - len(false_tso) - len(false_eduk2)
    synthetic_times = [float(row["median_tso_ns"] if selects_tso(row)
                             else row["median_eduk2_ns"]) for row in rows]
    auto_times = [float(row["median_auto_ns"]) for row in rows]
    eduk2_times = [float(row["median_eduk2_ns"]) for row in rows]
    regressions = [automatic / eduk2 for automatic, eduk2
                   in zip(auto_times, eduk2_times)]
    overheads = [automatic / synthetic for automatic, synthetic
                 in zip(auto_times, synthetic_times)]
    regrets = []
    for row, automatic in zip(rows, auto_times):
        oracle = float(row["median_eduk2_ns"])
        if row["median_tso_ns"] != "NA":
            oracle = min(oracle, float(row["median_tso_ns"]))
        regrets.append(automatic / oracle)
    tso_speedup = None
    if applicable:
        tso_speedup = geomean(float(row["median_tso_ns"]) /
                              float(row["median_eduk2_ns"])
                              for row in applicable)
    return {
        "instances": len(rows),
        "applicable": len(applicable),
        "selected": len(selected),
        "accuracy": correct / len(applicable) if applicable else 1.0,
        "false_tso": len(false_tso),
        "false_eduk2": len(false_eduk2),
        "speedup_vs_eduk2": geomean(eduk2 / automatic for eduk2, automatic
                                     in zip(eduk2_times, auto_times)),
        "synthetic_speedup_vs_eduk2": geomean(
            eduk2 / synthetic for eduk2, synthetic
            in zip(eduk2_times, synthetic_times)),
        "dispatcher_overhead": geomean(overheads),
        "always_tso_slowdown_vs_eduk2": tso_speedup,
        "regret": geomean(regrets),
        "worst_regret": max(regrets),
        "worst_regression": max(regressions),
        "p95_regression": percentile(regressions, 0.95),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_file")
    args = parser.parse_args()
    with open(args.csv_file, newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    total = summarize(rows)
    print("scope,instances,tso_applicable,tso_selected,accuracy,false_tso,"
          "false_eduk2,real_auto_speedup_vs_eduk2,synthetic_speedup_vs_eduk2,"
          "real_dispatcher_overhead,always_tso_slowdown,regret,"
          "worst_regret,worst_regression,p95_regression")
    grouped = defaultdict(list)
    for row in rows:
        grouped[family(row["instance"])].append(row)
    for label, group in [("ALL", rows), *sorted(grouped.items())]:
        metric = summarize(group)
        print(f"{label},{metric['instances']},{metric['applicable']},"
              f"{metric['selected']},{metric['accuracy']:.6f},"
              f"{metric['false_tso']},{metric['false_eduk2']},"
              f"{metric['speedup_vs_eduk2']:.6f},"
              f"{metric['synthetic_speedup_vs_eduk2']:.6f},"
              f"{metric['dispatcher_overhead']:.6f},"
              f"{metric['always_tso_slowdown_vs_eduk2'] or 0:.6f},"
              f"{metric['regret']:.6f},{metric['worst_regret']:.6f},"
              f"{metric['worst_regression']:.6f},"
              f"{metric['p95_regression']:.6f}")
    print(f"tso_selected_percent,{100.0 * total['selected'] / len(rows):.3f}")


if __name__ == "__main__":
    main()
