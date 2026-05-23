# EDUK2 C++ replication workspace

This repository contains two C++17 implementations for the Unbounded Knapsack Problem (UKP):

- `faithful`: a conservative implementation organized around the EDUK2 paper structure: preprocessing, bounds, core branch-and-bound instrumentation, sliced dynamic programming, and context-bound instrumentation.
- `optimized`: an engineering-oriented implementation using contiguous arrays and item-major unbounded DP for better cache locality.

The code is intended as a research scaffold for replication and engineering experiments. It preserves the exact UKP objective and uses the same families of bounds and dominance tests discussed in Poirriez, Yanev and Andonov's EDUK2 work. The `faithful` solver is deliberately written in a more explicit style to make comparisons against the OCaml modules easier.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run tests

```bash
./build/ukp_tests
```

## Solve an instance

```bash
./build/ukp_solve faithful data/example.ukp
./build/ukp_solve optimized data/example.ukp
```

Input format used by this workspace:

```text
n capacity
profit_0 weight_0
profit_1 weight_1
...
```

Comments beginning with `#` are ignored.

## Benchmark smoke test

```bash
./build/ukp_bench
```

## Notes for the TCC methodology

Use the implementations in this order:

1. Validate both solvers against `dense_dp_value` for small instances.
2. Compare `faithful` and the OCaml implementation on generated instances.
3. Compare `optimized` against `faithful` to isolate data-structure/cache effects.
4. Compare both against the solution-dominance implementation.

The current code is exact for the generated/tested integer UKP instances. For extremely large capacities, dense DP memory consumption can dominate; that limitation is intentional in this scaffold and should be measured explicitly.

## Updated optimized solver behavior

The `optimized` solver is now a hybrid/adaptive variant.  It runs a bounded
core Branch-and-Bound phase before the cache-friendly dynamic program.  If the
B&B incumbent reaches the global upper bound, the solver returns immediately.
Otherwise it falls back to exact DP and uses adaptive bound sampling: frequent
bound calls are kept only when they show useful pruning.

Useful commands after building:

```bash
./ukp_solve faithful ../data/example.ukp
./ukp_solve optimized ../data/example.ukp
./ukp_solve optimized ../data/ukp_bb_favorable_2000_1p2M.ukp
```

For B&B-favorable instances, expect `bb_nodes > 0` and sometimes
`states_scanned = 0` if the B&B closes the upper bound before DP.
