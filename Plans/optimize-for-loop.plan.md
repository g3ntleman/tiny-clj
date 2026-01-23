# Plan: Optimierung von `(for ...)` / Lazy `for`-Performance

Datum: 2026-01-22
Autor: GitHub Copilot (pair-programming plan)

Ziel
- `for` ist sehr häufig; Ziel ist eine lazy, speicher- und allocation-effiziente Implementierung, die Clojure‑Semantik (Laziness, Reihenfolge, Bindings) beibehält und auf Embedded‑Targets mit manueller RC gut skaliert.

Randbedingungen / Constraints
- Manuelle Referenzzählung (`RETAIN`/`RELEASE`/`AUTORELEASE`).
- Bestehende `CljLazySeq`, `make_lazy_seq`, `lazy_seq_realize` und `SeqIterator` in `src/seq.c` nutzen.
- Inkrementelle Änderung; Rückfall auf Chunked‑Lazy wenn nötig.

Design-Optionen (Kurz)
1. GeneratorState + COW (Empfohlen)
   - Erzeuge ein RC‑verwaltetes `GeneratorState`-Objekt, das die Iteration über `coll_eval` und die gebundenen Env‑Vorlagen hält.
   - `CljLazySeq` referenziert optional `GeneratorState` statt reinen Thunk. Realisierung:
     - Wenn `gen->rc == 1` → In‑place Advance (kein malloc für `rest`).
     - Wenn `gen->rc > 1` → Clone `GeneratorState` (COW) und allociere neuen Lazy‑Node für `rest`.
   - Vorteil: sehr geringe Allokationsrate in Single‑Consumer-Fall; COW sichert korrekte Sharing‑Semantik.
   - Aufwand: moderat–hoch (neue structs + careful RC handling).

2. Chunked lazy-seq (Fallback / schnelle Win)
   - Thunk produziert kleine `CljVector`-Chunks (z.B. 16–64 Elemente). Pro Chunk eine Allokation.
   - Einfacher, kleine Änderungen in `eval_for` (thunk erzeugt chunked seq), niedriges Risiko.
   - Nachteil: zusätzlicher Indirection, leicht abweichende Speicher-/Zugriffscharakteristik.

3. Vector‑backed eager build
   - `make_vector` und `vector_conj` in forward order; vermeidet `reverse` aber ist EAGER — nicht Clojure‑konform für lazy `for`.

Empfehlung
- Implementiere Variante (1) als langfristige Optimization; starte jedoch mit (2) als smal, testbarer Zwischenstufe, falls Zeit knapp ist.

Konkrete Implementierungsschritte
1. Spezifikation & Tests (0.5–1h)
   - Ergänze Unit‑Tests: per‑element allocation counting, multi‑consumer sharing, correctness (bestehende `src/tests/test_for_lazy.c`).
   - Füge microbench harness für `(for [i (range N)] ...)` measurement und allocation profiling.

2. API + Data Structures (0.5–1h)
   - Definiere `struct GeneratorState { base:CljObject, rc; SeqIterator iter; ID env_template; ID binding_info; /*...*/ }` mit `RETAIN`/`RELEASE` semantics.
   - Helper: `make_generator_state(ID coll_eval, ID env_template, binding_meta)`, `clone_generator_state(GeneratorState*)`, `retain/release`.

3. Extend `CljLazySeq` (small change) (0.5h)
   - Füge Feld `ID gen_state` und `bool gen_mode` oder erstelle `make_lazy_seq_from_gen()`.
   - Aktualisiere `release_lazy_seq` um `RELEASE(lazy->gen_state)` falls gesetzt.

4. Realizer: `generator_realize(CljLazySeq *lazy)` (1–2h)
   - Wenn `lazy->gen_state`:
     - Acquire EvalState, evaluate current binding/body using stored `SeqIterator` + env template.
     - If `gen->rc == 1`: advance `GeneratorState` in-place and set `lazy->first` and `lazy->cached_rest` to either `SYM_NIL`/next lazy node that reuses same gen_state (in-place update) or `NULL` when exhausted.
     - If `gen->rc > 1`: clone state for rest, allocate new `CljLazySeq` for `rest` pointing to the cloned state.
   - Handle `SYM_NIL` vs empty as existing code does.
   - Be meticulous with `RETAIN`/`ASSIGN`/`RELEASE` and `builtin_set_eval_state` usage.

5. `eval_for` changes (1–2h)
   - Instead of building entire list, build initial `GeneratorState` capturing `make_seq(coll_eval)` and the binding template; return `make_lazy_seq_from_gen(gen)`.
   - Preserve correct per-iteration env creation (use env_template + extend_env_with_binding at realization time).

6. Tests & Validation (1–2h)
   - Run unit tests (targeted) and memory profiler.
   - Add tests for shared lazy seqs: two references to same lazy seq cause COW path (rc>1) on first split.
   - Validate no leaks and correct ordering.

7. Benchmarks & Tuning (1h)
   - Measure allocation count per element and latency; set baseline (current eager) and target (e.g., >80% reduction in per-element mallocs for single consumer).
   - Adjust strategy (chunk size, lazy node reuse) if needed.

8. Rollout & Documentation (0.5h)
   - Add comments in `src/seq.c`/`src/eval.c`, update `Plans/` and TODOs, request maintainer review (RC/GC-sensitive change).

Risks & Mitigations
- RC errors (use small tests + valgrind/ASAN where possible). Mitigation: unit tests exercising retain/release and memory profiler.
- Complexity of env capture: create minimal `env_template` helper and reuse existing `extend_env_with_binding` at realization time.
- Platform differences (embedded): keep chunked fallback to reduce memory pressure.

Acceptance Criteria
- Semantic: `for` remains lazy, preserves order, supports infinite sequences and early termination.
- Performance: Single‑consumer case reduces per-element heap allocations significantly (target: large reduction vs current eager+reverse). Multi‑consumer still correct (COW path).
- Tests: `src/tests/test_for_lazy.c` and new allocation/ sharing tests pass; no regressions in core suites.

Timeline (rough)
- Design & tests: 1 hour
- Implement generator + integrate: 2–4 hours
- Tests & fixes: 1–2 hours
- Benchmark + docs: 1 hour

Nächste Schritte (konkret)
1. Ich implementiere zuerst die Chunked‑Lazy fallback (kleiner Patch), liefere Tests und messe. Wenn du zustimmst, implementiere ich im Anschluss GeneratorState+COW (größerer Patch) für die Produktionsoptimierung.
2. Ich werde die Änderungen schrittweise per Patch vornehmen, jeweils mit Unit‑Tests.

---
Datei erstellt: `Plans/optimize-for-loop.plan.md`
Wenn du GeneratorState+COW willst, sage "Implementiere GeneratorState" — ich mache dann den Patch.