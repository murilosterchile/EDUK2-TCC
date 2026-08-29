#!/usr/bin/env python3
"""Compare benchmark builds of optimized and faithful on PYAsUKP families."""

from __future__ import annotations

import argparse
import csv
import statistics
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INSTANCES = REPO_ROOT / "benchmarks" / "pyasukp_paper" / "instances"
DEFAULT_SOLVER = REPO_ROOT / "eduk2_cpp" / "build-benchmark" / "ukp_solve_benchmark"


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare optimized and faithful using only the algorithm-internal "
            "time_us reported by ukp_solve_benchmark."
        )
    )
    parser.add_argument("--instances", type=Path, default=DEFAULT_INSTANCES)
    parser.add_argument("--solver", type=Path, default=DEFAULT_SOLVER)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument(
        "--family",
        action="append",
        help="Immediate family directory to run; repeat this option as needed.",
    )
    parser.add_argument(
        "--limit-per-family",
        type=int,
        default=0,
        help="Limit instances per family; zero runs every instance.",
    )
    parser.add_argument(
        "--tso-max-transitions",
        type=int,
        default=0,
        help="Optimized AUTO transition budget; zero means unlimited.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("optimized_vs_faithful.csv"),
    )
    args = parser.parse_args()
    if args.repetitions <= 0:
        parser.error("--repetitions must be positive")
    if args.limit_per_family < 0:
        parser.error("--limit-per-family must be nonnegative")
    if args.tso_max_transitions < 0:
        parser.error("--tso-max-transitions must be nonnegative")
    return args


def parse_solver_output(output: str) -> tuple[int, int]:
    fields: dict[str, str] = {}
    for line in output.splitlines():
        key, separator, value = line.partition(" ")
        if separator:
            fields[key] = value.strip()
    required = ("profit", "verified", "time_us", "stats_mode")
    missing = [key for key in required if key not in fields]
    if missing:
        raise RuntimeError(f"solver output is missing: {', '.join(missing)}")
    if fields["verified"] != "1":
        raise RuntimeError("solver returned an unverified solution")
    if fields["stats_mode"] != "none":
        raise RuntimeError(
            f"expected benchmark stats_mode=none, got {fields['stats_mode']!r}"
        )
    return int(fields["profit"]), int(fields["time_us"])


def run_solver(
    executable: Path, solver: str, instance: Path, tso_budget: int
) -> tuple[int, int]:
    command = [str(executable), solver, str(instance)]
    if solver == "optimized":
        command.extend(
            ["--kernel", "auto", f"--tso-max-transitions={tso_budget}"]
        )
    completed = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{' '.join(command)} failed ({completed.returncode}): "
            f"{completed.stderr.strip()}"
        )
    return parse_solver_output(completed.stdout)


def discover_instances(root: Path, selected: list[str] | None, limit: int):
    families = sorted(path for path in root.iterdir() if path.is_dir())
    if selected:
        requested = set(selected)
        families = [path for path in families if path.name in requested]
        missing = requested - {path.name for path in families}
        if missing:
            raise RuntimeError(f"unknown families: {', '.join(sorted(missing))}")
    for family in families:
        instances = sorted(family.rglob("*.ukp"))
        if limit:
            instances = instances[:limit]
        for instance in instances:
            yield family.name, instance


def main() -> int:
    args = arguments()
    instances_root = args.instances.resolve()
    executable = args.solver.resolve()
    if not instances_root.is_dir():
        raise RuntimeError(f"instances directory not found: {instances_root}")
    if not executable.is_file():
        raise RuntimeError(
            f"benchmark executable not found: {executable}; build target "
            "ukp_solve_benchmark first"
        )

    instances = list(
        discover_instances(instances_root, args.family, args.limit_per_family)
    )
    if not instances:
        raise RuntimeError("no .ukp instances selected")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as output_file:
        writer = csv.writer(output_file)
        writer.writerow(
            [
                "family",
                "instance",
                "repetitions",
                "faithful_median_us",
                "optimized_median_us",
                "speedup_faithful_over_optimized",
                "profit",
            ]
        )
        for number, (family, instance) in enumerate(instances, start=1):
            times = {"faithful": [], "optimized": []}
            profits: dict[str, int] = {}
            for repetition in range(args.repetitions):
                order = (
                    ("faithful", "optimized")
                    if repetition % 2 == 0
                    else ("optimized", "faithful")
                )
                for solver in order:
                    profit, elapsed_us = run_solver(
                        executable, solver, instance, args.tso_max_transitions
                    )
                    profits[solver] = profit
                    times[solver].append(elapsed_us)
            if profits["faithful"] != profits["optimized"]:
                raise RuntimeError(
                    f"profit divergence on {instance}: faithful="
                    f"{profits['faithful']}, optimized={profits['optimized']}"
                )
            faithful_us = statistics.median(times["faithful"])
            optimized_us = statistics.median(times["optimized"])
            speedup = faithful_us / optimized_us if optimized_us else float("inf")
            writer.writerow(
                [
                    family,
                    instance.relative_to(instances_root),
                    args.repetitions,
                    faithful_us,
                    optimized_us,
                    f"{speedup:.6f}",
                    profits["faithful"],
                ]
            )
            output_file.flush()
            print(
                f"[{number}/{len(instances)}] {family}/{instance.name}: "
                f"{speedup:.3f}x",
                file=sys.stderr,
            )
    print(f"results: {args.output.resolve()}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"compare_optimized_faithful: {error}", file=sys.stderr)
        raise SystemExit(2)
