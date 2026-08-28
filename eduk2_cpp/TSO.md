# Terminating Step-Off kernel

`TerminatingStepOff` is an independent exact kernel. It receives the original
`Instance` and uses only `common_preprocess_items`, whose transformation removes
items with nonpositive weight/profit or weight above the capacity. It never sees
EDUK2's multiple-dominance, bound-reduced, core, or residual item lists.

The items are ordered by nondecreasing profit/weight without floating point;
cross-products use `__int128`. Equal ratios put the lighter item later. For each
reachable breakpoint `y`, the ordered step-off recurrence extends only with
items whose ordered index is at least the predecessor index stored at `y`.
Thus every multiset has one nondecreasing-index construction, while no feasible
multiset is lost. The value vector consequently represents the same optimum as
the ordinary unbounded-knapsack recurrence. On equal values, retaining the
largest predecessor index preserves the value and only shrinks later extension
sets; the tied solution itself has that largest last index, so its valid
continuations remain represented.

Let `x*` be the last improving state whose predecessor is not the best item and
let `lambda` be the maximum weight in that predecessor's suffix. Gilmore and
Gomory's periodicity result says that after `x* + lambda`, every optimum is the
corresponding earlier optimum plus copies of the final (most efficient) item.
The kernel stops exactly there and reconstructs those copies plus the stored
predecessor chain. Arithmetic on profits and reconstructed totals uses the
repository's checked integer helpers.

Before allocating the two `C + 1` arrays, their count and byte product are
checked for representability, vector limits, and a configurable memory budget
(512 MiB by default). Failure returns `kernel_not_applicable`, not a solver
error.

Manual execution is `ukp_solve optimized FILE --kernel eduk2` or `--kernel tso`.
The default, `--kernel auto`, runs common preprocessing once and applies a
conservative structural dispatcher.  It selects TSO only when its table fits
the configured memory budget and all of these calibrated conditions hold:

- at most 5,000 items remain after common preprocessing;
- every remaining item is within 1% of the best efficiency;
- `capacity / min_weight <= 50`;
- `capacity * min(n_after_common, capacity / min_weight + 1) <= 25,000,000`.

Every other instance uses EDUK2.  TSO returning `kernel_not_applicable` also
falls back to EDUK2, so dispatch is never part of the optimality argument.

`ukp_kernel_bench FILE...` emits the complete cheap feature row, both isolated
kernel timings, their ratio, classification, and equality flag.  Evaluate its
CSV with `scripts/analyze_dispatcher.py`; the report includes both always-kernel
baselines, oracle regret, confusion counts, regressions, and family slices.
`test_tso` contains 5,000 dense-oracle cases, exact reconstruction checks, and
TSO-versus-faithful corpus checks.
