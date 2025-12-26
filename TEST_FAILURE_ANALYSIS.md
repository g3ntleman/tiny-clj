# Analyse der fehlschlagenden Tests

## Übersicht
- **Gesamtanzahl fehlschlagender Tests**: 21
- **Kategorien**: 6 Hauptcluster

---

## Cluster 1: Symbol-Pointer-Inkonsistenzen (3 Tests)

### Problem
Symbole werden mit unterschiedlichen Pointern gespeichert und gesucht, was zu Lookup-Fehlern führt.

### Betroffene Tests:
1. `test_symbol_interning/inc_symbol_interning_during_load`
   - Fehler: "Found 'inc' in mappings but with different symbol pointer! Stored: 0x603000013750, Lookup: 0x6030000134e0"
   
2. `test_symbol_interning/inc_symbol_pointer_consistency`
   - Fehler: "inc not found in mappings after def. Form symbol: 0x6030000134e0, Interned symbol: 0x6030000134e0, Equal: 1"
   
3. `test_symbol_interning/map_get_with_interned_symbols`
   - Fehler: "Should retrieve value using interned symbol pointer"

### Mögliche Ursachen:
- **`ns_define()` qualifiziert Symbole**, erstellt dabei neue Symbol-Instanzen
- **`intern_symbol()` erstellt möglicherweise neue Instanzen** statt bestehende zu verwenden
- **Map-Lookup verwendet Pointer-Vergleich**, nicht strukturelle Gleichheit
- **COW (Copy-on-Write) bei Maps** könnte zu Pointer-Inkonsistenzen führen

### Relevante Code-Stellen:
- `src/namespace.c:876-910` - `ns_define()` qualifiziert Symbole
- `src/function_call.c:1852-1860` - `eval_def()` qualifiziert Symbole vor `ns_define()`
- `src/symbol.c` - `intern_symbol()` Implementierung

---

## Cluster 2: Namespace-Mappings werden nicht gefüllt (9 Tests)

### Problem
Funktionen werden nicht korrekt in Namespace-Mappings gespeichert, insbesondere:
- Builtin-Funktionen fehlen in `clojure.core`
- `def`/`defn` speichern nicht in Mappings
- Lookup findet Funktionen nicht, obwohl sie existieren

### Betroffene Tests:
1. `test_core_initialization/core_initialization_inc_loaded`
   - Fehler: "inc should be in clojure.core mappings after initialization"
   
2. `test_core_initialization/core_initialization_arithmetic_functions`
   - Fehler: "7 arithmetic functions missing from clojure.core after initialization: add sub mul div inc dec square"
   
3. `test_core_initialization/core_initialization_plus_available`
   - Fehler: "+ should be in clojure.core mappings (registered by register_builtins)"
   
4. `test_core_initialization/clojure_core_loads_inc`
   - Fehler: "'inc' not found in clojure.core mappings after load. Symbol count: 96, first: +. inc_sym pointer: 0x6030000134e0. Found inc by name: inc (ptr: 0x603000013750). Pointers equal: 0"
   
5. `test_core_initialization/clojure_core_loads_all_functions`
   - Fehler: "7 functions missing from clojure.core: add sub mul div inc dec square"
   
6. `test_core_initialization/def_inc_evaluation_during_load`
   - Fehler: "'inc' not found in mappings after def evaluation (but 1 other symbols exist, first: inc)"
   
7. `test_core_initialization/def_stores_symbol_even_if_value_null`
   - Fehler: "test-var should be in mappings after def"
   
8. `test_defn/defn_test_fn_evaluated`
   - Fehler: "'test-fn' should be in namespace mappings after eval_defn"
   
9. `test_defn/defn_add_stored_in_namespace`
   - Fehler: "'add' should be in namespace mappings after defn"

### Mögliche Ursachen:
- **`register_builtin_in_core()` verwendet unqualifizierte Symbole**, aber Lookup erwartet qualifizierte
- **`ns_define()` wird nicht aufgerufen** oder schlägt still fehl
- **Qualifizierung schlägt fehl** (z.B. `intern_symbol()` gibt NULL zurück)
- **COW-Maps werden nicht korrekt aktualisiert** (ASSIGN fehlt)
- **`load_clojure_core()` überschreibt Mappings** nach `register_builtins()`

### Relevante Code-Stellen:
- `src/builtins.c:3874-3965` - `register_builtin_in_core()` verwendet `intern_symbol_global()`
- `src/namespace.c:876-910` - `ns_define()` qualifiziert Symbole
- `src/clojure_core.c` - `load_clojure_core()` könnte Mappings überschreiben

---

## Cluster 3: Symbol-Auflösung in Funktionen (4 Tests)

### Problem
Symbole können in Funktionskontexten nicht aufgelöst werden, obwohl sie verfügbar sein sollten.

### Betroffene Tests:
1. `test_basics/if_nil_in_function_regression`
   - Fehler: "if in function should not throw exception, got: Unable to resolve symbol: test-nil in this context"
   
2. `test_sequences/reduce_with_identity_function`
   - Fehler: "reduce threw exception: RuntimeException - Unable to resolve symbol: f in this context"
   
3. `test_keyword_evaluation/keyword_in_function_body`
   - Fehler: "Unable to resolve symbol: :done in this context"
   
4. `test_keyword_evaluation/keyword_in_nested_function_call`
   - Fehler: "Unable to resolve symbol: :active in this context"

### Mögliche Ursachen:
- **Keywords werden nicht als Keywords erkannt** in Funktionskontexten
- **Environment-Stack wird nicht korrekt durchsucht**
- **Namespace-Mappings werden nicht in Funktions-Umgebung kopiert**
- **`resolve_symbol_in_env()` durchsucht nicht alle Ebenen**

### Relevante Code-Stellen:
- `src/function_call.c:2128-2235` - `eval_symbol()` prüft Keywords
- `src/function_call.c:579-616` - `resolve_symbol_in_env()` Logik
- `src/function_call.c:3034-3057` - Funktions-Umgebung wird erstellt

---

## Cluster 4: Keyword-Auflösung (2 Tests)

### Problem
Keywords werden als nicht-auflösbare Symbole behandelt, obwohl sie sich selbst evaluieren sollten.

### Betroffene Tests:
1. `test_keyword_evaluation/keyword_in_function_body`
   - Fehler: "Unable to resolve symbol: :done in this context"
   
2. `test_keyword_evaluation/keyword_in_nested_function_call`
   - Fehler: "Unable to resolve symbol: :active in this context"

### Mögliche Ursachen:
- **`IS_KEYWORD()` Makro funktioniert nicht korrekt** in allen Kontexten
- **Keywords werden vor Namespace-Lookup nicht geprüft**
- **Parser erstellt Keywords nicht korrekt** (z.B. `::keyword` Auto-Qualifizierung fehlt)

### Relevante Code-Stellen:
- `src/symbol.h` - `IS_KEYWORD()` Definition
- `src/function_call.c:2138-2141` - Keyword-Check in `eval_symbol()`
- `src/parser.c` - Keyword-Parsing

---

## Cluster 5: Symbol-Qualifizierung und Clojure-Kompatibilität (4 Tests)

### Problem
Symbol-Repräsentation und Namespace-Lookup entsprechen nicht Clojure-Semantik.

### Betroffene Tests:
1. `test_symbol_clojure_compat/symbol_creation_without_namespace`
   - Fehler: "Expected 2 Was 28526"
   
2. `test_symbol_clojure_compat/symbol_ns_is_symbol_not_namespace`
   - Fehler: "Expected 2 Was 31085"
   
3. `test_symbol_clojure_compat/symbol_string_representation`
   - Fehler: "String representation should contain namespace and name"
   
4. `test_symbol_clojure_compat/namespace_lookup_from_symbol`
   - Fehler: "Expected  Was . Should find namespace via symbol's namespace name"

### Mögliche Ursachen:
- **Symbol-Repräsentation entspricht nicht Clojure-Format**
- **`symbol->ns_name` ist nicht korrekt gesetzt**
- **Namespace-Lookup verwendet falsche Felder**

### Relevante Code-Stellen:
- `src/symbol.c` - Symbol-Erstellung und -Repräsentation
- `src/namespace.c:248-362` - `ns_resolve()` Logik

---

## Cluster 6: Verschiedene Probleme (3 Tests)

### Betroffene Tests:
1. `test_basics/def_function_isolated_problem`
   - Fehler: "Direct map_get should find test-fn after def"
   - **Ursache**: Wie Cluster 2 (Namespace-Mappings)
   
2. `test_namespace/namespace_no_next_field`
   - Fehler: "Expected Non-NULL"
   - **Ursache**: Unbekannt, benötigt Test-Code-Analyse
   
3. `test_map/update_missing_key`
   - Fehler: "P\x1E\x13" (binäre Daten)
   - **Ursache**: Unbekannt, möglicherweise Encoding-Problem
   
4. `test_require/require_blank_resolution`
   - Fehler: "blank? should be resolvable from clojure.string namespace"
   - **Ursache**: `require` funktioniert nicht korrekt
   
5. `test_meta/metadata_transferred_from_defn_to_function`
   - Fehler: "function should be in namespace"
   - **Ursache**: Wie Cluster 2 (Namespace-Mappings)
   
6. `test_meta/meta_qualified_symbol`
   - Fehler: "trim function should have metadata after redefinition"
   - **Ursache**: Metadaten werden nicht korrekt übertragen

---

## Priorisierte Lösungsansätze

### Priorität 1: Symbol-Pointer-Inkonsistenzen (Cluster 1)
**Kritisch**, da dies die Grundlage für alle anderen Probleme ist.

**Lösungsansätze:**
1. **Symbol-Interning konsistent machen**: Sicherstellen, dass `intern_symbol()` immer denselben Pointer für denselben Namen zurückgibt
2. **Map-Lookup anpassen**: Strukturelle Gleichheit statt Pointer-Vergleich verwenden
3. **Qualifizierung optimieren**: Qualifizierte Symbole nur einmal erstellen und wiederverwenden

### Priorität 2: Namespace-Mappings (Cluster 2)
**Kritisch**, da dies die meisten Fehler verursacht.

**Lösungsansätze:**
1. **`register_builtin_in_core()` anpassen**: Qualifizierte Symbole verwenden
2. **`ns_define()` Fehlerbehandlung**: Prüfen, ob Qualifizierung erfolgreich war
3. **COW-Map-Updates**: Sicherstellen, dass `ASSIGN()` korrekt verwendet wird
4. **Reihenfolge prüfen**: `register_builtins()` vor/nach `load_clojure_core()`?

### Priorität 3: Keyword-Auflösung (Cluster 4)
**Hoch**, da Keywords grundlegende Clojure-Funktionalität sind.

**Lösungsansätze:**
1. **Keyword-Check früher**: In `resolve_symbol_in_env()` vor Namespace-Lookup
2. **`IS_KEYWORD()` prüfen**: Funktioniert es in allen Kontexten?
3. **Parser anpassen**: `::keyword` Auto-Qualifizierung implementieren (siehe `KEYWORD_SEMANTICS_ANALYSIS.md`)

### Priorität 4: Symbol-Auflösung in Funktionen (Cluster 3)
**Mittel**, hängt von Cluster 2 ab.

**Lösungsansätze:**
1. **Environment-Stack prüfen**: Werden alle Ebenen durchsucht?
2. **Namespace-Mappings in Funktions-Umgebung**: Werden sie korrekt kopiert?

### Priorität 5: Clojure-Kompatibilität (Cluster 5)
**Niedrig**, betrifft hauptsächlich Edge-Cases.

**Lösungsansätze:**
1. **Symbol-Repräsentation anpassen**: Clojure-Format implementieren
2. **Namespace-Lookup**: Korrekte Felder verwenden

---

## Empfohlene Vorgehensweise

1. **Zuerst Cluster 1 analysieren**: Symbol-Pointer-Inkonsistenzen verstehen
2. **Dann Cluster 2 beheben**: Namespace-Mappings korrekt implementieren
3. **Cluster 4 parallel**: Keyword-Auflösung verbessern
4. **Cluster 3 sollte sich dann automatisch lösen**: Wenn Mappings korrekt sind
5. **Cluster 5 und 6**: Nach Bedarf






