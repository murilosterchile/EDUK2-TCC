# EDUK2 C++ replication workspace

This repository contains two C++17 implementations for the Unbounded Knapsack Problem (UKP):

- `faithful`: a conservative and basic implementation organized around the EDUK2 paper structure
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
