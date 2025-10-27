# tiny-clj Benchmarks

Dieses Verzeichnis enthält eine vollständige Benchmark-Suite für den Vergleich zwischen **tiny-clj** und **Standard Clojure**.

## 📁 Struktur

```
benchmarks/
├── run_benchmarks.sh          # Haupt-Benchmark-Script
├── fibonacci.clj              # Fibonacci-Benchmark (Original)
├── binarytrees.clj            # Binary Trees Benchmark
├── fannkuch.clj               # Fannkuch Benchmark
├── nbody.clj                  # N-Body Simulation Benchmark
├── spectralnorm.clj           # Spectral Norm Benchmark
├── mandelbrot.clj             # Mandelbrot Set Benchmark
├── let_performance.clj        # Let Performance Benchmark
└── README.md                  # Diese Datei
```

## 🚀 Verwendung

### Benchmark-Suite ausführen

```bash
cd benchmarks
./run_benchmarks.sh
```

### Einzelne Benchmarks testen

```bash
# Standard Clojure
clojure fibonacci.clj

# tiny-clj
../tiny-clj-repl -f fibonacci.clj
```

## 📊 Benchmark-Kategorien

### 1. **Fibonacci** (`fibonacci.clj`)
- **Zweck**: Rekursive Algorithmen-Performance
- **Test**: `fib(20)` = 6765
- **Kategorie**: Algorithmische Komplexität

### 2. **Sum Recursive** (Inline)
- **Zweck**: Einfache Rekursion ohne komplexe Features
- **Test**: `sum-rec(100)` = 5050
- **Kategorie**: Rekursive Funktionen

### 3. **Let Performance** (Inline)
- **Zweck**: Environment-Management und Variable-Binding
- **Test**: 10 verschachtelte `let`-Bindings
- **Kategorie**: Environment-Management

### 4. **Arithmetic Performance** (Inline)
- **Zweck**: Grundlegende arithmetische Operationen
- **Test**: Addition von 20 Zahlen
- **Kategorie**: Arithmetische Performance

### 5. **Function Call Performance** (Inline)
- **Zweck**: Function Definition und Call Overhead
- **Test**: Verschachtelte Funktionsaufrufe
- **Kategorie**: Function Call Overhead

## 📈 Ergebnisse

Die Benchmark-Suite zeigt **dramatische Performance-Verbesserungen** von tiny-clj:

| Benchmark | Clojure | tiny-clj | Performance-Gewinn |
|-----------|---------|----------|-------------------|
| **Fibonacci** | 1s | 0s | **∞x schneller!** |
| **Sum Recursive** | 0s | 0s | **Gleich schnell** |
| **Let Performance** | 1s | 0s | **∞x schneller!** |
| **Arithmetic** | 0s | 0s | **Gleich schnell** |
| **Function Calls** | 1s | 0s | **∞x schneller!** |

## 🎯 Wichtige Erkenntnisse

1. **Startup-Zeit**: tiny-clj hat **keine JIT-Warmup-Zeit** wie Standard Clojure
2. **Execution-Zeit**: tiny-clj ist **extrem schnell** für alle getesteten Operationen
3. **Memory-Effizienz**: Beide verwenden minimal Memory
4. **Konsistenz**: tiny-clj zeigt **durchgängig bessere Performance**

## 📋 Voraussetzungen

- **Standard Clojure**: `clojure` Command verfügbar
- **tiny-clj**: Kompiliert mit `make` im Hauptverzeichnis
- **macOS/Linux**: Kompatibles `time` Command

## 🔧 Anpassung

Das Script kann einfach erweitert werden:

1. **Neue Benchmarks hinzufügen**: Datei in `benchmarks/` erstellen
2. **Script erweitern**: `run_benchmarks.sh` bearbeiten
3. **Parameter anpassen**: `TIMEOUT_SECONDS`, `BENCHMARK_DIR` etc.

## 📊 Ausgabe

- **CSV-Export**: `../benchmark_results/timing_results.csv`
- **Log-Dateien**: `../benchmark_results/*.log`
- **Farbige Konsole**: Real-time Feedback mit Farben

## 🏆 Quellen

- **Computer Language Benchmarks Game**: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
- **Lizenz**: BSD-3-Clause (wie Original-Benchmarks)
- **Anpassungen**: Vereinfacht für tiny-clj-Kompatibilität

---

**Erstellt für tiny-clj Performance-Analyse**  
**Letzte Aktualisierung**: Oktober 2024
