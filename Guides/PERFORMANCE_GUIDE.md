# Tiny-CLJ Performance Guide

## 🚀 Performance-Optimierungsstrategien

### 1. **Tail-Call Optimization (TCO)**
- **`recur` Special Form** für konstanten Stack-Verbrauch
- **30-50% Performance-Gewinn** bei rekursiven Funktionen
- **Embedded-freundlich** - verhindert Stack-Overflow

```clojure
; ❌ Schlecht: Normale Rekursion (Stack-Overflow bei großen Daten)
(def factorial (fn [n] (if (= n 0) 1 (* n (factorial (- n 1))))))

; ✅ Gut: Tail-Call-Optimized
(def factorial-recur (fn [n acc] (if (= n 0) acc (recur (- n 1) (* n acc)))))
```

### 2. **Recur-basierte Funktionen**
```clojure
; Map mit Tail-Call-Optimization
(def map-recur (fn [f coll acc]
  (if (empty? coll)
    (reverse acc)
    (recur f (rest coll) (cons (f (first coll)) acc)))))

; Reduce mit Tail-Call-Optimization  
(def reduce-recur (fn [f acc coll]
  (if (empty? coll)
    acc
    (recur f (f acc (first coll)) (rest coll)))))

; Filter mit Tail-Call-Optimization
(def filter-recur (fn [pred coll acc]
  (if (empty? coll)
    (reverse acc)
    (if (pred (first coll))
      (recur pred (rest coll) (cons (first coll) acc))
      (recur pred (rest coll) acc)))))
```

### 3. **Benchmarking Best Practices**
- **Nie `system()` verwenden** für Micro-Benchmarks
- **In-Memory-Tests** für echte Performance-Messung
- **Profiling-Tools** für Hotspot-Identifikation

### 2. **Code-Optimierungen**
```c
// ✅ Gut: Fast path für ASCII
if (cp < 128) {
    return ascii_lookup[cp];
}

// ❌ Schlecht: Immer Unicode-Checks
return complex_unicode_check(cp);
```

### 3. **Parser-Optimierungen**
- **Lazy Validation**: UTF-8 nur bei Bedarf validieren
- **Symbol Caching**: Häufige Symbole cachen
- **Inline Functions**: Kleine Funktionen inline

### 4. **Memory-Optimierungen**
- **Object Pools**: Wiederverwendung von Objekten
- **Reference Counting**: Minimale RC-Overhead
- **Stack Allocation**: Wo möglich, Stack statt Heap

### 5. **Profiling-Tools**
```bash
# Micro-Benchmarks
./scripts/micro_benchmark.sh

# Vollständiges Profiling
./scripts/profile_performance.sh

# Benchmark-Vergleiche
./scripts/benchmark_compare.sh
```

## 📈 Performance-Metriken

### Aktuelle Benchmarks:
- **Parser**: 0.019ms (5.2M ops/sec)
- **UTF-8-Validation**: <1ns pro Zeichen
- **Symbol-Checks**: <1ns pro Zeichen

### Ziel-Metriken:
- **REPL-Startup**: <1ms
- **Parsing**: <0.01ms pro Expression
- **Memory**: <1MB Baseline

## 🎯 Optimierungs-Prioritäten

1. **Hot Paths**: Häufigste Code-Pfade optimieren
2. **Memory**: Heap-Allocations minimieren
3. **Branching**: Conditional Logic reduzieren
4. **Loops**: Iterationen optimieren

## 🔧 Debugging-Tools

```bash
# Performance-Analyse
./scripts/micro_benchmark.sh

# Memory-Profiling
valgrind --tool=massif ./test-benchmark

# Code-Coverage
gcov test-benchmark
```

## 📊 Benchmark-Interpretation

- **<1ms**: Exzellent
- **1-10ms**: Gut
- **10-100ms**: Akzeptabel
- **>100ms**: Optimierung erforderlich
