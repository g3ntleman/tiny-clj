# Benchmark Status Analyse

## ✅ Benchmarks die unverändert laufen können

### 1. **`arithmetic_performance.clj`** ⚠️ (mit Crash)
- **Status**: Läuft bis zum Crash
- **Problem**: AddressSanitizer SEGV in `memory.c:687` (release_object_deep)
- **Funktioniert**: Definitionen und Ausführung bis zum Cleanup
- **Benötigt**: Fix für Memory-Management-Problem

### 2. **`sleep_benchmark.clj`** ⚠️ (mit Crash)
- **Status**: Läuft bis zum Crash
- **Problem**: AddressSanitizer SEGV in `memory.c:687` (release_object_deep)
- **Funktioniert**: Definitionen und Ausführung bis zum Cleanup
- **Benötigt**: Fix für Memory-Management-Problem

### 3. **`sum_rec.clj`** ⚠️ (mit Crash)
- **Status**: Läuft bis zum Crash
- **Problem**: AddressSanitizer SEGV in `memory.c:687` (release_object_deep)
- **Funktioniert**: Definitionen und Ausführung bis zum Cleanup
- **Benötigt**: Fix für Memory-Management-Problem

### 4. **`let_performance.clj`** ⚠️ (mit Crash)
- **Status**: Läuft bis zum Crash
- **Problem**: AddressSanitizer SEGV in `memory.c:687` (release_object_deep)
- **Funktioniert**: Definitionen und Ausführung bis zum Cleanup
- **Benötigt**: Fix für Memory-Management-Problem

### 5. **`function_call_performance.clj`** ⚠️ (mit Crash)
- **Status**: Läuft bis zum Crash
- **Problem**: AddressSanitizer SEGV in `memory.c:687` (release_object_deep)
- **Funktioniert**: Definitionen und Ausführung bis zum Cleanup
- **Benötigt**: Fix für Memory-Management-Problem

### 6. **`fibonacci.clj`** ⚠️ (mit Crash)
- **Status**: Läuft bis zum Crash
- **Problem**: AddressSanitizer SEGV in `memory.c:687` (release_object_deep)
- **Funktioniert**: Definitionen und Ausführung bis zum Cleanup
- **Benötigt**: Fix für Memory-Management-Problem

## ❌ Benchmarks die Änderungen benötigen

### 1. **`minimal_benchmark.clj`**
- **Problem**: UTF-8 Fehler (Emoji-Zeichen `🚀`, `✅`)
- **Lösung**: Emojis entfernen oder durch ASCII ersetzen
- **Status**: Läuft teilweise (Funktionen werden definiert, aber Ausführung schlägt fehl)

### 2. **`simple_clojure_benchmarks.clj`**
- **Problem 1**: UTF-8 Fehler (Emoji-Zeichen `🚀`, `📊`, `✅`)
- **Problem 2**: Verwendet `#()` Syntax (Reader-Macro für anonyme Funktionen)
- **Lösung**: 
  - Emojis entfernen
  - `#()` durch `(fn [...] ...)` ersetzen
- **Status**: Läuft nicht (Parser-Fehler)

## 🔍 Gemeinsames Problem

**Alle Benchmarks crashen mit demselben Memory-Management-Problem:**
- **Fehler**: AddressSanitizer SEGV in `memory.c:687` (`release_object_deep`)
- **Ursache**: Vermutlich Double-Free oder Use-After-Free beim Cleanup
- **Betroffen**: Alle Benchmarks, die `time` oder `dotimes` verwenden
- **Lösung**: Memory-Management in `release_object_deep` überprüfen

## 📋 Empfehlung

### Sofort lauffähig (nach Memory-Fix):
1. `arithmetic_performance.clj`
2. `sleep_benchmark.clj`
3. `sum_rec.clj`
4. `let_performance.clj`
5. `function_call_performance.clj`
6. `fibonacci.clj`

### Nach kleinen Anpassungen:
1. `minimal_benchmark.clj` - Emojis entfernen
2. `simple_clojure_benchmarks.clj` - Emojis entfernen + `#()` Syntax ersetzen

### Nicht getestet (möglicherweise funktionsfähig):
- `binarytrees.clj`
- `fannkuch.clj`
- `nbody.clj`
- `spectralnorm.clj`
- `mandelbrot.clj`
- `special_forms_perf.clj`
- `complex_perf.clj`

