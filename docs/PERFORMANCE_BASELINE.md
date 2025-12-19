# Performance Baseline - Evaluator Optimierung

**Datum**: 2025-12-05  
**Benchmark**: Naive recursive Fibonacci `fib(20)`, 5 Iterationen  
**Ziel**: ≤20× langsamer als Clojure (aktuell: 2995.79×)

## Benchmark-Ergebnisse

### Clojure (JIT-optimized)
- **Laufzeit**: 4.076833ms (5 Iterationen von `fib(20)`)
- **Pro Iteration**: ~0.815ms

### tiny-clj (Interpreter)
- **Laufzeit**: 12213.36ms (5 Iterationen von `fib(20)`)
- **Pro Iteration**: ~2442.67ms
- **Ratio vs. Clojure**: **2995.79× langsamer**

### Analyse
- Ratio ist deutlich über dem Ziel von 20×
- **Geschätzter Debug-Overhead: 50%** (Memory-Profiler, Assertions, Debug-Build)
- **Geschätzte "echte" Performance ohne Debug**: ~1497.9× langsamer (50% von 2995.79×)
- Mögliche Ursachen für hohen Overhead:
  - Debug-Build mit Profiling-Overhead
  - Memory-Profiler aktiv (tracks all allocations/deallocations)
  - Assertions (`CLJ_ASSERT`) in Hotpaths
  - Interpreter-Overhead (erwartet, aber optimierbar)

## Profiling-Hotspots (Sample-Analyse)

**Sample-Datei**: `sample_baseline_20251205_230957.txt`  
**Sampling-Dauer**: 30 Sekunden  
**Methode**: macOS `sample` Tool

### Top-10 Hotspots (nach Stack-Top)

| Rang | Funktion | Aufrufe | Datei |
|------|----------|---------|-------|
| 1 | `TAG` | 3324 | `object.h:85` |
| 2 | `find_symbol` | 2319 | `symbol.c:451` |
| 3 | `assert_type` | 1516 | `object.h:190-192` |
| 4 | `_platform_strcmp` | 1132 | `libsystem_platform.dylib` |
| 5 | `vector_nth` | 914 | - |
| 6 | `map_get` | 829 | `map.c:65` |
| 7 | `clj_equal` | 745 | - |
| 8 | `as_symbol` | 498 | - |
| 9 | `DYLD-STUB$$strcmp` | 329 | - |
| 10 | `is_fixnum` | 196 | - |

### Weitere relevante Hotspots

- `resolve_symbol_in_env`: 17 Aufrufe (aber indirekt über `find_symbol`)
- `resolve_list_operator`: 16 Aufrufe
- `eval_arg_from_expr_with_context`: 14 Aufrufe
- `eval_list`: 13 Aufrufe
- `eval_arithmetic_generic_with_context`: 5 Aufrufe

## Call-Graph-Analyse

Aus der Sample-Datei zeigt sich folgendes Muster:

1. **Rekursive Funktionsaufrufe**: `eval_list` → `eval_function_call_from_list` → `call_function_with_args_and_context` → `eval_function_call` → `eval_body_with_params` → `eval_list_with_context` (Zyklen)

2. **Arithmetik-Dispatch**: `eval_arithmetic_dispatch_with_context` → `eval_arithmetic_generic_with_context` → `eval_arg_from_expr_with_context` → `eval_list` (rekursiv)

3. **Symbol-Resolution**: `resolve_list_operator` → `resolve_symbol_in_env` → `find_symbol` (häufig)

## Optimierungsziele

Basierend auf den Hotspots und Debug-Overhead:

1. **Call-Site-Caches**: Reduziere `resolve_list_operator`-Aufrufe durch Caching
2. **Arity-Fastpaths**: Optimiere `eval_arithmetic_generic_with_context` für binäre Operationen
3. **Dispatch-Tabellen**: Ersetze `if`-Ketten durch Tabellen-Lookup
4. **AST-Normalisierung**: Frühe Symbol-Resolution beim Parsen
5. **TAG-Optimierung**: Reduziere `TAG()`-Aufrufe durch bessere Datenstrukturen

**Hinweis**: Mit 50% Debug-Overhead wäre die "echte" Performance ohne Debug bei ~1497.9×. Das Ziel von ≤20× erfordert daher eine **~75× Verbesserung** der Interpreter-Performance (1497.9 / 20 ≈ 75×).

## Nächste Schritte

1. ✅ Baseline-Messungen abgeschlossen
2. ⏭️ Schritt 2: Eval-Pfad verschlanken (Dispatch-Tabellen)
3. ⏭️ Schritt 3: Call-Site-Caches implementieren
4. ⏭️ Schritt 4: Arity-Fastpaths für Hot-Builtins
5. ⏭️ Schritt 5: Tests & Validierung

## Referenzen

- Benchmark-Script: `benchmarks/compare_fibonacci_naive.sh`
- Sample-Erstellung: `benchmarks/create_baseline_sample.sh`
- Sample-Datei: `sample_baseline_20251205_230957.txt`
- Vergleichs-Ergebnisse: `benchmark_results/fibonacci_naive_comparison.txt`




















