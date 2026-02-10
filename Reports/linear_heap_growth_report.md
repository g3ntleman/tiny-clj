# Linear Heap Growth Investigation Report

Date: 2026-02-10

## Scope

Investigate per-eval memory predictability for repeated expression evaluation with `repl -e` and `(heap ...)`, and separate bounded warmup effects from persistent growth.

## Reproducible Workflows

- Smoke (quick):
  - `./scripts/heap_growth_smoke.sh`
- Detailed diagnosis (50-100 iterations recommended):
  - `ITERATIONS=60 WARMUP_EVALS=3 ./scripts/heap_growth_diagnose.sh`
- Type-level delta view:
  - `./build/tiny-clj-repl -f scripts/leak_type_breakdown.clj`

## Measured Slopes and Deltas

Before fix (`scripts/heap_growth_smoke.sh`, 12 iterations, warmup 1):

- `(+ 1 2)`: avg post-warmup delta `0` bytes
- `(reduce + (range 200))`: avg post-warmup delta `0` bytes
- `(assoc {:a 1 :b 2} :c 3)`: avg post-warmup delta `240` bytes
- `[1 2 3]`: avg post-warmup delta `96` bytes

Before fix (`scripts/heap_growth_diagnose.sh`, 60 iterations, warmup 3):

- `control_plus`: stable (`avg_post=0`, slope `0.00`)
- `control_reduce_range`: stable (`avg_post=0`, slope `0.00`)
- `suspect_assoc_literal`: persistent positive (`avg_post=240`, slope `0.00`)
- `suspect_vector_literal`: persistent positive (`avg_post=96`, slope `0.00`)
- `suspect_list_literal`: persistent positive (`avg_post=96`, slope `0.00`)
- `probe_assoc_var`: persistent positive (`avg_post=272`, slope `0.00`)
- `probe_discard_result`: persistent positive (`avg_post=240`, slope `0.00`)

After fix (`scripts/heap_growth_smoke.sh`, 12 iterations, warmup 1):

- `(+ 1 2)`: avg post-warmup delta `0` bytes
- `(reduce + (range 200))`: avg post-warmup delta `0` bytes
- `(assoc {:a 1 :b 2} :c 3)`: avg post-warmup delta `0` bytes
- `[1 2 3]`: avg post-warmup delta `0` bytes

After fix (`scripts/heap_growth_diagnose.sh`, 60 iterations, warmup 3):

- `control_plus`: stable (`avg_post=0`, slope `0.00`)
- `control_reduce_range`: stable (`avg_post=0`, slope `0.00`)
- `suspect_assoc_literal`: stable (`avg_post=0`, slope `0.00`)
- `suspect_vector_literal`: stable (`avg_post=0`, slope `0.00`)
- `suspect_list_literal`: stable (`avg_post=0`, slope `0.00`)
- `probe_discard_result`: stable (`avg_post=0`, slope `0.00`)
- `probe_assoc_var`: remaining positive (`avg_post=80`, slope `0.00`) in the var-based probe path

Interpretation:

- Main linear-growth symptom for literal collection workloads is fixed.
- Remaining positive delta appears only in a var-based probe and needs separate follow-up.

Additional isolation:

- `(heap m)` is stable (`0`), so symbol lookup alone is not the source.
- `(heap (do (assoc m :c 3) nil))` still shows `+80`, so the retained map comes from the assoc call path even when the value is discarded.

## Type-Level Findings

From `scripts/leak_type_breakdown.clj`:

- Control (`stats -> stats`) already adds measurement overhead (`Map`, `String`, `Instant`, occasional `CallsiteCache`).
- Workloads still show additional growth dominated by collection/object categories:
  - `assoc literal`: `Map` growth dominates.
  - `vector literal`: `Vector` growth is visible.
  - `list literal`: `List` growth is visible.
- Counter-probes keep positive deltas, including discard-result probe, which suggests growth is not only due to caller bindings.

## Root Cause and Fix

Primary root causes that were fixed:

- `src/eval.c`: collection literal evaluation used `ASSIGN(result, vector_conj(...)/map_assoc(...))`, which added extra `RETAIN` and left one reference alive per eval in COW paths.
- `src/builtins.c`: `native_list` returned owned list instead of pool-safe return.

Implemented fix details:

- `src/eval.c`:
  - replaced `ASSIGN` updates for map/vector literal evaluation with owned-update pattern:
    - `old_result = result; result = ...; if (result != old_result) RELEASE(old_result);`
  - removed unnecessary `RETAIN(result)` initialization in those literal-eval builders.
- `src/builtins.c`:
  - changed `native_list` to return `AUTORELEASE(head)`.
- `src/builtins.c`:
  - changed `native_assoc` to return `AUTORELEASE(result)` for pool-safe builtin return.
- `src/eval.c`:
  - reinforced top-level return policy (`eval_parsed_value`) and heap-measurement pass to bind heap results into the current autorelease pool.

## Follow-up Priority

1. Investigate remaining `probe_assoc_var` +80 bytes/eval behavior.
2. Add focused regression guard for var-based assoc path once expected behavior is decided.
3. Keep smoke + detailed scripts in regular embedded-readiness checks.

## Regression Guard Added

A deterministic runtime memory stability test was added:

- File: `src/tests/test_runtime_stats.c`
- Test: `test_runtime_stats_reduce_range_loop_stable_after_warmup`
- Goal: ensure a representative non-leaking workload (`reduce/range`) stays stable after warmup.

Targeted run:

- `./build/unit-tests --test test_runtime_stats/runtime_stats_reduce_range_loop_stable_after_warmup`
