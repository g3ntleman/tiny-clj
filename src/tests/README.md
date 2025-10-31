# Tiny-Clj Tests

Dieser Ordner enthält alle Test- und Demo-Programme für Tiny-Clj.

## Test-Programme

### Grundlegende Tests

- **`basic_test.c`** - Basis-Funktionalitätstest
  - Testet grundlegende Symbol-Erstellung
  - Testet Funktionsaufrufe ohne Parameter
  - Verifiziert Memory Management

- **`simple_test.c`** - Einfacher Funktionalitätstest
  - Testet Symbol-Internierung
  - Testet Funktions-Erstellung
  - Verifiziert grundlegende Operationen

### Funktionsaufruf-Tests

- **`function_test.c`** - Funktionsaufruf-Test
  - Testet Funktionsaufrufe mit Parametern
  - Verifiziert Arity-Check
  - Testet Environment-System

- **`param_test.c`** - Parameter-Test
  - Testet verschiedene Parameter-Anzahlen
  - Verifiziert Stack-Allokation
  - Testet Parameter-Bindung

- **`test_function_calls.c`** - Umfassender Funktionsaufruf-Test
  - Testet komplexe Funktionsaufrufe
  - Verifiziert alle Funktionsaufruf-Features
  - Testet Fehlerbehandlung

### Array-System-Demos

- **`demo_array_system.c`** - Array-System-Demonstration
  - Zeigt Stack-Allokation für Argumente
  - Demonstriert interleaved Key/Value Arrays
  - Verifiziert STM32-sichere Implementierung

### KV-Makros-Tests

- **`test_kv_macros.c`** - KV-Makros-Grundtest
  - Testet alle KV-Makro-Funktionen
  - Verifiziert Map-Operationen
  - Testet Iteration und Utility-Funktionen

- **`kv_macros_demo.c`** - KV-Makros-Erweiterte Demo
  - Zeigt praktische Anwendungen
  - Demonstriert Environment-Lookup
  - Testet Performance mit großen Arrays

- **`test_alloc_macros.c`** - ALLOC-Makros-Test
  - Testet STACK_ALLOC, ALLOC und ALLOC_ZERO
  - Demonstriert verschiedene Allokations-Strategien
  - Verifiziert Memory Management

## Kompilierung und Ausführung

Alle Tests können mit CMake kompiliert werden:

```bash
# Alle Tests kompilieren
cmake . && make

# Einzelne Tests ausführen
./basic-test
./demo-array-system
./test-kv-macros
./kv-macros-demo
```

## Verfügbare Test-Executables

- `basic-test` - Basis-Funktionalität
- `simple-test` - Einfache Tests
- `function-test` - Funktionsaufruf-Tests
- `param-test` - Parameter-Tests
- `test-function-calls` - Umfassende Funktionsaufruf-Tests
- `demo-array-system` - Array-System-Demonstration
- `test-kv-macros` - KV-Makros-Grundtest
- `kv-macros-demo` - KV-Makros-Erweiterte Demo
- `test-alloc-macros` - ALLOC-Makros-Test

## Test-Strategie

Die Tests sind hierarchisch aufgebaut:

1. **Basis-Tests** - Grundlegende Funktionalität
2. **Feature-Tests** - Spezifische Features
3. **Integration-Tests** - Zusammenspiel verschiedener Komponenten
4. **Performance-Tests** - Geschwindigkeit und Speicherverbrauch
5. **Demo-Programme** - Praktische Anwendungsbeispiele

## Wartung

- Neue Tests sollten in diesem Ordner erstellt werden
- Include-Pfade verwenden `../` für Header-Dateien
- Tests sollten eigenständig lauffähig sein
- Cleanup muss in allen Tests implementiert sein
