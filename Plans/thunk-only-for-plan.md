# Plan: Thunk-only Implementierung für lazy `for`

Datum: 2026-01-22
Autor: GitHub Copilot (paired)

Ziel
- Implementiere eine `thunk`-only Variante für `(for ...)`, bei der der 0-Arg‑Thunk alle nötigen Iterationsdaten kapselt. Erwartung: Normalfall ist `rc == 1`, daher optimieren wir für Single‑Consumer. Falls `rc > 1`, wird der Thunk (bzw. dessen Captured State) kopiert und die `rest`-LazySeq erhält eine eigene Kopie.

Annahmen
- Bestehende `CljLazySeq`/`make_lazy_seq`/`lazy_seq_realize` API bleibt bestehen.
- Es gibt Unterstützung für native 0-arity Thunks (native functions or closures) und wir können in den Closure‑Env einen Pointer auf einen kleinen `ThunkState` legen.
- Meistens `rc == 1` für `for`-Ergebnisse (benchmarks/empirie des Projekts).

Designübersicht
- Thunk-only = native 0-arity thunk + ein kleines Heap-`ThunkState`-Objekt, das die laufende Iteration kapselt (SeqIterator Snapshot, `coll`, `binding_spec`, `env_template`, evtl. `body_id`).
- Ablauf (High level):
  1. `eval_for` erstellt `coll_eval` und einen `ThunkState` (iter init).
  2. `eval_for` erzeugt einen 0-arity native thunk (C function) mit `ThunkState*` in seinem captured env und gibt `make_lazy_seq(thunk)` zurück.
  3. `lazy_seq_realize` ruft den thunk (normaler Pfad). Der thunk:
     - realisiert die aktuelle Iterations‑Element: setzt lokale Per-Iteration‑Env (via `extend_env_with_binding`) und wertet das `body` aus (nutzt `EvalState`),
     - erstellt ein `rest`-`CljLazySeq` mit einem neuen thunk, der dieselbe `ThunkState*` verwendet (aber auf fortgeschrittener Position),
     - liefert als Ergebnis ein seqable Objekt, so dass `lazy_seq_realize` wie bisher `native_first`/`native_rest` benutzt.
  4. Nach Rückkehr des thunks: `lazy_seq_realize` cached `first` und `cached_rest` wie bisher.
  5. WICHTIG: wenn beim Setzen von `cached_rest` die Eltern‑`lazy` bereits `base.rc > 1` ist, müssen wir die `rest`-ThunkState kopieren (shallow copy + `RETAIN` Felder) und die `rest`-LazySeq so umpatchen, dass sie auf die Kopie zeigt. Dadurch teilen sich zwei LazySeq‑Referenzen nicht denselben iterierbaren State.

Warum so? (Tradeoffs)
- Vorteil: Kein neuer globaler `GeneratorState`-API; alle State‑Daten bleiben im Thunk‑Env ("thunk-only"). Minimal sichtbare API‑Änderungen.
- Optimierung: Single‑consumer (rc==1) Path gilt als billig: Thunk erstellt Rest‑LazySeqs, die im Normalfall vom selben `ThunkState` profitieren.
- Nachteil: Wir müssen Thunks/ThunkState explizit kopieren, wenn geteilt; Cloning muss korrekt und effizient implementiert (shallow copy + RETAIN).
- Zyklusgefahr: wir vermeiden, dass Thunk direkt `RETAIN` das Parent‑`CljLazySeq` (das würde Cycles erzeugen). Stattdessen lässt der Thunk nur die `ThunkState` besitzen; `lazy` selbst hält die `thunk`. `lazy_seq_realize` kontrolliert Copy/Swap‑Logik.

Datenstruktur (konzeptionell)
```c

# Plan: Thunk-only Implementierung für lazy `for` (Closure‑Env Variante)

Datum: 2026-01-22
Autor: GitHub Copilot (paired)

Ziel
- Implementiere eine `thunk`-only Variante für `(for ...)`, bei der der 0‑Arg‑Thunk (eine `CljFunction`/Closure) seinen Iterations‑State in der Closure‑Env hält. Erwartung: Normalfall ist `rc == 1`, daher optimieren wir für Single‑Consumer. Falls `rc > 1`, kopieren wir die Closure inkl. des State‑Teils.

Kernentscheidungen
- Keine neuen C‑Structs: State wird als `CljObject` im Closure‑Env gehalten (z.B. `CljMap` oder `CljVector`).
- Mutate‑in‑place: native Helfer `mutate_closure_state_inplace(thunk)` ändert den Iterator/Index im State nur wenn `thunk->base.rc == 1`.
- Clone‑on‑share: native Helfer `clone_closure_with_state(thunk)` kopiert die Closure und deep‑kopiert nur den iterator‑spezifischen Teil des State (persistent Sub‑Objekte werden nur `RETAIN`ed).

Designübersicht (Kurz)
- `eval_for` baut ein `state_obj` (CljMap/CljVector) mit: `container`, iterator index/snapshot, `binding_spec`, `env_template`, `body_ref`.
- `eval_for` erstellt eine native 0‑arity Thunk/Closure (`make_thunk_with_state(state_obj)`) und gibt `make_lazy_seq(thunk)` zurück.
- Beim Realisieren ruft `lazy_seq_realize` den Thunk (Standardpfad). Der Thunk erzeugt `first` und einen `rest_thunk` basierend auf `state_obj`:
  - Wenn `thunk->base.rc == 1`: advance `state_obj` in‑place via `mutate_closure_state_inplace`, und `rest_thunk` re‑referenziert dieselbe Closure (günstig).
  - Falls `thunk->base.rc > 1`: `clone_closure_with_state` wird benutzt, die Kopie advance, und `rest_thunk` nutzt die Kopie (COW).
- `lazy_seq_realize` kontrolliert final die `cached_rest`: wenn Parent‑`lazy` bereits `rc>1` ist, stellt sie sicher, dass `cached_rest->thunk` eine geklonte Closure mit eigenem State verwendet (ersetzt sonstige shared Thunks).

Persistente Sub‑Strukturen
- Beim Kopieren werden persistent Sub‑Strukturen (Maps/Vectors innerhalb `state_obj`) nicht tief kopiert — sie werden per `RETAIN` geteilt. Nur der Iterator/Index‑Teil wird tatsächlich dupliziert.

APIs / Helfer (konzeptionell)
- `ID make_thunk_with_state(ID state_obj)` — create native 0‑arity thunk with `state_obj` in env.
- `CljFunction* clone_closure_with_state(CljFunction *thunk)` — clone Closure + copy iterator‑relevant parts in `state_obj`.
- `bool mutate_closure_state_inplace(CljFunction *thunk)` — advance iterator in `state_obj`, only when `thunk->base.rc == 1`.

Copy‑on‑share Ablauf (zusammengefasst)
1. Thunk erzeugt `first` und ein `rest_thunk` (standardmäßig auf Basis desselben `state_obj`).
2. `lazy_seq_realize` cached `first` und `cached_rest`.
3. Wenn `lazy->base.rc > 1` (oder Test erkennt Sharing), ersetzt `lazy_seq_realize` `cached_rest->thunk` durch `clone_closure_with_state(old_thunk)` (nur dann tiefere Copy‑Arbeit).

Vorteile dieser Variante
- Keine neue Runtime‑Struct API — wir nutzen vorhandene `CljFunction`/Closure‑Env. Das reduziert Footprint und Integrationsarbeit.
- Single‑consumer Pfad ist sehr schnell (in‑place mutate). Multi‑consumer korrekt per COW.

Risiken & Hinweise
- Implementiere `clone_closure_with_state` sorgfältig: iterator/position muss wirklich kopiert werden; persistent Unterstrukturen werden nur `RETAIN`ed.
- Vermeide zyklische RETAINs: Closure darf nicht direkt den `CljLazySeq` parent stark halten.

Tests & Metriken
- Shared‑lazy Test (erzeuge zwei Referenzen auf dieselbe LazySeq, forciere `rc>1` vor Realisierung, verifiziere unabhängige Iteration).
- Allocation smoke test: vergleiche Anzahl Allokationen pro Element Single‑consumer vs baseline.

Implementierungs‑Schritte (konkret)
1. Tests & Minimal‑Failcases hinzufügen (0.5–1h)
  - Shared‑lazy Test (rc>1) und Single‑consumer Allocation Test.
2. Implementiere Closure‑State‑Helpers (1–1.5h)
  - `make_thunk_with_state(state_obj)`
  - `clone_closure_with_state(thunk)`
  - `mutate_closure_state_inplace(thunk)`
3. `eval_for` anpassen (0.5h)
  - Baue `state_obj` (CljMap/CljVector) und erstelle `thunk = make_thunk_with_state(state_obj)`; return `make_lazy_seq(thunk)`.
4. `lazy_seq_realize` anpassen (0.5h)
  - Nach Rückkehr des thunks: falls `lazy->base.rc > 1`, ersetze `cached_rest->thunk` durch `clone_closure_with_state(old_thunk)` (und `RELEASE` old thunk).
5. Tests & Validierung (1–2h)
  - Lauf targeted tests, memory profiler, und vollständige Unit‑Tests.
6. Optional: micro‑benchmarks und Tuning (0.5–1h)

Nächste Aktion
- Wenn du zustimmst, implementiere ich jetzt test‑first:
  - add shared lazy tests,
  - implement closure helpers,
  - patch `eval_for` und `lazy_seq_realize`.
Sag bitte kurz "Implementiere thunk-only (closure)" und ich fange an.

