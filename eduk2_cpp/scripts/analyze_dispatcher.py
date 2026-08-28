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
    c_over_min = float(row["C_over_min_weight"])
    estimated_work = capacity * min(n, c_over_min + 1.0)
    memory_safe = row["tso_us"] != "NA"
    return (memory_safe and n <= 5000 and
            int(row["near_best_efficiency_items"]) == n and
            c_over_min <= 50.0 and estimated_work <= 25_000_000.0)


def geomean(values):
    values = list(values)
    return math.exp(sum(math.log(value) for value in values) / len(values))


def percentile(values, quantile):
    ordered = sorted(values)
    index = math.ceil(quantile * len(ordered)) - 1
    return ordered[max(0, index)]


def summarize(rows):
    applicable = [row for row in rows if row["tso_us"] != "NA"]
    selected = [row for row in rows if selects_tso(row)]
    false_tso = [row for row in selected
                 if float(row["tso_us"]) >= float(row["eduk2_us"])]
    false_eduk2 = [row for row in applicable if not selects_tso(row)
                   and float(row["tso_us"]) < float(row["eduk2_us"])]
    correct = len(applicable) - len(false_tso) - len(false_eduk2)
    dispatch_times = [float(row["tso_us"] if selects_tso(row) else row["eduk2_us"])
                      for row in rows]
    eduk2_times = [float(row["eduk2_us"]) for row in rows]
    regressions = [dispatch / eduk2 for dispatch, eduk2
                   in zip(dispatch_times, eduk2_times)]
    regrets = []
    for row, dispatch in zip(rows, dispatch_times):
        oracle = float(row["eduk2_us"])
        if row["tso_us"] != "NA":
            oracle = min(oracle, float(row["tso_us"]))
        regrets.append(dispatch / oracle)
    tso_speedup = None
    if applicable:
        tso_speedup = geomean(float(row["tso_us"]) / float(row["eduk2_us"])
                              for row in applicable)
    return {
        "instances": len(rows),
        "applicable": len(applicable),
        "selected": len(selected),
        "accuracy": correct / len(applicable) if applicable else 1.0,
        "false_tso": len(false_tso),
        "false_eduk2": len(false_eduk2),
        "speedup_vs_eduk2": geomean(eduk2 / dispatch for eduk2, dispatch
                                     in zip(eduk2_times, dispatch_times)),
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
          "false_eduk2,speedup_vs_eduk2,always_tso_slowdown,regret,"
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
              f"{metric['always_tso_slowdown_vs_eduk2'] or 0:.6f},"
              f"{metric['regret']:.6f},{metric['worst_regret']:.6f},"
              f"{metric['worst_regression']:.6f},"
              f"{metric['p95_regression']:.6f}")
    print(f"tso_selected_percent,{100.0 * total['selected'] / len(rows):.3f}")


if __name__ == "__main__":
    main()
