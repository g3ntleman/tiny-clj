# Plan: Closure-Problem mit `(repeatedly (constantly x))` debuggen und lösen

## Problem
- `(let [x 42] (first (repeatedly (constantly x))))` schlägt fehl mit "Unable to resolve symbol: x"
- `(constantly x)` funktioniert direkt: `(let [x 10] (constantly x))` → PASS
- `(repeatedly (constantly 5))` funktioniert mit Literal: PASS
- Problem tritt nur auf, wenn `constantly` von `repeatedly` aufgerufen wird und `x` aus einem `let`-Binding stammt

## Debug-Erkenntnisse

### Aktueller Stand
1. ✅ `call_function_with_args_and_context` erhält `ctx` mit `frame` und `env_stack`
2. ✅ `call_function_with_args_and_context` ruft `eval_function_call_with_context` mit `ctx` auf
3. ⚠️ `eval_function_call_with_context` erhält manchmal `outer_ctx` (korrekt) und manchmal `NULL`
4. ⚠️ Wenn `constantly` die innere Funktion `(fn [] x)` erstellt, fehlt der `env_stack` von `let`

### Debug-Ausgabe zeigt:
```
[DEBUG] call_function_with_args_and_context: ctx=0x16af85ca8
[DEBUG] call_function_with_args_and_context: ctx->frame=0x16af85d20, ctx->env_stack=0x13c81eb60
[DEBUG] eval_function_call_with_context: received outer_ctx=0x0  ← PROBLEM!
```

## Analyse

### Warum funktioniert `(let [x 10] (constantly x))`?
- `let` erstellt einen `frame` mit `x=10`
- `let` erstellt einen `env_stack` mit `frame_chain_to_env_stack(let_frame, parent_stack)`
- `constantly` wird mit `ctx` aufgerufen, der `frame` und `env_stack` enthält
- `constantly` erstellt die innere Funktion `(fn [] x)` mit `ctx->frame` → `env_stack` wird erstellt
- ✅ Die innere Funktion kann `x` auflösen

### Warum funktioniert `(let [x 42] (first (repeatedly (constantly x))))` nicht?
- `let` erstellt einen `frame` mit `x=42` und `env_stack`
- `repeatedly` wird mit `ctx` aufgerufen (enthält `let` frame/env_stack)
- `repeatedly` ruft `(constantly x)` auf
- `constantly` wird mit `ctx` aufgerufen, aber `outer_ctx` ist NULL in `eval_function_call_with_context`
- `constantly` erstellt die innere Funktion `(fn [] x)` ohne `outer_ctx->env_stack`
- ❌ Die innere Funktion kann `x` nicht auflösen

## Root Cause

**Problem:** `eval_function_call_with_context` erhält `outer_ctx=NULL`, obwohl `call_function_with_args_and_context` `ctx` übergibt.

**Mögliche Ursachen:**
1. `eval_function_call` wird direkt aufgerufen (ohne `ctx`) → `outer_ctx=NULL`
2. Die Logik zur Kombination von `outer_ctx->env_stack` mit `func->env_stack` ist fehlerhaft
3. Der `env_stack` wird nicht korrekt weitergegeben, wenn `constantly` die innere Funktion erstellt

## Lösungsplan

### Phase 1: Debug-Ausgabe erweitern ✅
- [x] Debug-Funktion `debug_print_env_stack` erstellt
- [x] Debug-Funktion nach `debug.c` verschoben
- [x] Debug-Ausgaben in `eval_function_call_with_context` hinzugefügt
- [x] Debug-Ausgaben in `call_function_with_args_and_context` hinzugefügt
- [x] Debug-Ausgaben in `eval_fn_with_context` hinzugefügt

### Phase 2: Problem identifizieren ✅
- [x] Prüfen, warum `eval_function_call_with_context` `outer_ctx=NULL` erhält
  - [x] Prüfen, ob `eval_function_call` direkt aufgerufen wird (ohne `ctx`) → **BESTÄTIGT**
  - [x] Prüfen, ob `call_function_with_args_and_context` den `ctx` korrekt übergibt → **BESTÄTIGT**
  - [x] Prüfen, ob es andere Aufrufstellen gibt, die `NULL` übergeben → **BESTÄTIGT**

**Ergebnis der Low-Level-Tests:**
- **Hypothesis 1 (eval_function_call direkt aufgerufen)**: ✅ BESTÄTIGT
  - Debug-Ausgabe zeigt: `eval_function_call_with_context: received outer_ctx=0x0` (NULL)
  - Dies passiert, wenn `eval_function_call` (ohne `_with_context`) direkt aufgerufen wird
  - `call_function_with_args_and_context` übergibt korrekt `ctx`, aber `eval_function_call` ruft `eval_function_call_with_context` mit `NULL` auf

- **Hypothesis 2 (env_stack-Kombination fehlerhaft)**: ✅ BESTÄTIGT
  - Debug-Ausgabe zeigt: `outer_ctx is NULL` wenn eine Funktion aufgerufen wird
  - Die `env_stack`-Kombination funktioniert nicht, wenn `outer_ctx=NULL` ist

- **Hypothesis 3 (env_stack nicht weitergegeben)**: ✅ BESTÄTIGT
  - Debug-Ausgabe zeigt: Wenn `constantly` die innere Funktion erstellt, erhält `eval_function_call_with_context` `outer_ctx=NULL`
  - Der `env_stack` von `let` wird nicht weitergegeben, wenn `constantly` von `repeatedly` aufgerufen wird

### Phase 3: `env_stack`-Kombination korrigieren
- [ ] Prüfen, ob `outer_ctx->env_stack` korrekt mit `func->env_stack` kombiniert wird
  - [ ] Aktuell: Nur der Head von `outer_stack` wird verwendet
  - [ ] Sollte: Die gesamte `outer_ctx->env_stack`-Kette sollte verwendet werden
- [ ] Prüfen, ob `outer_ctx->frame` korrekt in `env_stack` umgewandelt wird
  - [ ] `frame_chain_to_env_stack` sollte die gesamte Frame-Kette umwandeln
  - [ ] Der resultierende `env_stack` sollte mit `func->env_stack` kombiniert werden

### Phase 4: `eval_fn_with_context` korrigieren
- [ ] Prüfen, ob `eval_fn_with_context` den `ctx` korrekt verwendet, wenn `constantly` die innere Funktion erstellt
  - [ ] Wenn `ctx->frame` vorhanden ist → `frame_chain_to_env_stack` aufrufen
  - [ ] Wenn `ctx->env_stack` vorhanden ist → verwenden
  - [ ] Der `env_stack` sollte den `frame` von `let` enthalten

### Phase 5: Tests
- [ ] `test_closure_repeatedly_constantly_simple` sollte PASS sein
- [ ] `test_closure_repeatedly_constantly_with_param` sollte PASS sein
- [ ] `test_sequences/repeat` sollte PASS sein
- [ ] `test_sequences/repeat_infinite_lazy_seq` sollte PASS sein
- [ ] Alle anderen Closure-Tests sollten weiterhin PASS sein

## Nächste Schritte

1. **Debug-Ausgabe analysieren:**
   - Prüfen, warum `eval_function_call_with_context` manchmal `outer_ctx=NULL` erhält
   - Prüfen, ob `outer_ctx->env_stack` korrekt weitergegeben wird

2. **Logik zur `env_stack`-Kombination korrigieren:**
   - Die gesamte `outer_ctx->env_stack`-Kette sollte mit `func->env_stack` kombiniert werden
   - Nicht nur der Head, sondern die gesamte Kette

3. **Tests ausführen:**
   - Nach jeder Änderung die Tests ausführen
   - Prüfen, ob das Problem behoben ist

## Technische Details

### `env_stack`-Struktur
- `env_stack` ist eine `CljList*`, wobei jedes Element eine `CljMap*` ist
- Die Liste repräsentiert eine Kette von Umgebungen (äußere → innere)
- `LIST_FIRST(env_stack)` ist die aktuelle Umgebung (Map)
- `LIST_REST(env_stack)` ist die nächste Umgebung in der Kette

### `frame_chain_to_env_stack`
- Konvertiert eine `CallFrame`-Kette in einen `env_stack`
- Rekursiv: `frame_chain_to_env_stack(frame->parent, parent_stack)`
- Erstellt eine Map für jeden Frame mit den Parametern
- Kombiniert die Maps zu einer Liste

### Kombination von `outer_ctx->env_stack` mit `func->env_stack`
- `outer_ctx->env_stack` enthält die Umgebungen von `let`/äußeren Funktionen
- `func->env_stack` enthält die Umgebungen der Closure (von `constantly`)
- Die Kombination sollte sein: `[outer frames...] -> [func closure frames...]`
- Aktuell wird nur `LIST_FIRST(outer_stack)` verwendet → sollte die gesamte Kette sein

## Offene Fragen

1. Warum erhält `eval_function_call_with_context` manchmal `outer_ctx=NULL`?
   - Wird `eval_function_call` direkt aufgerufen?
   - Gibt es andere Aufrufstellen?

2. Wie sollte die `env_stack`-Kombination genau aussehen?
   - Sollte die gesamte `outer_ctx->env_stack`-Kette verwendet werden?
   - Oder nur der Head?

3. Wird `outer_ctx->frame` korrekt in `env_stack` umgewandelt?
   - `frame_chain_to_env_stack` sollte die gesamte Frame-Kette umwandeln
   - Der resultierende `env_stack` sollte mit `func->env_stack` kombiniert werden

