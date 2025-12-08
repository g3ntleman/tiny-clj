# Übersicht über fehlgeschlagene Tests - Geclustert nach Fehlertypen

## Zusammenfassung
- **Gesamtanzahl fehlgeschlagener Tests:** 22 (reduziert von 28)
- **Letzte Aktualisierung:** 2025-01-XX
- **Status Cluster 2:** ✅ 4 Tests behoben (string_capitalize, string_ends_with in test_string und test_require)

## Statistik der fehlgeschlagenen Symbole:
- `clojure.core/reverse`: 22 Tests
- `join`: 4 Tests
- `index-of`: 4 Tests

---

## Cluster 1: Qualified Symbol Resolution (clojure.core/reverse)
**Anzahl:** 22 Tests
**Fehlertyp:** `Unable to resolve symbol: clojure.core/reverse in this context`
**Fehlerstelle:** `src/function_call.c:2237` (in `eval_symbol`)

### Betroffene Tests:
1. `test_string/string_escape`
2. `test_require/require_reverse_conflict_clojure_core`
3. `test_require/require_reverse_in_let_after_require`
4. `test_require/require_reverse_in_recursive_function`
5. `test_require/require_both_reverse_functions`
6. `test_qualified_symbol_resolution/resolve_clojure_core_reverse`
7. `test_qualified_symbol_resolution/eval_clojure_core_reverse_expression`
8. `test_qualified_symbol_resolution/resolve_clojure_core_reverse_in_let`
9. `test_qualified_symbol_resolution/resolve_clojure_core_reverse_in_function`
10. `test_qualified_symbol_resolution/resolve_clojure_core_reverse_in_local_fn`
11. `test_qualified_symbol_resolution/resolve_clojure_core_reverse_in_let_with_fn`
... (weitere 11 Tests)

### Problem:
Qualified Symbols wie `clojure.core/reverse` werden nicht korrekt aufgelöst. Die Auflösung schlägt in `eval_symbol` bei Zeile 2237 fehl, wenn das Symbol nicht im Namespace gefunden wird.

### Code-Stelle:
```c
// src/function_call.c:2230-2242
// Qualified symbol not found in target namespace
throw_exception_formatted(..., "Unable to resolve symbol: %s in this context", qualified_name);
```

### Mögliche Ursachen:
- `ns_resolve` kann qualified symbols nicht auflösen
- Namespace-Mappings enthalten qualified symbols nicht korrekt
- Qualified Symbol Parsing funktioniert nicht korrekt
- `resolve_list_operator` behandelt qualified symbols nicht

### Lösungshinweise:
- Prüfen, ob `ns_resolve` qualified symbols erkennt
- Prüfen, ob Namespace-Mappings qualified symbols als Keys verwenden
- Prüfen, ob `resolve_list_operator` qualified symbols vor der Namespace-Auflösung behandelt

---

## Cluster 2: Parameter Resolution in String Functions ✅ BEHOBEN
**Anzahl:** 4 Tests behoben von ursprünglich 8
**Fehlertyp:** `Unable to resolve symbol: <parameter> in this context`
**Fehlerstelle:** `src/function_call.c:242` (in `throw_unresolved_symbol_exception`)

### Behobene Tests (4):
1. ✅ `test_string/string_capitalize` - Parameter: `s`
2. ✅ `test_string/string_ends_with` - Parameter: `s-len`
3. ✅ `test_require/string_capitalize_after_require` - Parameter: `s`
4. ✅ `test_require/string_ends_with_after_require` - Parameter: `s-len`

### Lösung:
- `eval_comparison_dispatch` und `eval_numeric_comparison` wurden erweitert, um Context zu akzeptieren
- `eval_seq` wurde erweitert, um Context zu akzeptieren
- `EVAL_TWO_ARGS_WITH_CONTEXT` Makro hinzugefügt
- Alle Aufrufe aktualisiert, um Context weiterzuleiten

### Verbleibende Tests (0):
- Alle Parameter-Resolution-Tests sind behoben!

---

## Cluster 3: Unqualified Symbol Resolution (join, index-of)
**Anzahl:** 8 Tests
**Fehlertyp:** `Unable to resolve symbol: <symbol> in this context`
**Fehlerstelle:** `src/function_call.c:2273` (in `eval_symbol`)

### Betroffene Tests:
1. `test_string/string_includes` - Symbol: `index-of`
2. `test_require/string_includes_after_require` - Symbol: `index-of`
3. `test_qualified_symbol_resolution/eval_clojure_string_join_expression` - Symbol: `join`
4. `test_qualified_symbol_resolution/resolve_clojure_string_join_in_local_fn` - Symbol: `join`
... (weitere 4 Tests)

### Problem:
Unqualified Symbole wie `join` und `index-of` werden nicht korrekt aufgelöst. Die Auflösung schlägt in `eval_symbol` bei Zeile 2273 fehl.

### Code-Stelle:
```c
// src/function_call.c:2271-2274
// Symbol not found
const char *cname = symbol->cname ? symbol->cname : "unknown";
throw_exception_formatted(..., "Unable to resolve symbol: %s in this context", cname);
```

### Mögliche Ursachen:
- `ns_resolve` findet die Symbole nicht
- Namespace-Mappings enthalten die Symbole nicht
- Symbole sind nicht im aktuellen Namespace registriert
- `require` für `clojure.string` funktioniert nicht korrekt

### Lösungshinweise:
- Prüfen, ob `clojure.string` Namespace korrekt geladen wird
- Prüfen, ob `require` die Namespace-Mappings korrekt erstellt
- Prüfen, ob `index-of` und `join` in den Namespace-Mappings vorhanden sind

---

## Priorisierung

### Hoch (kritisch - betrifft viele Tests):
1. **Cluster 1:** Qualified Symbol Resolution (`clojure.core/reverse`) - 22 Tests
   - Betrifft viele Tests in `test_qualified_symbol_resolution` und `test_require`
   - Fehlerstelle: `eval_symbol` Zeile 2237

### Mittel:
2. **Cluster 3:** Unqualified Symbol Resolution (`join`, `index-of`) - 8 Tests
   - Betrifft `clojure.string` Namespace
   - Fehlerstelle: `eval_symbol` Zeile 2273

---

## Empfohlene Lösungsansätze

### Für Cluster 1 (Qualified Symbols - clojure.core/reverse):
1. Prüfen, ob `ns_resolve` qualified symbols korrekt behandelt
2. Prüfen, ob `resolve_list_operator` qualified symbols erkennt und vor der Namespace-Auflösung behandelt
3. Prüfen, ob Namespace-Mappings qualified symbols als Keys enthalten
4. Prüfen, ob `eval_symbol` qualified symbols korrekt parst und auflöst

### Für Cluster 3 (Unqualified Symbols - join, index-of):
1. Prüfen, ob `clojure.string` Namespace korrekt geladen wird
2. Prüfen, ob `require` die Namespace-Mappings korrekt erstellt
3. Prüfen, ob `index-of` und `join` in den Namespace-Mappings vorhanden sind
4. Prüfen, ob `ns_resolve` diese Symbole findet

---

## Code-Stellen für weitere Analyse

### Zeile 2237 (Qualified Symbol Resolution):
```c
// src/function_call.c:2230-2242
// In eval_symbol, wenn qualified symbol nicht gefunden wird
throw_exception_formatted(..., "Unable to resolve symbol: %s in this context", qualified_name);
```

### Zeile 2273 (Unqualified Symbol Resolution):
```c
// src/function_call.c:2271-2274
// In eval_symbol, wenn symbol nicht gefunden wird
throw_exception_formatted(..., "Unable to resolve symbol: %s in this context", cname);
```

---

## Nächste Schritte

1. **Qualified Symbol Resolution analysieren (Cluster 1):**
   - Prüfen, wie `ns_resolve` qualified symbols behandelt
   - Prüfen, ob `resolve_list_operator` qualified symbols erkennt
   - Prüfen, ob Namespace-Mappings qualified symbols enthalten

2. **Unqualified Symbol Resolution analysieren (Cluster 3):**
   - Prüfen, ob `clojure.string` Namespace korrekt geladen wird
   - Prüfen, ob `require` die Mappings korrekt erstellt
   - Prüfen, ob `index-of` und `join` in den Namespace-Mappings vorhanden sind
