# Problem Summary: Alias Setting for Pre-loaded Namespaces

## Problem Description

Aliases werden nicht gesetzt, wenn ein Namespace bereits geladen ist (`clojure.string`), aber funktionieren korrekt, wenn der Namespace erst geladen werden muss (`test.alias`).

## Test Results

### Low-Level Tests (6 Tests, 3 Pass, 3 Fail)

**✅ Passing Tests:**
1. `test_lowlevel_alias_sym_extraction` - `alias_sym` wird korrekt aus Vector extrahiert
2. `test_lowlevel_current_ns_correct` - `st->current_ns` ist korrekt
3. `test_lowlevel_ns_set_alias_stores` - `ns_set_alias` speichert Alias korrekt

**❌ Failing Tests:**
4. `test_lowlevel_native_require_sets_alias_preloaded` - Alias wird nicht gesetzt nach `native_require`
5. `test_lowlevel_alias_sym_not_null_when_preloaded` - Alias wird nicht gesetzt
6. `test_lowlevel_current_ns_correct_when_alias_set` - Alias wird nicht gesetzt

### Hypothesis Tests (9 Tests, 1 Pass, 8 Fail)

**✅ Passing:**
- `test_hypothesis_parser_no_resolution_when_alias_missing` - Parser verhält sich korrekt bei fehlendem Alias

**❌ Failing:**
- Alle anderen Tests schlagen fehl, weil der Alias nicht gesetzt wird

## Code Flow Analysis

### When Namespace Needs Loading (WORKS ✅)

1. `process_require_spec` wird aufgerufen
2. `alias_sym` wird aus Vector extrahiert (Zeile 2230)
3. Namespace wird geladen (Zeile 2318-2422)
4. `orig_ns` wird gespeichert (Zeile 2348)
5. Namespace wird temporär gewechselt (Zeile 2365)
6. Namespace wird geladen
7. Namespace wird wiederhergestellt (Zeile 2369)
8. **Alias wird gesetzt** (Zeile 2407) ✅
9. Funktioniert korrekt!

### When Namespace Already Loaded (FAILS ❌)

1. `process_require_spec` wird aufgerufen
2. `alias_sym` wird aus Vector extrahiert (Zeile 2230) ✅
3. `existing = ns_find(ns_name)` findet Namespace (Zeile 2272)
4. `needs_loading = false` wird gesetzt (Zeile 2293)
5. Code in Zeile 2298-2317 wird ausgeführt
6. **Alias sollte hier gesetzt werden** (Zeile 2309) ❌
7. Aber `ns_get_alias` gibt NULL zurück

## Root Cause Hypothesis

Die Low-Level-Tests zeigen:
- ✅ `alias_sym` wird korrekt extrahiert
- ✅ `st->current_ns` ist korrekt
- ✅ `ns_set_alias` funktioniert

**Aber:** `native_require` setzt den Alias nicht, wenn `needs_loading = false` ist.

### Possible Causes

1. **`alias_sym` ist NULL**, wenn `needs_loading = false` ist
   - Aber: Low-Level-Test zeigt, dass `alias_sym` korrekt extrahiert wird
   - **Verdacht:** `alias_sym` wird möglicherweise zwischen Extraktion und Verwendung auf NULL gesetzt

2. **`st->current_ns` ist falsch**, wenn der Alias gesetzt wird
   - Aber: Low-Level-Test zeigt, dass `st->current_ns` korrekt ist
   - **Verdacht:** `st->current_ns` wird möglicherweise zwischen Prüfung und Verwendung geändert

3. **`ns_set_alias` wird nicht aufgerufen**
   - Code zeigt, dass `ns_set_alias` aufgerufen wird (Zeile 2309)
   - **Verdacht:** Die Bedingung `if (alias_sym && TAG(alias_sym) == CLJ_SYMBOL)` schlägt fehl

4. **`ns_set_alias` wird aufgerufen, aber der Alias wird nicht gespeichert**
   - Aber: Low-Level-Test zeigt, dass `ns_set_alias` funktioniert
   - **Verdacht:** Der Alias wird im falschen Namespace gesetzt

## Key Observation

**Working Test:** `test_namespace/require_with_alias`
- Verwendet `test.alias` (nicht vor-geladen)
- Namespace wird aus Datei geladen
- Alias wird korrekt gesetzt ✅

**Failing Tests:** Alle `hypothesis` und `lowlevel` Tests
- Verwenden `clojure.string` (vor-geladen)
- `needs_loading = false`
- Alias wird nicht gesetzt ❌

## Code Location

**File:** `src/builtins.c`
- **Function:** `process_require_spec` (Zeile 2169)
- **Problem Area:** Zeile 2298-2317 (wenn `needs_loading = false`)
- **Alias Extraction:** Zeile 2227-2234
- **Alias Setting:** Zeile 2306-2310

## Next Steps

1. **Debug:** Prüfen, ob `alias_sym` tatsächlich gesetzt ist, wenn Zeile 2306 erreicht wird
2. **Debug:** Prüfen, ob `st->current_ns` korrekt ist, wenn Zeile 2309 erreicht wird
3. **Debug:** Prüfen, ob `ns_set_alias` tatsächlich aufgerufen wird
4. **Fix:** Sicherstellen, dass `alias_sym` nicht NULL ist und korrekt verwendet wird







