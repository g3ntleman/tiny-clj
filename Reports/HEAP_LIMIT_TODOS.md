# Heap-Limit-Anhebungen – TODOs Leak-Suche

Wenn ein Test-Heap-Limit angehoben wird, hier eintragen: **Datei**, **alter Wert (Ziel)**, **neuer Wert**, **TODO**.

| Datei | Alter Wert (Ziel) | Neuer Wert | TODO |
|-------|-------------------|------------|------|
| `src/tests/test_arithmetic.c` | 200 | 200 | Erledigt: tearDown ruft `autorelease_pool_free()`; Limit 200 |
| `src/tests/test_core_functions.c` | 2048 | 4096 | Lazy-Seq/keep/mapcat über 2048; Limit 4096 für Gruppe |
| `src/tests/test_core_functions.c` (`concat_two_lists`) | 400 | 450 | TODO: remaining lazy concat allocations reduzieren und wieder auf 400 senken |
| `src/tests/test_core_functions.c` (`concat_empty_first`) | 400 | 450 | TODO: remaining lazy concat allocations reduzieren und wieder auf 400 senken |
| `src/tests/test_core_functions.c` (`concat_returns_lazy_seq`) | 400 | 450 | TODO: remaining lazy concat allocations reduzieren und wieder auf 400 senken |
| `src/tests/test_loops.c` (`for_multiple_bindings`) | 400 | 800 | TODO: for/macroexpand allocations reduzieren und wieder auf 400 senken |
| `src/tests/test_loops.c` (`for_let_modifier`) | 400 | 600 | TODO: for/macroexpand allocations reduzieren und wieder auf 400 senken |
| `src/tests/test_event_loop_latency.c` (`run_next_prioritizes_older…`) | 64 | 1024 | TODO: Heap-Growth in tearDown / run_next-Vorspann reduzieren und wieder auf 64 senken |
