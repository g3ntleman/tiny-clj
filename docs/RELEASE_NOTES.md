# Tiny-CLJ Release Notes (2025-11-27)

## Latest Updates (Recent Commits)

### Metadata & clojure.string Support (2025-11-27)
- **Metadata Support**: Full metadata support in Desktop-REPL (DEBUG builds)
  - Metadata maps with :name, :ns, :doc, :line, :file, etc.
  - `meta` and `with-meta` functions fully implemented
  - Automatic metadata merging for native functions
  - `map_merge()` helper function for efficient metadata merging
  - Release builds exclude metadata code for optimal binary size
  - Clojure-compatible metadata semantics
- **clojure.string Library (Phase 1)**: Complete string manipulation library
  - 21 string functions implemented (trim, upper-case, lower-case, etc.)
  - 5 native implementations for performance-critical operations
  - 16 pure Clojure implementations (blank?, capitalize, ends-with?, etc.)
  - Full test coverage with 611 tests passing
  - Works without regex support (TRE planned for Phase 2)
- **Namespace Introspection**: Enhanced namespace system
  - `ns-map` function returns all mappings in a namespace
  - `find-ns` function returns namespace object by name
  - `ns-resolve` with symbol resolution cache for performance
  - `ns_find_by_symbol()` for efficient symbol-based namespace lookup
- **Symbol System Improvements**:
  - `SYM_KW_NAME` constant added alongside `SYM_KW_NS`
  - Compile-time native function table initialization
  - Static symbol data structures for zero runtime overhead
  - `MAP_FOR_EACH` macro usage for cleaner iteration
- **Code Quality**: Major refactoring and cleanup
  - Removed unnecessary type casts throughout codebase
  - Fixed all compiler warnings (0 warnings)
  - Simplified `eval_arg()` with early `SYM_NIL` check
  - resolve_cache properly maintained (was being destroyed)
- **Test Framework**: Complete test coverage
  - 611 tests, 0 failures, 11 ignored
  - All MinUnit tests migrated to Unity framework
  - Fixed 3 failing tests (resolve_cache bugs)
  - Test isolation and reproducibility ensured

### Async Programming & Event Loop (Latest)
- **Go-Blöcke (go special form)**: Implementierung von asynchronen Go-Blöcken für kooperative Multitasking
  - `(go body)` - Wrappt den Body in eine nullstellige Funktion und reiht sie in die Event-Loop-Queue ein
  - FIFO-Queue für Task-Verwaltung mit manueller Ausführung über `(run-next-task)`
  - Result-Channel-Support für Rückgabewerte von Go-Blöcken
  - Vereinfachte, minimal-kompatible Version im Vergleich zu Clojures core.async
  - Integration in REPL: Event-Loop wird automatisch während REPL-Zyklen verarbeitet
- **Event Loop/Runloop**: Zentrale Event-Loop-Implementierung für asynchrone Task-Verarbeitung
  - `event_loop_enqueue()` - Fügt Go-Blöcke zur Queue hinzu
  - `event_loop_run_next()` - Führt den nächsten Task aus (FIFO-Reihenfolge)
  - `event_loop_clear()` - Leert die Queue für Test-Isolation
  - Automatische Timer-Verarbeitung während Task-Ausführung
  - Exception-sichere Ausführung mit WITH_AUTORELEASE_POOL
- **Timer-Funktionalität**: Clojure-kompatible Timer-API für verzögerte und periodische Ausführung
  - `(schedule delay-ms fn)` - Führt eine Funktion einmal nach `delay-ms` Millisekunden aus
  - `(schedule-periodic delay-ms period-ms fn)` - Führt eine Funktion periodisch aus
  - `(cancel-timer timer-id)` - Bricht einen Timer anhand seiner ID ab
  - Timer-IDs werden als Integer zurückgegeben und können zum Abbrechen verwendet werden
  - Zeitgesteuerte Queue mit sortierter Einfügung für effiziente Timer-Verarbeitung
  - Transiente Vektoren für effiziente Timer-Verwaltung im Runtime
  - Umfassende Timer-Tests mit 15+ Testfällen
  - Alle Timer-Funktionen sind STM32-kompatibel und in Embedded-Builds enthalten

### Code Refactoring & Architecture Improvements (Latest)
- **Object.c Refactoring**: Auslagerung von Funktionen aus object.c in thematische Dateien
  - String-Formatierung (to_cstring, pr_str, print_str) nach strings.c
  - Exception-Erstellung (make_exception) nach exception.c
  - Object-Erstellung: make_list nach list.c, make_function nach function.c
  - Environment-Funktionen nach environment.c
  - Function-Call-Funktionen nach function_call.c
  - Equality-Funktion (clj_equal) nach equality.c
  - Metadata-Funktionen nach meta.c
  - CMakeLists.txt aktualisiert für neue Dateien
  - Alle Warnungen behoben (const char* statt char*)
- **Code Quality**: Entfernung redundanter eval-Funktionen und Memory-Management-Optimierungen
- **Symbol Refactoring**: Symbol-Tabellen-Funktionen von object.c nach symbol.c verschoben
- **Type System Improvements**: `as_fixnum` gibt jetzt `int` statt `int32_t` zurück für bessere Konsistenz
- **Code Cleanup**: Entfernung doppelter Includes und falscher `unused`-Attribute
- **Test Coverage**: 495 Tests, 0 Failures, 0 Ignored

### Atom Implementation & Clojure Compatibility (Recent)
- **Atom Data Type**: Implementierung von Clojure-Atom mit Test-First-Ansatz
- **Atom Functions**: Vollständige Atom-API mit Tests
- **Memory Management**: Korrekte Reference Counting für Atoms
- **Test Coverage**: Atom-Tests erfolgreich integriert

### Namespace & Module System (Recent)
- **Require Function**: Clojure-kompatible require-Funktion implementiert
- **Namespace Aliases**: Unterstützung für Namespace-Aliases
- **Refer Mechanisms**: refer und refer-all Funktionalität
- **Clojure Compatibility**: Verbesserte Kompatibilität mit Clojure-Namespace-System

### Vector Functions & COW Optimization (Recent)
- **vec Function**: Implementierung der vec-Funktion mit Test-First-Ansatz
- **COW for Vectors**: Copy-on-Write für Vektoren implementiert
- **Mutable Flag Removal**: Entfernung von mutable_flag, Implementierung von COW
- **Vector Parser**: Optimierung des Vector-Parsers ohne temporäre Buffer
- **nil Handling**: Korrekte Behandlung von nil-Werten in vec und list Iteration

### Test Framework Improvements (Recent)
- **JUnit-Style Output**: JUnit-ähnliche Test-Ausgabe mit TRY/CATCH Exception-Handling
- **Test Counting**: Fix für Test-Counting bei unhandled Exceptions
- **Test Refactoring**: High-Level API statt Low-Level eval_list()
- **Test Consolidation**: Konsolidierung von COW-Tests und Test-Runner-Cleanup

### Clojure Compatibility Improvements (Recent)
- **next/rest Functions**: next und rest sind jetzt Clojure-kompatibel
- **Function Cleanup**: Entfernung von map_assoc und clj_equal_id Funktionen
- **Native Vector**: Implementierung von native_vector für Clojure-kompatible (vector) Builtin

### High-Level Test Refactoring & Vector Functions (2025-01-26)
- **Test Refactoring**: All go-block tests now use high-level `eval_string()` API instead of low-level `eval_list()`
- **Vector Functions**: Implemented `peek` and `pop` functions for vectors
- **COW Optimization**: `pop` now uses Copy-on-Write - O(1) for RC=1, O(n) for RC>1
- **nth Enhancement**: `nth` now supports 3 arguments (with default value for out-of-bounds access)
- **Event Loop Fix**: Fixed double RELEASE bug in `event_loop_run_next` that caused premature channel deallocation
- **Code Cleanup**: Removed debug output, duplicate tests, and simplified comments
- **Test Coverage**: All 495 tests passing (0 failures)

### Documentation Cleanup & Testing Improvements
- **Documentation Consolidation**: Removed 25 unused markdown files, keeping only documented files
- **Testing Framework**: Enhanced TESTING_GUIDE.md with efficient debugging workflows
- **Test Consolidation**: Moved CljValue tests to dedicated test_values.c file
- **Exception Handling**: Fixed autorelease pool exception propagation bug
- **Test Coverage**: All 495 tests now pass successfully

### Exception Handling Refactoring
- **DRY Principle**: Centralized exception throwing with `throw_exception_object()`
- **Architecture**: Moved exception functions from object.c to dedicated exception.c/h
- **Autorelease Pool**: Simplified WITH_AUTORELEASE_POOL macro without exception swallowing
- **Memory Safety**: Fixed exception propagation in autorelease pools

### Memory Management & Testing
- **Fixed-Point Overflow**: Implemented Defense in Depth strategy for overflow detection
- **Memory Profiler**: Fixed counter reset issues and improved test isolation
- **Test Framework**: Enhanced Unity test framework with better error reporting
- **Memory Leaks**: Resolved various memory management issues in tests

## Previous Release Highlights (2025-10-20)
- **Q16.13 Fixed-Point Support**: Complete fixed-point arithmetic implementation for embedded systems
- **Arithmetic Operations**: Full support for +, -, *, / with mixed int/float operations
- **Comparison Operators**: Complete set of comparison operators (=, <, >, <=, >=) with type promotion
- **DRY Refactoring**: Eliminated code duplication in comparison operators (~200 lines reduced)
- **Memory Safety**: Fixed immediate value handling in memory management
- **Release Target Optimization**: Separate STM32 builds optimized for embedded deployment

## Q16.13 Fixed-Point Implementation
- **Type System**: Q16.13 Fixed-Point stored as immediate values (no heap allocation)
- **Type Promotion**: Automatic promotion from Fixnum to Fixed-Point for mixed operations
- **Mixed Operations**: Seamless int/fixed arithmetic with single-pass processing
- **Precision**: ~0.00012 precision (4x better than Float16)
- **Saturation**: Overflow/underflow handled with saturation to ±32767.9998
- **Numerical Promotion**: See implementation details in `src/builtins.c` and `src/function_call.c`

## Code Quality Improvements
- **DRY Principles**: Generic comparison functions eliminate code duplication
- **Memory Management**: Safe handling of immediate values vs heap objects
- **Error Handling**: Centralized error message constants
- **Type Safety**: Proper immediate value checks in memory operations

## Release Target Optimization
- **tiny-clj-repl**: 569KB (development with memory profiling)
- **tiny-clj-stm32-main**: 84KB (minimal embedded build)
- **tiny-clj-stm32**: 327KB (embedded REPL)
- **Optimizations**: -Os -DNDEBUG -ffunction-sections with dead code elimination

## Changes
- Parser: Non-ASCII defaults to `parse_symbol`; UTF-8 validation via `external/utf8.h`.
- Tests: Added UTF-8 tests in `src/tests/test_parser.c`.
- Memory: `autorelease()` pushes into a weak vector; pool pop releases elements and vector.
- Types: `as_vector()` accepts `CLJ_VECTOR` and `CLJ_WEAK_VECTOR`; added finalizer for `CLJ_WEAK_VECTOR`.
- Docs: Expanded Design Decisions in `README.md` with the memory model.

## Benchmarks (current run)
- repl_startup_eval_10x: ~35.0 ms total (~3.50 ms/iter, ~286 ops/sec).
- exe_size_cmp: unchanged report.
- No significant change (>=2%) detected vs. previous baseline.

## Notes
- UTF-8 support: validation & iteration only; normalization intentionally omitted.
- Singletons are never autoreleased; exceptions follow explicit ownership rules.
- Code architecture: object.c wurde deutlich reduziert durch Auslagerung in thematische Dateien (strings.c, exception.c, function.c, environment.c, equality.c, meta.c).
- Test coverage: 495 Tests, 0 Failures, 0 Ignored - alle Tests bestehen nach Refactoring.
