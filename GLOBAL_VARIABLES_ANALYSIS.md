# Analyse: Globale Variablen in Tests

## Übersicht
Diese Analyse untersucht, welche globalen Variablen in Tests verwendet werden und ob sie zwischen Tests richtig zurückgesetzt werden.

## Globale Variablen in Tests

### 1. `g_runtime` (runtime.c)
- **Typ**: `TinyClJRuntime` (statisch alloziert)
- **Verwendung**: Wird in `setUp()` durch `runtime_init()` initialisiert
- **Reset-Verhalten**:
  - ✅ `clojure_core_cache` wird erhalten (sollte persistieren)
  - ✅ `symbol_table` wird erhalten (sollte persistieren)
  - ❌ `builtins_registered` wird nur einmal gesetzt und nie zurückgesetzt
  - ⚠️ `ns_registry` wird in `runtime_init()` zurückgesetzt, aber in `tearDown()` wird `runtime_free()` aufgerufen, was `ns_cleanup()` aufruft (wenn kein Cache vorhanden)
- **Problem**: `builtins_registered` wird nie zurückgesetzt, könnte zu Problemen führen wenn Tests die Builtins ändern

### 2. `g_special_symbols_initialized` (unity_test_runner.c)
- **Typ**: `static bool`
- **Verwendung**: Flag, ob `init_special_symbols()` bereits aufgerufen wurde
- **Reset-Verhalten**: ❌ Wird nur einmal gesetzt und nie zurückgesetzt
- **Problem**: Kein Problem, da Special Symbols nur einmal initialisiert werden müssen

### 3. `g_clojure_core_loaded` (unity_test_runner.c)
- **Typ**: `static bool`
- **Status**: ✅ **ENTFERNT** - Wurde nicht verwendet

### 4. `g_suppress_time_output` (function_call.c)
- **Typ**: `static bool`
- **Verwendung**: Unterdrückt die Ausgabe von `eval_time` in Tests
- **Reset-Verhalten**: ❌ Wird in `setUp()` auf `true` gesetzt, aber nie zurückgesetzt
- **Problem**: Kein Problem für Tests, aber sollte in `tearDown()` zurückgesetzt werden für Konsistenz

### 5. `g_core_quiet` (clojure_core.c)
- **Typ**: `static bool`
- **Verwendung**: Unterdrückt Ausgaben beim Laden von clojure.core
- **Reset-Verhalten**: ❌ Wird nie zurückgesetzt
- **Problem**: Wird in Tests nicht verwendet, aber sollte zurückgesetzt werden

### 6. `g_test_eval_state` (unity_test_runner.c)
- **Typ**: `EvalState*`
- **Verwendung**: Globaler EvalState für alle Tests
- **Reset-Verhalten**: ✅ Wird in `setUp()` neu erstellt, in `tearDown()` wird `runtime_free()` aufgerufen
- **Problem**: `runtime_free()` könnte den Namespace-Cache löschen, aber das wird verhindert durch Preservation-Logik

### 7. `SYM_*` Variablen (symbol.c)
- **Typ**: `CljObject*` (z.B. `SYM_PLUS`, `SYM_TIME`, `SYM_DEF`, etc.)
- **Verwendung**: Globale Symbol-Pointer für spezielle Symbole
- **Reset-Verhalten**: ❌ Werden in `init_special_symbols()` initialisiert, aber nie zurückgesetzt
- **Problem**: Kein Problem, da diese nur einmal initialisiert werden müssen

### 8. `g_memory_stats` und `g_memory_verbose_mode` (extern)
- **Typ**: `MemoryStats` und `bool`
- **Verwendung**: Memory-Profiling-Statistiken
- **Reset-Verhalten**: ✅ Werden in `setUp()` durch `memory_profiler_reset()` zurückgesetzt

## Probleme und Empfehlungen

### Kritische Probleme
1. **`g_runtime.builtins_registered`**: Wird nie zurückgesetzt
   - **Empfehlung**: Sollte in `tearDown()` zurückgesetzt werden, oder die Logik sollte überprüft werden

### Nicht-kritische Probleme
1. **`g_suppress_time_output`**: Wird nie zurückgesetzt
   - **Empfehlung**: In `tearDown()` auf `false` zurücksetzen für Konsistenz

2. **`g_core_quiet`**: Wird nie zurückgesetzt
   - **Empfehlung**: In `tearDown()` auf `false` zurücksetzen

3. **`g_clojure_core_loaded`**: ✅ **ENTFERNT** - Wurde nicht verwendet

## Empfohlene Änderungen

### tearDown() erweitern
```c
void tearDown(void) {
    // Reset time output suppression
    set_suppress_time_output(false);
    
    // Reset clojure.core quiet mode (falls verwendet)
    // clojure_core_set_quiet(false); // Falls verfügbar
    
    // JUnit-style: Only print memory stats if verbose mode is enabled
    if (g_memory_verbose_mode) {
        memory_profiler_print_stats("Test Complete");
    }
    // Check for leaks silently (only prints if leaks detected and reporting enabled)
    memory_profiler_check_leaks("Test Complete");
    
    runtime_free();
}
```

### runtime_free() überprüfen
- `g_runtime.builtins_registered` sollte zurückgesetzt werden, wenn `builtins_registered` in `setUp()` geprüft wird

