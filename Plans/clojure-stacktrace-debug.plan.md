# Plan: Clojure-Stacktrace im Debug-Mode

> **Constraint: Alles ausschließlich in `#ifdef DEBUG`-Blöcken.**
> Release-Builds bleiben unberührt: kein zusätzlicher Runtime-Pfad.

## Status: Kern umgesetzt

Die ursprünglichen Kernpunkte sind im aktuellen Code bereits vorhanden und aktiv:

- `CljCallStack` mit festem 64-Frame-Array (`CLJ_CALLSTACK_MAX`) in `subjective-c/src/subjective-c/exception.h`.
- Thread-local `g_clj_callstack` in `subjective-c/src/exception.c`.
- `clj_callstack_push/pop` (Debug-only), aufgerufen in `eval_function_call` für native und Clojure-Funktionen (`src/eval.c`).
- TRY/CATCH-Restore via `saved_callstack_depth` + `_TRY_SAVE_CALLSTACK` / `_CATCH_RESTORE_CALLSTACK`.
- `throw_exception` baut den Stacktrace und hängt ihn an `CLJException.stacktrace`.
- `print_exception` kann den Stacktrace ausgeben.

## Widersprüche zum alten Plan (auf Ist-Stand korrigiert)

- Der alte Plan nannte noch ein 512-Byte-Risiko in `clj_stacktrace_build()`.
  Aktueller Stand: `char buf[CLJ_CALLSTACK_MAX * 40]` in `subjective-c/src/exception.c`; der Punkt ist erledigt.
- Der alte Plan führte `test_stacktrace.c (neu)` als offen.
  Aktueller Stand: `src/tests/test_clojure_stacktrace.c` existiert bereits mit gezielten Tests:
  - `test_clojure_stacktrace_contains_function_names`
  - `test_clojure_stacktrace_cleared_after_catch`

## Technische Entscheidung

Festes C-Array bleibt die richtige Implementierung:

- keine Heap-Allokationen im Callstack-Hot-Path,
- klare Restore-Semantik bei `longjmp` (`depth` zurücksetzen),
- stabiler, deterministischer Debug-Pfad.

Ein transient/persistent vector-basierter Callstack ist hierfür unnötig komplex.

## Offene Folgearbeit (optional)

- Lesbarkeit für anonyme `fn` verbessern (z. B. synthetische Debug-Namen).
- Kein Muss für Korrektheit; nur Debug-Ergonomie.

## Nicht-Ziele

- Keine Stacktrace-Erweiterung in Release-Builds.
- Kein Expression-Level-Tracking (`if`/`let` etc.) im Stacktrace.
