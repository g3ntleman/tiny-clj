# Testing Workflow (Heap/Leak Focus)

This document defines the practical workflow for debugging and stabilizing heap-growth tests.

## Principles

- Keep tests behavior-focused; avoid masking issues with runner expression warmups.
- Prefer ownership fixes over threshold inflation.
- Keep changes policy-conformant with `docs/MEMORY_POLICY.md`.

## Recommended Debug Loop

1. Reproduce the exact failing test.
2. Isolate the failing behavior as a minimal expression.
3. Measure with `(heap ...)` using `repl -e`.
4. Verify whether growth is:
   - one-time bootstrap/fixture cost, or
   - recurring ownership leak.
5. Apply the smallest ownership fix in the relevant call path.
6. Re-run:
   - focused tests,
   - then full suite.

## Use `(heap ...)` Proactively

Use command-line probes to isolate behavior:

```sh
./tiny-clj-repl -e "(heap (= (concat (list 1 2) (list 3 4)) (list 1 2 3 4)))"
```

Repeat probes to distinguish first-run costs from recurring growth.

## Shared Tests and Baselines

- Shared groups should run with correct group setup context.
- Fixtures that are required for a whole group (for example namespace load)
  should happen before baseline capture.
- Do not introduce ad-hoc runner expression warmups as a leak workaround.

## Heap-Limit Changes

If a heap limit must be increased temporarily:

- record old value as rollback target,
- record new temporary value,
- add a concrete TODO for reduction,
- track it in `Reports/HEAP_LIMIT_TODOS.md`.
