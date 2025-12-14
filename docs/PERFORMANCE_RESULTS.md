# Performance Results - Callsite Cache Optimization

**Datum**: 2025-12-XX  
**Optimierung**: Callsite Cache für Symbolauflösung

## Implementierte Optimierungen

### 1. AST-basierte Callsite-Caches
- Jeder AST-Knoten kann einen `CljCallsiteCache` speichern
- Speichert aufgelöste Symbole mit Epochennummer
- Automatische Invalidation bei Namespace-Änderungen

### 2. Epochen-basierte Invalidation
- Zentrale Epochensteuerung über `g_runtime.resolve_cache_epoch`
- Alle Callsite-Caches werden automatisch invalidiert
- Kein AST-Walk nötig für Invalidation

### 3. Optimierte Auflösungsreihenfolge
1. Call Frame Lookup (Parameter) - schnellste Methode
2. Callsite-Cache (wenn kein resolve_stack)
3. Globaler Resolve-Cache
4. Environment Stack (Closures)
5. Namespace Lookup (Fallback)

## Benchmark-Ergebnisse

### Baseline-Messungen (Release-Build)

**Fibonacci Benchmark - Naive rekursive Implementierung** (`benchmarks/fibonacci.clj`):
- **Clojure (JIT-optimized)**: 2.81 msecs für `fib(20)`
- **tiny-clj (Interpreter, Release-Build)**: 130.25 msecs für `fib(20)`
- **Ratio**: **46.3× langsamer** als Clojure

**Hinweise**:
- Messung mit Release-Build (`-O3 -DNDEBUG`)
- Naive rekursive Implementierung (exponentielles Wachstum)
- Bei `fib(47)` wäre der Unterschied noch dramatischer (Clojure: ~27 Sekunden, tiny-clj: Timeout nach 60 Sekunden)

**Let Performance Benchmark** (`benchmarks/let_performance.clj`):
- **1000 Iterationen**: 11.38 msecs
- **Pro Iteration**: ~0.0114 msecs

### Benchmark-Ausführung

```bash
# Fibonacci Benchmark
cd benchmarks
../build/tiny-clj-repl < fibonacci.clj

# Let Performance Benchmark  
../build/tiny-clj-repl < let_performance.clj
```

## Vergleich Clojure vs. tiny-clj

| Benchmark | Clojure | tiny-clj | Ratio | Status |
|-----------|---------|----------|-------|--------|
| fibonacci.clj (fib 20) | 2.81 msecs | 130.25 msecs | **46.3× langsamer** | ✅ Baseline gemessen |
| let_performance.clj (1000x) | N/A | 11.38 msecs | N/A | ✅ Gemessen |

**Build-Konfiguration**:
- **tiny-clj**: Release-Build (`-O3 -DNDEBUG`)
- **Clojure**: JIT-optimized (Standard)

**Wichtige Erkenntnisse**:
- 46.3× langsamer ist dramatisch, aber erwartbar für einen Interpreter vs. JIT
- Callsite-Cache-Optimierung hilft, kann aber Interpreter-Overhead nicht vollständig kompensieren
- Exponentielles Wachstum bei rekursiven Algorithmen verstärkt den Performance-Unterschied

## Technische Details

### Callsite-Cache Mechanismus

```c
// Cache-Lookup in resolve_list_operator():
bool allow_callsite_cache = call_node && op_sym && !resolve_stack && 
                            g_runtime.resolve_cache_epoch != 0;
if (allow_callsite_cache) {
    ID cached_call = ast_node_get_cached_resolution(call_node, op_sym, 
                                                     g_runtime.resolve_cache_epoch);
    if (cached_call) {
        return cached_call;  // Cache Hit - keine Auflösung nötig!
    }
}
```

### Invalidation bei Namespace-Änderungen

```c
void ns_invalidate_resolve_cache(void) {
    uint64_t next_epoch = g_runtime.resolve_cache_epoch + 1;
    if (next_epoch == 0) {
        next_epoch = 1;  // Overflow-Handling
    }
    g_runtime.resolve_cache_epoch = next_epoch;
    ASSIGN(g_runtime.resolve_cache, NULL);
}
```

## Fehlende Features

Die Performance-Messungen zeigen, dass tiny-clj noch deutlich langsamer ist als Clojure. Zusätzlich zu den Callsite-Cache-Optimierungen fehlen noch wichtige Features, die jedoch die Performance **verschlechtern** können:

1. **Verschiedene Aritäten**: Funktionen mit unterschiedlichen Parameteranzahlen (z.B. `+` mit 1, 2, oder mehr Argumenten)
   - **Performance-Impact**: Laufzeit-Prüfung der Argumentanzahl bei jedem Aufruf
   - **Overhead**: Arity-Dispatch-Tabellen oder if/switch-Statements

2. **Varargs**: Unterstützung für variadische Funktionen (`& args`)
   - **Performance-Impact**: Argumente müssen in Sequenz/Liste gesammelt werden
   - **Overhead**: Zusätzliche Allokationen für varargs-Argumente

3. **Weitere Optimierungen** (können Performance verbessern):
   - Memoization für rekursive Funktionen
   - Tail-Call-Optimierung (teilweise vorhanden via `recur`)
   - Bytecode-Compiler statt reiner Interpreter
   - JIT-ähnliche Optimierungen für Hot-Paths

## Nächste Schritte

1. ✅ Callsite-Cache implementiert
2. ✅ Epochen-Mechanik implementiert
3. ✅ Dokumentation erstellt
4. ✅ Baseline-Performance-Messungen durchgeführt
5. ✅ Ergebnisse dokumentiert
6. ⏳ Implementierung fehlender Features (verschiedene Aritäten, varargs)
   - Siehe: `docs/ARITY_VARARGS_IMPLEMENTATION.md` für Vorschläge
7. ⏳ Weitere Performance-Optimierungen

## Referenzen

- Plan: `.cursor/plans/callsite_optimization_strategy_8f2b169a.plan.md`
- Dokumentation: `docs/CALLSITE_CACHE.md`
- Baseline: `docs/PERFORMANCE_BASELINE.md`
- Benchmarks: `benchmarks/fibonacci.clj`, `benchmarks/let_performance.clj`
