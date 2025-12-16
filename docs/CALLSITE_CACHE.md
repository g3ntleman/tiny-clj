# Callsite Cache Optimization

**Datum**: 2025-12-XX  
**Status**: Implementiert

## Übersicht

Diese Optimierung implementiert Call-Site-Caches für Symbolauflösung, um wiederholte Auflösungen von Funktionssymbolen zu vermeiden. Dies reduziert den Overhead von `resolve_list_operator` bei rekursiven Funktionsaufrufen.

## Implementierte Mechanismen

### 1. AST-basierte Callsite-Caches

Jeder AST-Knoten kann einen `CljCallsiteCache` speichern, der folgende Informationen enthält:
- `symbol`: Das aufgelöste Symbol
- `resolved`: Der aufgelöste Wert (z.B. `CljFunction` oder `CljCFunc`)
- `epoch`: Epochennummer für Invalidation

**Dateien**:
- `src/ast.h`: Definition von `CljCallsiteCache`
- `src/ast.c`: Implementierung der Cache-Funktionen

### 2. Epochen-basierte Invalidation

Die Epochen-Mechanik zentralisiert die Invalidation aller Callsite-Caches:

- `g_runtime.resolve_cache_epoch`: Globale Epochennummer
- `ns_invalidate_resolve_cache()`: Erhöht die Epoche bei Namespace-Änderungen
- Callsite-Caches prüfen die Epoche bei jedem Zugriff

**Vorteile**:
- Kein AST-Walk nötig für Invalidation
- Automatische Invalidation aller Caches bei Namespace-Änderungen
- Thread-safe durch Epochenvergleich

**Dateien**:
- `src/namespace.c`: `ns_invalidate_resolve_cache()`
- `src/runtime.h`: `resolve_cache_epoch` Definition

### 3. Resolve-Cache (Global)

Zusätzlich zu den Callsite-Caches existiert ein globaler `resolve_cache`, der:
- Symbol → aufgelöster Wert Mappings speichert
- Bei Namespace-Lookups befüllt wird
- Bei `ns_invalidate_resolve_cache()` geleert wird

**Dateien**:
- `src/runtime.h`: `resolve_cache` Definition
- `src/eval.c`: `resolve_list_operator()` verwendet beide Caches

## Auflösungsreihenfolge

Die Symbolauflösung in `resolve_list_operator()` folgt dieser Reihenfolge:

1. **Call Frame Lookup** (Parameter)
   - Schnellste Methode für Funktionsparameter
   - Stack-basiert, keine Heap-Allokation

2. **Callsite-Cache** (wenn kein `resolve_stack`)
   - Prüft `ast_node_get_cached_resolution()`
   - Gültig, wenn Epoche übereinstimmt

3. **Globaler Resolve-Cache**
   - Prüft `g_runtime.resolve_cache`
   - Unterstützt qualified/unqualified Symbole

4. **Environment Stack** (wenn vorhanden)
   - Für Closures und `let`-Bindungen
   - Wird nicht gecacht (context-spezifisch)

5. **Namespace Lookup** (`eval_symbol`)
   - Fallback zu globaler Namespace-Auflösung
   - Befüllt beide Caches nach erfolgreicher Auflösung

## Wann wird gecacht?

### ✅ Wird gecacht:
- Namespace-Funktionen (z.B. `defn`-Funktionen)
- Native Funktionen (`CljCFunc`)
- Symbole ohne `resolve_stack` (keine Closures)

### ❌ Wird NICHT gecacht:
- Environment-Lookups (Closures, `let`-Bindungen)
- Dynamische Vars
- Symbole mit `resolve_stack`

## Performance-Verbesserungen

### Erwartete Verbesserungen:
- **Reduzierte Symbolauflösung**: Bei rekursiven Aufrufen wird das Symbol nur einmal aufgelöst
- **Weniger Namespace-Lookups**: Cached Werte werden direkt verwendet
- **Bessere Cache-Lokalität**: Callsite-Caches sind AST-nah

### Messungen:
- Benchmarks: `benchmarks/fibonacci.clj`, `benchmarks/let_performance.clj`
- Vergleich vorher/nachher erforderlich

## Code-Beispiele

### Callsite-Cache Verwendung

```c
// In resolve_list_operator():
bool allow_callsite_cache = call_node && op_sym && !resolve_stack && 
                            g_runtime.resolve_cache_epoch != 0;
if (allow_callsite_cache) {
    ID cached_call = ast_node_get_cached_resolution(call_node, op_sym, 
                                                     g_runtime.resolve_cache_epoch);
    if (cached_call) {
        return cached_call;  // Cache Hit!
    }
}

// ... Auflösung ...

// Nach erfolgreicher Auflösung:
if (call_node && !resolve_stack) {
    ast_node_update_callsite_cache(call_node, op_sym, resolved, 
                                    g_runtime.resolve_cache_epoch);
}
```

### Invalidation

```c
// Bei Namespace-Änderungen (def, defn, require):
void ns_invalidate_resolve_cache(void) {
    uint64_t next_epoch = g_runtime.resolve_cache_epoch + 1;
    if (next_epoch == 0) {
        next_epoch = 1;  // Overflow-Handling
    }
    g_runtime.resolve_cache_epoch = next_epoch;
    ASSIGN(g_runtime.resolve_cache, NULL);
}
```

## Wartung

### Neue Features hinzufügen:
1. Callsite-Cache wird automatisch bei `resolve_list_operator()` verwendet
2. Invalidation erfolgt automatisch bei `ns_define()` / `ns_define_refer()`

### Debugging:
- `g_runtime.resolve_cache_epoch`: Aktuelle Epoche prüfen
- `ast_node_get_callsite_cache()`: Cache-Inhalt eines AST-Knotens prüfen

## Weitere Optimierungen

### Mögliche zukünftige Verbesserungen:
1. **Direkte Funktionspointer**: Bei `defn` ohne Closures direkt im AST speichern
2. **HashMap-Implementierung**: Schnellere Cache-Lookups
3. **Tail-Call-Optimierung**: Bereits teilweise implementiert

## Referenzen

- Plan: `.cursor/plans/callsite_optimization_strategy_8f2b169a.plan.md`
- Baseline: `docs/PERFORMANCE_BASELINE.md`
- Tests: `src/tests/test_namespace.c` (test_resolve_list_operator_uses_cache)



