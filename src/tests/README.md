# Tiny-Clj Tests

Dieser Ordner enthält alle Test-Programme für Tiny-Clj.

## Test-Struktur

### Haupt-Test-Runner
- **`run-tests`** - Einheitlicher Test-Runner für alle Tests
  - Führt alle MinUnit-Tests aus
  - Aktiviert Memory Profiling
  - Unterstützt Test-Kategorien (core, data, control, api, memory, error, ui)

### Core Tests (Kernfunktionalität)
- **`test_unit.c`** - Unit Tests für grundlegende Objekt-Erstellung
- **`test_parser.c`** - Parser-Tests für Clojure-Syntax
- **`test-namespace.c`** - Namespace-Management Tests
- **`test_function_types.c`** - Function Type Tests
- **`test_nil_arithmetic.c`** - Nil-Arithmetik Tests

### Data Tests (Datenstrukturen)
- **`test_seq.c`** - Sequence/Iterator Tests
- **`test_memory.c`** - Memory Management Tests

### Control Tests (Kontrollstrukturen)
- **`test_for_loops.c`** - For-Loop Tests (dotimes, doseq, for)

### API Tests (Öffentliche API)
- **`test_eval_string_api.c`** - eval_string API Tests

### Error Tests (Fehlerbehandlung)
- **`test-exception-handling.c`** - Exception Handling Tests

### UI Tests (Benutzeroberfläche)
- **`test_line_editor.c`** - Line Editor Tests
- **`test_platform_mock.c`** - Platform Mock Tests
- **`test_platform_abstraction.c`** - Platform Abstraction Tests
- **`test_repl_line_editing.c`** - REPL Line Editing Tests

### Test Utilities
- **`test-utils.c`** - Gemeinsame Test-Utilities
- **`minunit.h`** - MinUnit Test Framework
- **`test_registry.h`** - Test Registry für einheitlichen Runner
- **`run_tests_main.c`** - Haupt-Test-Runner

## Kompilierung und Ausführung

### Alle Tests ausführen
```bash
# Alle Tests kompilieren und ausführen
make run-tests
./run-tests
```

### Spezifische Test-Kategorien
```bash
# Nur Core-Tests
./run-tests --suite core

# Nur Memory-Tests
./run-tests --suite memory

# Nur UI-Tests
./run-tests --suite ui
```

### Test-Optionen
```bash
# Hilfe anzeigen
./run-tests --help

# Verfügbare Tests auflisten
./run-tests --list

# Spezifischen Test ausführen
./run-tests --test test_name
```

## Test-Features

### Memory Profiling
- Automatische Memory Leak Detection
- Detaillierte Speicher-Statistiken
- Objekt-Typ Tracking
- Retention Ratio Analysis

### Test-Kategorien
- **core**: Grundlegende Funktionalität
- **data**: Datenstrukturen und Memory
- **control**: Kontrollstrukturen
- **api**: Öffentliche API
- **memory**: Speicher-Management
- **error**: Fehlerbehandlung
- **ui**: Benutzeroberfläche

### Test-Statistiken
- Anzahl ausgeführter Tests
- Erfolgreiche/fehlgeschlagene Tests
- Memory Leak Warnings
- Performance Metrics

## Wartung

- Neue Tests sollten in die entsprechende Kategorie-Datei eingefügt werden
- Tests müssen mit dem einheitlichen `run-tests` Runner kompatibel sein
- Memory Profiling ist für alle Tests aktiviert
- Tests sollten eigenständig lauffähig sein
- Cleanup muss in allen Tests implementiert sein