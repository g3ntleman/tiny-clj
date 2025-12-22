# Analyse der fehlschlagenden Tests

## Zusammenfassung
- **Gesamt**: 94 Failures
- **Tests mit Exceptions**: Die meisten Failures sind durch "Unable to resolve symbol" Exceptions verursacht

## Cluster 1: Symbol-Auflösung in Funktionen/let-Blöcken

### 1.1 Verschachtelte let-Blöcke
- `test_let/let_nested`: Unable to resolve symbol: x
- Problem: Äußere let-Variablen werden in inneren let-Blöcken nicht gefunden

### 1.2 Lokale Funktionen in let-Blöcken
- `test_let/let_with_local_function_multiple_calls`: Unable to resolve symbol: step
- `test_let/let_recursive_function_multiple_calls`: Unable to resolve symbol: step
- `test_let/let_recursive_function_namespace_access`: Unable to resolve symbol: step
- Problem: Lokale Funktionen (step) werden nicht in verschachtelten Kontexten gefunden

### 1.3 Funktion-Parameter in verschachtelten Kontexten
- `test_sequences/filter_*`: Unable to resolve symbol: pred (mehrere Tests)
- `test_sequences/reduce_*`: Unable to resolve symbol: f (mehrere Tests)
- `test_sequences/rest_with_parameter_*`: Unable to resolve symbol: coll
- `test_require/string_*_after_require`: Unable to resolve symbol: s
- `test_qualified_symbol_resolution/*`: Unable to resolve symbol: separator
- Problem: Funktion-Parameter werden in verschachtelten let-Blöcken oder Funktionen nicht gefunden

## Cluster 2: clojure.core Initialisierung

### 2.1 Fehlende Funktionen in clojure.core
- `test_core_initialization/core_initialization_inc_loaded`: inc should be in clojure.core mappings
- `test_core_initialization/core_initialization_arithmetic_functions`: 7 functions missing (add sub mul div inc dec square)
- `test_core_initialization/clojure_core_loads_inc`: 'inc' not found in clojure.core mappings
- `test_core_initialization/clojure_core_loads_all_functions`: 7 functions missing
- `test_core_initialization/plus_available_during_fn_evaluation`: + should be in clojure.core mappings
- Problem: Funktionen werden nicht korrekt in clojure.core registriert oder nicht gefunden

### 2.2 Symbol-Interning-Probleme
- `test_symbol_interning/inc_symbol_interning_during_load`: Different symbol pointer
- `test_symbol_interning/inc_symbol_pointer_consistency`: inc not found in mappings after def
- `test_symbol_interning/map_get_with_interned_symbols`: Should retrieve value using interned symbol pointer
- Problem: Symbol-Interning führt zu unterschiedlichen Pointern, die nicht in Mappings gefunden werden

## Cluster 3: Namespace/def/defn Probleme

### 3.1 def/defn speichert nicht in Namespace
- `test_basics/def_function_isolated_problem`: Direct map_get should find test-fn after def
- `test_defn/defn_test_fn_evaluated`: 'test-fn' should be in namespace mappings after eval_defn
- `test_defn/defn_add_stored_in_namespace`: 'add' should be in namespace mappings after defn
- `test_core_initialization/def_stores_symbol_even_if_value_null`: test-var should be in mappings after def
- Problem: def/defn speichern Symbole nicht korrekt in Namespace-Mappings

## Cluster 4: Keyword-Auflösung

### 4.1 Keywords in Funktionen
- `test_basics/if_nil_in_function_regression`: Unable to resolve symbol: :else
- `test_keyword_evaluation/keyword_in_function_body`: Unable to resolve symbol: :done
- Problem: Keywords werden in Funktionen nicht korrekt behandelt

## Cluster 5: Qualified Symbol Resolution

### 5.1 Qualified Symbols mit Alias
- `test_qualified_symbol_resolution/resolve_qualified_symbol_with_alias`: Unable to resolve symbol: str/blank?
- `test_require/require_reverse_*`: Unable to resolve symbol: clojure.core/reverse (mehrere Tests)
- Problem: Qualified Symbols werden nicht korrekt aufgelöst, besonders mit Aliases

## Cluster 6: Metadata

### 6.1 Metadata nach Redefinition
- `test_meta/meta_qualified_symbol`: trim function should have metadata after redefinition
- Problem: Metadata geht nach Redefinition verloren

## Cluster 7: Map-Operationen

### 7.1 Map update
- `test_map/update_missing_key`: Fehler bei update mit fehlendem Key
- Problem: Map update funktioniert nicht korrekt bei fehlenden Keys

## Cluster 8: Filter/Reduce Funktionen

### 8.1 Filter/Reduce nicht verfügbar
- `test_sequences/filter_*`: Filter-Funktion nicht verfügbar (mehrere Tests)
- `test_sequences/reduce_*`: Reduce-Funktion nicht verfügbar (mehrere Tests)
- Problem: Filter und Reduce sind nicht in clojure.core verfügbar oder nicht korrekt implementiert

## Priorisierung

### Hoch (kritisch für Funktionalität):
1. **Cluster 1**: Symbol-Auflösung in verschachtelten Kontexten (let, Funktionen)
2. **Cluster 2**: clojure.core Initialisierung (grundlegende Funktionen fehlen)

### Mittel:
3. **Cluster 3**: def/defn Namespace-Speicherung
4. **Cluster 8**: Filter/Reduce Funktionen

### Niedrig:
5. **Cluster 4**: Keyword-Auflösung
6. **Cluster 5**: Qualified Symbol Resolution
7. **Cluster 6**: Metadata
8. **Cluster 7**: Map-Operationen




