---
name: Lokales def ctx-Fix
overview: Test-First-Behebung des Problems, dass `eval_def` keinen `EvalContext *ctx` erhält und daher `CLJ_SLOT_REF`-Wertausdrücke (aus lexikalischen `let`-Bindings) nicht auflösen kann.
todos:
  - id: regression-tests
    content: "Failing regression tests in src/tests/test_defn.c (oder test_let.c) schreiben: test_def_in_let_sees_binding, test_def_value_from_macro_generated_let"
    status: pending
  - id: fix-eval-def
    content: "Fix in src/eval.c + src/eval.h: eval_def Signatur um const EvalContext *ctx erweitern, Aufrufstelle ctx übergeben, eval_body(value_expr, env, st, ctx) statt eval_body(value_expr, ns_mappings, st, NULL)"
    status: pending
  - id: run-tests
    content: "Build + ./build/unit-tests -test test_def ausführen, dann volle Suite prüfen"
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen
    status: pending
isProject: false
---

# Fix: lokales `def` sieht lexikalische Bindings nicht

## Problem

`eval_def` in [`src/eval.c`](src/eval.c) ruft `eval_body` ohne `EvalContext` auf:

```c
// eval.c:1991
CljPersistentMap *eval_env = (st && st->current_ns) ? st->current_ns->mappings : env;
value = eval_body(value_expr, eval_env, st, NULL);  // ctx=NULL → kein Frame
```

Enthält `value_expr` einen `CLJ_SLOT_REF` (Canonical-Slot-Ref für ein `let`-Binding), schlägt die Auflösung fehl – in Debug-Builds mit `CLJ_ASSERT(ctx && ctx->frame && "CLJ_SLOT_REF requires frame context")`.

**Clojure/JVM-Verhalten** (bestätigt via `clj -e`):

- `(let [x 5] (def y x)) y` → `5` ✓
- `(do (def x 1) (def y (+ x 1)) y)` → `2` ✓

## Wurzelursache

`eval_def` (Zeile 1963, `eval.h:43`) hat keine `ctx`-Parameter, während alle anderen Special-Forms mit lokalen Bindings (`eval_let`, `eval_doseq`, `eval_dotimes`) `const EvalContext *ctx` erhalten und weitergeben.

```c
// Dispatch (eval.c:1840) – ctx wird nicht übergeben:
result = eval_def(args, effective_env, effective_st);       // BUG
// Vergleich:
result = eval_special_defmacro(args, effective_env, effective_st, ctx); // korrekt
```

## Betroffene Dateien

- [`src/eval.c`](src/eval.c) – `eval_def` Implementierung (Zeile 1963) und Dispatch (Zeile 1840)
- [`src/eval.h`](src/eval.h) – Deklaration `eval_def` (Zeile 43)

## Fix

```c
// eval.h – Signatur ergänzen:
ID eval_def(CljPersistentVector *args, CljPersistentMap *env,
            EvalState *st, const EvalContext *ctx);

// eval.c – Dispatch (Zeile 1840):
result = eval_def(args, effective_env, effective_st, ctx);

// eval.c – eval_def Implementierung:
ID eval_def(CljPersistentVector *args, CljPersistentMap *env,
            EvalState *st, const EvalContext *ctx) {
  // ...
  value = eval_body(value_expr, env, st, ctx);  // ctx weiterreichen
}
```

Mit `ctx != NULL` → `eval_body_with_params` → `CLJ_SLOT_REF` wird per Frame aufgelöst.  
Mit `ctx == NULL` (Top-Level) → `eval_body_no_ctx` → `eval_env_or_ns_mappings` greift wie bisher.

## Tests (in `src/tests/test_let.c` oder `test_defn.c`)

Vor der Implementierung schreiben (sie scheitern), danach sollen sie bestehen:

- `test_def_in_let_sees_binding` – `(let [x 5] (def y x)) y` → `5`
- `test_def_in_let_multi` – `(let [a 1 b 2] (def z (+ a b))) z` → `3`
- `test_def_value_from_macro_let` – Makro generiert `(let [v val] (def name v))` → Ergebnis korrekt

## Teststrategie

1. Tests schreiben → Build → Fehlschlag / Assert bestätigen
2. Fix in `eval.c` + `eval.h` implementieren
3. Build + `./build/unit-tests -test test_let` (oder `test_defn`) → alle grün
4. Vollständige Test-Suite laufen lassen: `./build/unit-tests`
