# Memory Profiling Guide für Tiny-CLJ

## Übersicht

Das Memory-Profiling-System ermöglicht es, die Heap-Last und Objekt-Freigaben in Tiny-CLJ zu überwachen. **Wichtig**: Memory-Profiling findet nur für Subtypen von `CljObject` statt, nicht für andere Strukturen wie `SymbolEntry`, `CljNamespace`, `EvalState`, etc.

Dies ist besonders nützlich für:

- **Performance-Optimierung**: Identifikation von Memory-Hotspots
- **Memory-Leak-Detection**: Automatische Erkennung von Speicherlecks
- **Heap-Effizienz-Analyse**: Vergleich verschiedener Implementierungen
- **Benchmarking**: Messung der Memory-Overhead verschiedener Operationen

## Build-Konfiguration

### Debug Builds (Memory-Profiling aktiviert)
```bash
cmake -DCMAKE_BUILD_TYPE=Debug .
make test-memory-simple
```

### Release Builds (Zero-Overhead)
```bash
cmake -DCMAKE_BUILD_TYPE=Release .
make test-memory-simple
# Memory-Profiling ist deaktiviert (alle Makros sind No-Ops)
```

## Verwendung in Tests

### Grundlegende Memory-Profiling-Makros

```c
#include "memory.h"

// Empfohlen: Wrapper um einen Testabschnitt
WITH_MEMORY_PROFILING({
    CljObject *obj = make_int(42);
    retain(obj);
    release(obj);
});
```

### Benchmark-Vergleiche

```c
WITH_MEMORY_PROFILING({
    MemoryStats before = memory_profiler_get_stats();

    // Benchmark-Code hier...
    for (int i = 0; i < 1000; i++) {
        CljObject *obj = make_int(i);
        release(obj);
    }

    MemoryStats after = memory_profiler_get_stats();
    MEMORY_PROFILER_COMPARE_STATS(before, after, "Benchmark Name");
});
```

### Manuelle Statistiken

```c
// Aktuelle Statistiken anzeigen
MEMORY_PROFILER_PRINT_STATS("Current State");

// Memory-Leaks prüfen
MEMORY_PROFILER_CHECK_LEAKS("After Operation");
```

## Memory-Statistiken

### Überwachte Metriken

| Metrik | Beschreibung |
|--------|--------------|
| **total_allocations** | Gesamtanzahl der CljObject-Allokationen |
| **total_deallocations** | Gesamtanzahl der CljObject-Deallokationen |
| **peak_memory_usage** | Spitzen-Speichernutzung in Bytes |
| **current_memory_usage** | Aktuelle Speichernutzung in Bytes |
| **object_creations** | Anzahl der CljObject-Erstellungen |
| **object_destructions** | Anzahl der CljObject-Zerstörungen |
| **retain_calls** | Anzahl der retain()-Aufrufe |
| **release_calls** | Anzahl der release()-Aufrufe |
| **autorelease_calls** | Anzahl der autorelease()-Aufrufe |
| **memory_leaks** | Potentielle Memory-Leaks (allocations - deallocations) |

**Hinweis**: Nur CljObject-Subtypen werden getrackt. Andere Strukturen wie `SymbolEntry`, `CljNamespace`, etc. verwenden direkt `malloc`/`calloc` ohne Profiling.

### Beispiel-Output

```
📊 Memory Statistics for Vector Creation:
  ┌─────────────────────────────────────────────────────────┐
  │ Memory Operations                                       │
  ├─────────────────────────────────────────────────────────┤
  │ Allocations:           15                                │
  │ Deallocations:         15                                │
  │ Peak Memory:          120 bytes                          │
  │ Current Memory:         0 bytes                          │
  │ Memory Leaks:           0                                │
  ├─────────────────────────────────────────────────────────┤
  │ CljObject Operations                                   │
  ├─────────────────────────────────────────────────────────┤
  │ Object Creations:      12                                │
  │ Object Destructions:   12                                │
  │ retain() calls:        18                                │
  │ release() calls:       18                                │
  │ autorelease() calls:    0                                │
  └─────────────────────────────────────────────────────────┘
  📈 Retention Ratio: 1.50 (retain calls per object)
  📈 Deallocation Ratio: 1.00 (deallocations per allocation)
  ✅ Perfect Memory Management: All allocations freed
```

## Integration in bestehende Tests

### MinUnit-Tests erweitern

```c
static char *test_my_function_with_memory_profiling(void) {
    printf("\n=== Testing My Function with Memory Profiling ===\n");

    WITH_MEMORY_PROFILING({
        // Bestehender Test-Code...
        CljObject *result = my_function();
        mu_assert("function works", result != NULL);
        release(result);
    });

    printf("✓ My function test passed\n");
    return 0;
}
```

### For-Loop-Performance mit Memory-Analyse

```c
static char *benchmark_for_loop_with_memory(void) {
    printf("\n=== Benchmarking For-Loop with Memory Analysis ===\n");
    
    MEMORY_TEST_BENCHMARK_START("For-Loop Benchmark");
    
    // For-Loop-Benchmark-Code...
    for (int i = 0; i < 1000; i++) {
        CljObject *result = eval_dotimes(dotimes_call, NULL);
        if (result) release(result);
    }
    
    MEMORY_TEST_BENCHMARK_END("For-Loop Benchmark");
    
    return 0;
}
```

## Memory-Optimierungsstrategien

### 1. Objekt-Pooling
```c
// Für hochfrequente Allokationen
static CljObject* object_pool[100];
static int pool_index = 0;

CljObject* get_pooled_object(void) {
    if (pool_index > 0) {
        return object_pool[--pool_index];
    }
    return make_int(0); // Fallback
}

void return_to_pool(CljObject *obj) {
    if (pool_index < 100) {
        object_pool[pool_index++] = obj;
    } else {
        release(obj);
    }
}
```

### 2. Iterator-Wiederverwendung
```c
// Für wiederholte Seq-Operationen
static SeqIterator *reusable_iterator = NULL;

SeqIterator* get_reusable_seq_iterator(CljObject *obj) {
    if (reusable_iterator) {
        seq_release(reusable_iterator);
    }
    reusable_iterator = seq_create(obj);
    return reusable_iterator;
}
```

### 3. Memory-Effizienz-Vergleiche

```c
// Vergleiche verschiedene Implementierungen
static char *compare_implementations(void) {
    printf("\n=== Comparing Implementation Memory Usage ===\n");
    
    // Implementierung A
    MEMORY_TEST_START("Implementation A");
    implementation_a();
    MEMORY_TEST_END("Implementation A");
    
    // Implementierung B
    MEMORY_TEST_START("Implementation B");
    implementation_b();
    MEMORY_TEST_END("Implementation B");
    
    return 0;
}
```

## Automatische Integration

Das Memory-Profiling ist automatisch in folgende Funktionen integriert:

- `make_int()`, `make_float()`, `make_string()` - Objekt-Erstellung
- `retain()` - Reference-Count-Inkrement
- `release()` - Reference-Count-Dekrement und Objekt-Zerstörung
- `autorelease()` - Autorelease-Pool-Integration

### Was wird NICHT getrackt

Memory-Profiling findet **nur** für Subtypen von `CljObject` statt. Folgende Strukturen werden **nicht** getrackt:

- `SymbolEntry` - Symbol-Tabelle-Einträge
- `CljNamespace` - Namespace-Strukturen  
- `EvalState` - Evaluierungs-Zustand
- `CljObjectPool` - Autorelease-Pool-Strukturen
- Pointer-Arrays (`CljObject**`) - Arrays von CljObject-Pointern

Diese verwenden direkt `malloc`/`calloc` ohne Memory-Profiling.

## Debugging-Tipps

### Memory-Leak-Diagnose
```c
// Vor verdächtiger Operation
MEMORY_PROFILER_CHECK_LEAKS("Before Operation");

// Nach verdächtiger Operation
MEMORY_PROFILER_CHECK_LEAKS("After Operation");

// Detaillierte Statistiken
MEMORY_PROFILER_PRINT_STATS("Detailed Analysis");
```

### Performance-Hotspot-Identifikation
```c
// Vor Performance-kritischem Code
MemoryStats before = memory_profiler_get_stats();

// Performance-kritischer Code
for (int i = 0; i < 1000000; i++) {
    // Intensive Operation
}

// Nach Performance-kritischem Code
MemoryStats after = memory_profiler_get_stats();
MEMORY_PROFILER_COMPARE_STATS(before, after, "Performance Hotspot");
```

## Fazit

Das Memory-Profiling-System bietet:

- ✅ **Zero-Overhead in Release-Builds**
- ✅ **Automatische Memory-Leak-Detection**
- ✅ **Detaillierte Heap-Statistiken**
- ✅ **Einfache Integration in bestehende Tests**
- ✅ **Performance-Optimierungs-Unterstützung**

Mit diesem System können Sie die Heap-Last verschiedener Clojure-Implementierungen vergleichen und Optimierungsmöglichkeiten identifizieren!
