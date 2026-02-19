# Tiny-CLJ Release Notes (2026-02-19)

## Latest Updates (Recent Commits)

### Record Support (Latest)
- **Records (`defrecord`)**: Basis-Funktionalität ist implementiert (Descriptor, Konstruktoren `->Type`/`map->Type`, Feldzugriff).
- **Runtime Integration**: Record-Equality berücksichtigt den konkreten Descriptor (Type-Identity) und vergleicht Felder Clojure-kompatibel.
- **Performance Focus**: Hot-Path-Invarianten in der Record-Laufzeit sind auf `CLJ_ASSERT` ausgerichtet, um Release-Build-Overhead zu minimieren.
- **Implementation Restriction**: Extension-Map (`extmap`) ist aktuell **nicht** implementiert.
  - Zusätzliche Keys, die nicht in `defrecord` deklariert sind, werden nicht unterstützt.
  - Entsprechende Operationen enden mit `NotImplementedException`.

### Piezo Sound Engine & macOS Simulation (Latest)
- **Piezo Audio Engine**: Sound-Engine für Piezo-Buzzer ist implementiert (Noten-/Frequenzsteuerung, Timing-Sequenzen, Embedded-fokussierter Runtime-Pfad).
- **Desktop Validation**: macOS-Simulationspfad für die Audio-Engine ist verfügbar, um Verhalten ohne ESP-Hardware reproduzierbar zu testen.
- **Parity Goal**: Gemeinsames API-Verhalten zwischen Embedded-Backend und macOS-Simulation für schnellere Iteration und Regression-Tests.

### Version 0.3: tiny-db flash database layer (sorted key-value store)
- **tiny-db storage layer**: Database layer for flash-like block devices using a sorted key-value store (B-tree)
  - Copy-on-write / append-only write path to tolerate resets and power-loss scenarios
  - Prefix iteration via cursors and large values via chunking/streaming helpers
  - Integrated into Tiny-CLJ via the filesystem layer and key-value bindings

### Regular Expression Support (Latest)
- **Regex Subset**: Basic regular expression support via `re-pattern`, `re-find`, `re-matches`, and `regex?`
  - Based on tiny-regex-c library (Public Domain)
  - Clojure-compatible string representation: Regex objects print as `#"pattern"`
  - **Supported Features**:
    - Character classes: `[a-z]`, `[^0-9]`
    - Quantifiers: `*`, `+`, `?`
    - Anchors: `^` (start), `$` (end)
    - Shorthand classes: `\d` (digits), `\w` (word), `\s` (whitespace)
    - Escaped characters: `\.`, `\\`, etc.
  - **Unsupported Features** (throw exceptions with clear error messages):
    - Alternation: `|`
    - Quantifier bounds: `{n,m}`
    - Lookahead/lookbehind: `(?=...)`, `(?!...)`, `(?<=...)`, `(?<!...)`
    - Named groups: `(?<name>...)`
    - Non-greedy quantifiers: `*?`, `+?`, `??`
    - Backreferences: `\1` through `\9`
  - Full test coverage with 28+ regex tests
  - Maximum pattern length: 256 characters

### Date/Time Library (Latest)
- **tiny-clj.datetime**: Neue Datum/Zeit-Konvertierungsbibliothek
  - `civil-from-days` / `days-from-civil` - Unix-Tage ↔ Datum (Jahr/Monat/Tag)
  - `time-from-millis` / `millis-from-time` - Millisekunden ↔ Zeit (Stunde/Minute/Sekunde)
  - `date-time` / `to-raw` - High-Level API für Timestamp-Konvertierung
  - `format-iso` - ISO-8601 Formatierung (z.B. "2024-12-23T14:30:45")
  - Basiert auf Howard Hinnants optimierten Datumsalgorithmen (Public Domain)
  - Keine Lookup-Tables, keine Schleifen - rein mathematische Berechnung

### Clojure-Compatible Macroexpander & Destructuring (Latest)
- **Macro System**: Vollständiger clojure-kompatibler Makroexpander implementiert
  - `defmacro` Special Form für Makro-Definitionen
  - `macroexpand-1` und `macroexpand` Funktionen in Clojure implementiert
  - Quasiquote (`), Unquote (~), und Splice-Unquote (~@) Reader-Macros
  - Makro-Registry in Namespaces für Makro-Speicherung
  - Automatische Makro-Expansion vor der Evaluation
  - Hybrid-Ansatz: Komplexe Logik in Clojure, minimale C-Glue-Layer
- **Destructuring**: Clojure-kompatible Destructuring-Transformation
  - `destructure` Funktion in Clojure implementiert
  - Unterstützung für Vektoren, Maps, und verschachtelte Patterns
  - Integration in `let`, `loop`, und `defn` Special Forms
  - AST-Canonicalisierung delegiert komplexe Logik an Clojure-Funktion
  - Reduziert C-Code durch Clojure-Implementierung
- **defn als Makro**: `defn` ist jetzt ein Clojure-Makro statt C Special Form
  - Syntaktischer Zucker für `def` und `fn`
  - Metadata-Transfer von `def`-Form zu definiertem Wert
  - TCO-Rewriting für rekursive Aufrufe (qualified names)
  - `:native` Funktionen werden in `fn` Special Form behandelt
  - Unterstützung für mehrere Body-Ausdrücke in `fn`

### Sequence Support for Maps (Latest)
- **Map Sequencing**: `seq` now works with maps, returning a sequence of `[key value]` vectors
  - `(seq {:a 1 :b 2 :c 3})` returns `([:a 1] [:b 2] [:c 3])`
  - Maps can now be used with all sequence functions (`first`, `rest`, `next`, `map`, etc.)
  - Clojure-compatible behavior: empty maps return `nil` from `seq`
  - Copy-on-Write (COW) optimization for map iterators to reduce allocations
  - Full test coverage with map sequencing tests

### clojure.repl Functions Available (Latest)
- **REPL Helper Functions**: Complete implementation of clojure.repl namespace utilities
  - `doc` - Print documentation for a var (with metadata support)
  - `source` - Print source code for a function (native implementation)
  - `dir` - List all public functions in a namespace
  - `find-doc` - Search documentation across namespaces (simplified)
  - `pst` - Print stack trace (simplified, full implementation planned)
  - All functions available via `(require 'clojure.repl)` or qualified calls
  - Clojure-compatible API and behavior
  - Note: `ast` function has been removed (was previously available for debugging)

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
  - Alle Timer-Funktionen sind in Embedded-Builds (ESP32) enthalten

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
- **Release Target Optimization**: Separate embedded builds optimized for ESP32 deployment

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
- **Minimal embedded build**: 84KB
- **Embedded REPL build**: 327KB
- **Optimizations**: -Os -DNDEBUG -ffunction-sections with dead code elimination

## Changes
- Parser: Non-ASCII defaults to `parse_symbol`; UTF-8 validation via `external/utf8.h`.
- Tests: Added UTF-8 tests in `src/tests/test_parser.c`.
- Memory: `autorelease()` pushes into a weak vector; pool pop releases elements and vector.
- Types: `as_vector()` accepts `CLJ_VECTOR_PERSISTENT` and `CLJ_VECTOR_TRANSIENT_WEAK`; added finalizer for `CLJ_VECTOR_TRANSIENT_WEAK`.
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
