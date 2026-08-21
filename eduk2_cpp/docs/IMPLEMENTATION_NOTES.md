# Implementation notes

## Mapping to the OCaml code

| OCaml module | C++ module |
|---|---|
| `wintpint.ml`, `wandp.ml` | `types.hpp` |
| `bounds.ml` | `bounds.hpp/cpp` |
| `dominance.ml` | `dominance.hpp/cpp` |
| `bandbukp2.ml` | core-B&B helper inside `faithful_solver.cpp` |
| `slice.ml`, `eduk.ml` | `faithful_solver.cpp` |
| optimized engineering variant | `optimized_solver.cpp` |

## Faithful implementation

`paper_faithful_mode=true` is the default.  It disables all experimental
extensions (simple dominance, remainder core ordering, modular dominance, and
core-local multiple dominance), even if their flags are set.  The mandatory
best-item multiple-dominance reduction is not a core-local extension.

| Paper phase | C++ block |
|---|---|
| 1. Initial reduction | `preprocess_items`: select the best item directly from the working instance with `better_ratio`, optional simple dominance, mandatory global `remove_multiple_dominated_by_best`, then ratio sort |
| 2. Bounds/reduction | `initialize_bounds`, `reduce_variables_by_bound` |
| 3. Core B&B | `traverse_core`, `greedy_fill`, `backtrack`/`complete` |
| 4. Listing 1 sliced DP | sequence of skip-points for the strict increases of `f(N,y)`, contextual fathoming, completion, threshold dominance |

`Stats::slices` records the Listing 1 work per actual slice, and the global
and contextual bound types are exposed through stable names (`U3`, `V`,
`TauStar`, `BestItemStar`, or `none`).

The faithful implementation keeps the EDUK2 decomposition visible:

- preprocessing by simple and multiple dominance;
- ratio-ordered items;
- Martello-Toth `U3` and normalized `V` bound;
- branch-and-bound on a small core as a lower-bound improvement stage;
- sliced DP over capacities;
- context-bound instrumentation using the same condition as the paper: `f(y) + U(c-y) <= z`.

In faithful mode the order is deliberately fixed: select the best item from the
working instance, apply global multiple dominance with it, sort the reduced
global list by `better_ratio`, and take its first
`C = min(n, max(100, n / 100))` items as the core.  The B&B receives only a
local copy of that prefix and is capped at `B = 10,000` nodes; the DP retains
the separate, unchanged global list.  Ordering by `capacity % weight` and all
core-local reductions are experimental-only and are never reached by this
faithful path.

The exact solution is certified by a dynamic-programming sequence of skip-points. `value_at(y)` returns the last point whose weight is at most `y`; items eliminated by threshold dominance do not participate in recurrences of later slices.

### Incremental backfill correspondence

PYAsUKP stores `next_built_upon` per introduced item. `Slice.one` resumes that
index in `sequence_result` and consumes parents only while the shifted target
is at most `bsup`. The faithful C++ mapping is:

- `sequence_result` parents that survive contextual fathoming map to
  `expandable_points_`;
- an `ItemCursor::next_built_upon` is an index in that append-only vector;
- the C++ slice upper bound `yb` maps to `bsup` and limits cursor advancement to
  parents of weight at most `yb - w_i`;
- candidates are still consumed in increasing target weight, while immutable
  `tie_rank` implements the OCaml decreasing-ratio priority independently of
  physical generation order;
- a threshold-removed item retains a retired cursor whose terminal index is the
  expandable prefix present at removal. It emits no transitions from later
  states, but finishes the prefix that the old eager C++ implementation had
  already scheduled. This preserves its candidates, predecessors, and
  periodicity while deferring work past the stopping boundary.

Cursor metadata is kept in a cold table indexed by immutable `tie_rank`, so the
24-byte hot `ActiveItem` layout is unchanged. `cursor_advances` counts shifted
parents actually visited; `backfill_attempts` is the subset belonging to the
prefix present at introduction; `historical_states_avoided` counts represented
cursor parents whose capacity-feasible successor remains deferred at the exact
stop.

## Optimized implementation

The optimized implementation changes the data layout:

- contiguous arrays/vectors;
- item-major unbounded DP;
- simpler backtracking arrays;
- fewer branches inside the innermost loop.

It is intended to quantify implementation and memory-locality effects, not to claim a new algorithmic contribution.

## Optimized hybrid/adaptive solver update

The `optimized` solver is now an optimized hybrid solver rather than a pure
cache-friendly DP.  It keeps the contiguous dynamic-programming table, but adds:

1. a bounded core Branch-and-Bound phase before DP;
2. early return when the core B&B incumbent closes the global upper bound;
3. adaptive bound sampling during DP, reducing or disabling bound calls when the
   observed pruning rate is too low.

This makes the optimized solver closer to the intended EDUK2 hybrid behavior
while avoiding the original implementation's fixed cost of evaluating a bound at
nearly every capacity/state.

## Optimized adaptive hybrid policy

The optimized solver is intentionally not a line-by-line port of PYAsUKP.  It
keeps the EDUK2 idea of combining preprocessing, bounds, a core branch-and-bound
probe and an exact dynamic programming fallback, but it controls the hybrid
components adaptively.

The policy is:

1. compute the global bound and a greedy incumbent;
2. run a small B&B probe on the best-ratio core;
3. return immediately if the core B&B closes the global upper bound;
4. escalate B&B only if the probe improved the incumbent and left a very small
   relative gap, or if preprocessing reduced the instance to a small core;
5. otherwise switch to the cache-friendly exact DP fallback;
6. during DP, sample context bounds only when the instance looks favorable.  The
   sampling budget is capped, and bounds are disabled when early samples are not
   useful.

Correctness is preserved because B&B and context bounds are used only for early
termination or performance guidance.  If they are disabled, the dense unbounded
DP still computes the exact optimum.
