#!/usr/bin/env python3
"""Evaluate dispatcher v3 from ukp_kernel_bench CSV output."""

import argparse
import csv
import math
from collections import defaultdict

MAX_ITEMS = 10_000
SPAN_NUM = 71
SPAN_DEN = 50
ENDPOINT_DEN = 20
PRESSURE_NUM = 7
PRESSURE_DEN = 10
MIN_FINITE_BUDGET = 650_000_000


def family(name):
    for prefix, label in (("exnsd", "EXNSD"), ("hi_", "HI"),
                          ("nsds2_", "NSDS2"), ("saw_", "SAW"),
                          ("sc_", "SC"), ("ss_", "SS"),
                          ("ss2_", "SS2")):
        if name.startswith(prefix):
            return label
    return "AUX"


def model_decision(row):
    n = int(row["n_after_common"])
    capacity = int(row["C"])
    min_weight = int(row["min_weight"])
    max_weight = int(row["max_weight"])
    best_weight = int(row["w_best"])
    if n <= 0 or n > MAX_ITEMS or min_weight <= 0 or best_weight <= 0:
        return False
    if int(row["near_best_efficiency_items"]) != n:
        return False
    if max_weight * SPAN_DEN > min_weight * SPAN_NUM:
        return False

    light_ok = best_weight <= min_weight + min_weight // ENDPOINT_DEN
    heavy_ok = best_weight >= max_weight - max_weight // ENDPOINT_DEN
    if not light_ok and not heavy_ok:
        return False
    if light_ok and heavy_ok:
        light = best_weight - min_weight <= max_weight - best_weight
    else:
        light = light_ok

    remainder = capacity % best_weight
    residual_numerator = remainder if light else best_weight - remainder
    if residual_numerator * PRESSURE_DEN < best_weight * PRESSURE_NUM:
        return False

    max_transitions = int(row.get("tso_max_transitions", "0") or 0)
    if max_transitions:
        if max_transitions < MIN_FINITE_BUDGET:
            return False
        reachable = min(n, capacity // min_weight + 1)
        if capacity * reachable > max_transitions:
            return False
    return True


def selected_tso(row):
    reported = row.get("dispatch_kernel", "").strip().upper()
    if reported in {"TSO", "EDUK2"}:
        return reported == "TSO"
    return model_decision(row)


def tso_applicable(row):
    status = row.get("tso_status", "")
    if status:
        return status == "proved_optimal"
    return row.get("median_tso_ns", "NA") != "NA"


def geomean(values):
    values = list(values)
    return math.exp(sum(math.log(value) for value in values) / len(values))


def percentile(values, quantile):
    ordered = sorted(values)
    index = math.ceil(quantile * len(ordered)) - 1
    return ordered[max(0, index)]


def summarize(rows):
    applicable = [row for row in rows if tso_applicable(row)]
    selected = [row for row in rows if selected_tso(row)]
    selected_applicable = [row for row in selected if tso_applicable(row)]

    false_tso = [row for row in selected_applicable
                 if float(row["median_tso_ns"]) >= float(row["median_eduk2_ns"])]
    false_tso_10 = [row for row in selected_applicable
                    if float(row["median_tso_ns"]) >
                    1.1 * float(row["median_eduk2_ns"])]
    false_eduk2 = [row for row in applicable if not selected_tso(row)
                   and float(row["median_tso_ns"]) < float(row["median_eduk2_ns"])]

    synthetic_times = []
    for row in rows:
        if selected_tso(row) and tso_applicable(row):
            synthetic_times.append(float(row["median_tso_ns"]))
        else:
            synthetic_times.append(float(row["median_eduk2_ns"]))

    auto_times = [float(row["median_auto_ns"]) for row in rows]
    eduk2_times = [float(row["median_eduk2_ns"]) for row in rows]
    regressions = [automatic / eduk2 for automatic, eduk2
                   in zip(auto_times, eduk2_times)]
    overheads = [automatic / synthetic for automatic, synthetic
                 in zip(auto_times, synthetic_times)]
    regrets = []
    for row, automatic in zip(rows, auto_times):
        oracle = float(row["median_eduk2_ns"])
        if tso_applicable(row):
            oracle = min(oracle, float(row["median_tso_ns"]))
        regrets.append(automatic / oracle)

    selected_speedups = [float(row["median_eduk2_ns"]) /
                         float(row["median_tso_ns"])
                         for row in selected_applicable]
    mismatches = sum(
        1 for row in rows
        if row.get("dispatch_kernel", "").strip().upper() in {"TSO", "EDUK2"}
        and (row["dispatch_kernel"].strip().upper() == "TSO") != model_decision(row)
    )

    return {
        "instances": len(rows),
        "applicable": len(applicable),
        "selected": len(selected),
        "false_tso": len(false_tso),
        "false_tso_10": len(false_tso_10),
        "false_eduk2": len(false_eduk2),
        "selected_tso_speedup": geomean(selected_speedups) if selected_speedups else 1.0,
        "speedup_vs_eduk2": geomean(eduk2 / automatic for eduk2, automatic
                                     in zip(eduk2_times, auto_times)),
        "synthetic_speedup_vs_eduk2": geomean(
            eduk2 / synthetic for eduk2, synthetic
            in zip(eduk2_times, synthetic_times)),
        "dispatcher_overhead": geomean(overheads),
        "regret": geomean(regrets),
        "worst_regret": max(regrets),
        "worst_regression": max(regressions),
        "p95_regression": percentile(regressions, 0.95),
        "model_mismatches": mismatches,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_file")
    args = parser.parse_args()
    with open(args.csv_file, newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise SystemExit("empty CSV")

    print("scope,instances,tso_applicable,tso_selected,false_tso,false_tso_gt10pct,"
          "false_eduk2,selected_tso_geomean_speedup,real_auto_speedup_vs_eduk2,"
          "synthetic_speedup_vs_eduk2,real_dispatcher_overhead,regret,"
          "worst_regret,worst_regression,p95_regression,model_mismatches")
    grouped = defaultdict(list)
    for row in rows:
        grouped[family(row["instance"])].append(row)
    for label, group in [("ALL", rows), *sorted(grouped.items())]:
        metric = summarize(group)
        print(f"{label},{metric['instances']},{metric['applicable']},"
              f"{metric['selected']},{metric['false_tso']},"
              f"{metric['false_tso_10']},{metric['false_eduk2']},"
              f"{metric['selected_tso_speedup']:.6f},"
              f"{metric['speedup_vs_eduk2']:.6f},"
              f"{metric['synthetic_speedup_vs_eduk2']:.6f},"
              f"{metric['dispatcher_overhead']:.6f},"
              f"{metric['regret']:.6f},{metric['worst_regret']:.6f},"
              f"{metric['worst_regression']:.6f},"
              f"{metric['p95_regression']:.6f},"
              f"{metric['model_mismatches']}")


if __name__ == "__main__":
    main()