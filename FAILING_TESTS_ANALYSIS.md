# Analyse der fehlgeschlagenen Tests

## Übersicht
**Anzahl fehlgeschlagener Tests:** 19

## Kategorisierung

### 1. Symbol-Interning-Probleme (7 Tests)
**Problem:** Symbole werden nicht in Namespace-Mappings gefunden, obwohl sie definiert wurden.

**Betroffene Tests:**
- `test_symbol_interning/inc_symbol_pointer_consistency`: "inc not found in mappings after def"
- `test_symbol_interning/map_get_with_interned_symbols`: "Should retrieve value using interned symbol pointer"
- `test_core_initialization/def_inc_evaluation_during_load`: "'inc' not found in mappings after def evaluation"
- `test_core_initialization/def_stores_symbol_even_if_value_null`: "test-var should be in mappings after def"
- `test_defn/defn_test_fn_evaluated`: "'test-fn' should be in namespace mappings after eval_defn"
- `test_defn/defn_add_stored_in_namespace`: "'add' should be in namespace mappings after defn"
- `test_basics/def_function_isolated_problem`: "Direct map_get should find test-fn after def"

**Mögliche Ursachen:**
- Symbol-Pointer-Inkonsistenzen: Symbole werden mit unterschiedlichen Pointern gespeichert/gesucht
- `ns_define` verwendet qualifizierte Symbole, aber Lookup verwendet unqualifizierte Symbole (oder umgekehrt)
- `clojure.core` Symbole werden unqualifiziert gespeichert, aber mit qualifizierten Symbolen gesucht

**Relevante Code-Stellen:**
- `src/namespace.c`: `ns_define`, `ns_resolve`
- `src/symbol.c`: `intern_symbol`, `intern_symbol_global`

### 2. Qualified Symbol-Probleme (3 Tests)
**Problem:** Qualified Symbols werden nicht korrekt geparst oder aufgelöst.

**Betroffene Tests:**
- `test_namespace/qualified_symbol_parsing_moved`: "Expected Non-NULL"
- `test_qualified_symbol_resolution/qualified_symbol_parsing`: "Expected Non-NULL"
- `test_qualified_symbol_resolution/resolve_clojure_core_reverse`: "Expected Non-NULL"

**Mögliche Ursachen:**
- Parser erkennt qualified symbols nicht korrekt
- `ns_resolve` kann qualified symbols nicht auflösen
- `clojure.core` Symbole werden unqualifiziert gespeichert, aber mit qualifizierten Symbolen gesucht

**Relevante Code-Stellen:**
- `src/parser.c`: Qualified Symbol Parsing
- `src/namespace.c`: `ns_resolve` für qualified symbols

### 3. Symbol Clojure-Kompatibilität (4 Tests)
**Problem:** Symbol-Repräsentation entspricht nicht Clojure-Semantik.

**Betroffene Tests:**
- `test_symbol_clojure_compat/symbol_creation_without_namespace`: "Expected 2 Was 28526"
- `test_symbol_clojure_compat/symbol_ns_is_symbol_not_namespace`: "Expected 2 Was 31085"
- `test_symbol_clojure_compat/symbol_string_representation`: "String representation should contain namespace and name"
- `test_symbol_clojure_compat/namespace_lookup_from_symbol`: "Should find namespace via symbol's namespace name"

**Mögliche Ursachen:**
- `CljSymbol` Struktur entspricht nicht Clojure-Semantik
- `ns_name` Feld wird nicht korrekt gesetzt/abgefragt
- String-Repräsentation von Symbolen ist falsch

**Relevante Code-Stellen:**
- `src/symbol.c`: Symbol-Erstellung und -Repräsentation
- `src/symbol.h`: `CljSymbol` Struktur

### 4. Environment/Function Parameter-Probleme (1 Test)
**Problem:** Funktionsparameter werden nicht korrekt im Environment aufgelöst.

**Betroffene Tests:**
- `test_sequences/reduce_with_identity_function`: "Unable to resolve symbol: f in this context"

**Mögliche Ursachen:**
- `reduce` Funktion hat Parameter `f` und `coll`, aber `f` wird nicht im Environment gefunden
- `env_extend_stack` erstellt Environment nicht korrekt
- `resolve_symbol_in_env` sucht nicht korrekt im Environment-Stack

**Relevante Code-Stellen:**
- `src/environment.c`: `env_extend_stack`
- `src/function_call.c`: `resolve_symbol_in_env`, `eval_body_with_params`

### 5. Namespace-Struktur-Probleme (1 Test)
**Problem:** Namespace-Struktur hat unerwartete Felder.

**Betroffene Tests:**
- `test_namespace/namespace_no_next_field`: "Expected Non-NULL"

**Mögliche Ursachen:**
- Test erwartet, dass Namespace kein `next` Feld hat (aber vielleicht hat es eins?)
- Struktur-Definition stimmt nicht mit Test-Erwartungen überein

**Relevante Code-Stellen:**
- `src/namespace.h`: `CljNamespace` Struktur

### 6. Map/Update-Probleme (1 Test)
**Problem:** `update` Funktion funktioniert nicht korrekt mit fehlenden Keys.

**Betroffene Tests:**
- `test_map/update_missing_key`: Fehlerzeichen (möglicherweise Encoding-Problem)

**Mögliche Ursachen:**
- `update` Funktion behandelt fehlende Keys nicht korrekt
- Exception wird nicht korrekt behandelt

**Relevante Code-Stellen:**
- `src/builtins.c`: `update` Funktion

### 7. Metadata-Probleme (1 Test)
**Problem:** Metadata wird nicht korrekt auf qualifizierte Symbole angewendet.

**Betroffene Tests:**
- `test_meta/meta_qualified_symbol`: "trim function should have metadata after redefinition"

**Mögliche Ursachen:**
- Metadata wird nicht korrekt auf qualifizierte Symbole gesetzt/abgefragt
- `meta_get` kann qualifizierte Symbole nicht auflösen

**Relevante Code-Stellen:**
- `src/meta.c`: `meta_get`, Metadata-Verwaltung
- `src/namespace.c`: `ns_resolve` für qualifizierte Symbole

## Priorisierung

### Hoch (kritisch für Funktionalität)
1. **Symbol-Interning-Probleme** (7 Tests) - Betrifft grundlegende Funktionalität
2. **Environment/Function Parameter-Probleme** (1 Test) - Betrifft Funktionsaufrufe

### Mittel (wichtig für Kompatibilität)
3. **Qualified Symbol-Probleme** (3 Tests) - Betrifft Namespace-Auflösung
4. **Symbol Clojure-Kompatibilität** (4 Tests) - Betrifft Clojure-Kompatibilität

### Niedrig (weniger kritisch)
5. **Metadata-Probleme** (1 Test)
6. **Map/Update-Probleme** (1 Test)
7. **Namespace-Struktur-Probleme** (1 Test)

## Nächste Schritte

1. **Symbol-Interning analysieren**: Prüfen, ob `ns_define` und `ns_resolve` konsistente Symbol-Pointer verwenden
2. **Environment-Stack prüfen**: Verifizieren, dass `env_extend_stack` korrekt funktioniert
3. **Qualified Symbols testen**: Prüfen, ob qualified symbols korrekt geparst und aufgelöst werden








