# EDUK2 C++ replication workspace

This repository contains two C++17 implementations for the Unbounded Knapsack Problem (UKP):

- `faithful`: a conservative and basic implementation organized around the EDUK2 paper structure
- `optimized`: an engineering-oriented implementation using contiguous arrays and item-major unbounded DP for better cache locality.

The code is intended as a research scaffold for replication and engineering experiments. It preserves the exact UKP objective and uses the same families of bounds and dominance tests discussed in Poirriez, Yanev and Andonov's EDUK2 work. The `faithful` solver is deliberately written in a more explicit style to make comparisons against the OCaml modules easier.

`faithful` defaults to `paper_faithful_mode=true`.  In that mode the four
paper phases are explicit: initial best-item reduction, bounds and bound
reduction, ratio-ordered core B&B, and Listing 1's sliced DP/fathoming.  The
experimental `--simple-dominance`, `--core-remainder-ordering`,
`--modular-dominance`, and `--core-multiple-dominance` switches take effect
only with `--no-paper-faithful`.  The best-item multiple-dominance reduction
remains mandatory in phase 1 in either mode.

In faithful mode phase 1 selects the best item directly from the working
instance using `better_ratio`, then applies global multiple dominance and
sorts the remaining global DP list by that same comparator.  The B&B core is
exactly the first `min(n, max(100, n / 100))` items of that list and uses a
fixed 10,000-node limit.  B&B works on a local core copy; the DP keeps the
global list unchanged.  Remainder ordering (`capacity % weight`) is only an
experimental, non-faithful option.

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
./build/ukp_solve faithful data/example.ukp --no-paper-faithful --simple-dominance --core-remainder-ordering --modular-dominance --core-multiple-dominance
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
