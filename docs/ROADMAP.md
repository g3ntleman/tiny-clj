Tiny-CLJ Roadmap
================

Active/Next
-----------
- Q16.13 Fixed-Point Arithmetic: ✅ COMPLETED
  - Complete Q16.13 fixed-point implementation replacing Float16
  - 29-bit immediate values with 16 integer + 13 fractional bits
  - Arithmetic operations (+, -, *, /) with mixed int/fixed support
  - Type promotion from Fixnum to Fixed-Point for mixed operations
  - Saturation handling for overflow/underflow (±32767.9998 range)
  - Immediate value storage (no heap allocation) with TAG_FIXED=7
  - Single-pass processing for efficient variadic operations
  - ~0.00012 precision (4x better than Float16)
  - 19 comprehensive test suites with 124 total tests passing
  - Binary size: 569KB (repl), 84KB (ESP32-main), 327KB (ESP32-repl)

- Code Quality & DRY Refactoring: ✅ COMPLETED
  - Eliminated ~200 lines of duplicated comparison operator code
  - Generic comparison functions with type promotion
  - Centralized error message constants
  - Memory safety improvements for immediate value handling
  - Release target optimization for embedded deployment
  - Auslagerung von Funktionen aus object.c in thematische Dateien (strings.c, exception.c, function.c, environment.c, equality.c, meta.c)
  - Entfernung redundanter eval-Funktionen und Memory-Management-Optimierungen
  - Symbol-Tabellen-Funktionen nach symbol.c verschoben

- UTF-8 (Phase 1): ✅ COMPLETED
  - Vendor sheredom/utf8.h (done)
  - Parser: validate & iterate codepoints for symbols/strings (no normalization)
  - Tests for UTF-8 roundtrip & delimiters
  - Measure code size impact (Release) and record in `Reports/` (Target: <100KB binary size)
       - Seq Semantics (Core): ✅ COMPLETED
         - seq() for list, vector, map (entry view), string, nil
         - first/rest/next via iterator views (no heap alloc)
         - reduce/concat/apply on iterator basis
         - Equality across seqables (value-based)
       - For-Loops: ✅ COMPLETED
         - for, doseq, dotimes implementations
         - Seq-based iteration with environment binding
         - Performance benchmarks and optimization analysis
       - Unity Test Framework: ✅ COMPLETED
         - Unity C Test Framework integration with command-line parameters
         - Separate test files: memory_tests.c, parser_tests.c, exception_tests.c
         - Central test runner with flexible test suite selection
         - Memory profiling integration with WITH_AUTORELEASE_POOL
         - 17 tests across 3 test suites with individual execution support

Next Priority
-------------
- Async Programming & Event Loop: ✅ COMPLETED
  - Go-Blöcke (go special form) für asynchrones kooperatives Multitasking
  - Event Loop/Runloop mit FIFO-Queue für Task-Verwaltung
  - Result-Channel-Support für Go-Blöcke
  - Manuelle Ausführung über `(run-next-task)` Builtin
  - Integration in REPL für automatische Event-Loop-Verarbeitung
  - Exception-sichere Ausführung mit WITH_AUTORELEASE_POOL
- Timer Functionality: ✅ COMPLETED
  - `(schedule delay-ms fn)` - Einmalige verzögerte Ausführung
  - `(schedule-periodic delay-ms period-ms fn)` - Periodische Ausführung
  - `(cancel-timer timer-id)` - Timer-Abbruch anhand ID
  - Zeitgesteuerte Queue mit sortierter Einfügung
  - Transiente Vektoren für effiziente Timer-Verwaltung
  - STM32-kompatibel und in Embedded-Builds enthalten
  - Umfassende Timer-Tests (15+ Testfälle)
- Core Control Flow: `(let)` Implementation: ✅ COMPLETED
  - Implement `(let)` special form for local variable binding
  - Support binding pairs: `(let [var1 val1 var2 val2 ...] body)`
  - Lexical scoping with shadowing support
  - Integration with existing special forms (if, do, fn, etc.)
  - Unit tests for let evaluation, scoping, and shadowing
  - Memory management for local bindings with autorelease pools
- Core Control Flow: `(do)` Implementation: ✅ COMPLETED
  - Implement `(do)` special form for sequential evaluation
  - Support multiple expressions in sequence: `(do expr1 expr2 ... exprN)`
  - Return value of last expression, nil for empty do
  - Integration with existing special forms (if, let, fn, etc.)
  - Unit tests for do evaluation and return values
- Atom Implementation: ✅ COMPLETED
  - Clojure-Atom Datentyp mit Test-First-Ansatz implementiert
  - Vollständige Atom-API mit Tests
  - Korrekte Reference Counting für Atoms
- Vector Functions: ✅ COMPLETED
  - vec-Funktion implementiert
  - COW (Copy-on-Write) für Vektoren implementiert
  - mutable_flag entfernt, COW-Implementierung
  - Vector-Parser optimiert ohne temporäre Buffer
- Namespace & Module System: ✅ COMPLETED
  - Clojure-kompatible require-Funktion mit Namespace-Aliases
  - refer und refer-all Funktionalität
  - Verbesserte Clojure-Kompatibilität
- Clojure Compatibility: ✅ COMPLETED
  - next und rest sind Clojure-kompatibel
  - Native vector für Clojure-kompatible (vector) Builtin
- Test Framework Enhancements: ✅ COMPLETED
  - JUnit-Style Test-Output mit TRY/CATCH Exception-Handling
  - Test-Counting bei unhandled Exceptions gefixt
  - High-Level API statt Low-Level eval_list()
  - 611 Tests, 0 Failures, 11 Ignored
  - All MinUnit tests migrated to Unity framework
  - Individual test execution support implemented
  - Comprehensive test coverage for all core features
- Metadata Support: ✅ COMPLETED
  - Full metadata support in Desktop-REPL (DEBUG builds)
  - Metadata maps with :name, :ns, :doc, etc.
  - meta and with-meta functions
  - Metadata merging for native functions
  - Release builds exclude metadata code for size optimization
  - Clojure-compatible metadata semantics
- clojure.string Library: ✅ COMPLETED (Phase 1)
  - Basic string manipulation functions (21 functions)
  - Native implementations: trim, upper-case, lower-case, last-index-of, reverse
  - Pure Clojure implementations: blank?, capitalize, ends-with?, escape, etc.
  - Full test coverage with namespace introspection (ns-map, find-ns)
  - Metadata support for all functions
  - Works without TRE (regex support planned for Phase 2)

Planned
-------
- Large-Map:
  - Small→Large promotion; open addressing; pointer-key fastpath for interned symbols
  - Benchmarks vs small maps (N≥16)
- Test Framework Enhancements:
  - Additional test suites: namespace_tests.c, function_tests.c, ui_tests.c
  - Test categories: core, data, control, api, memory, error, ui
  - Parallel test execution for multiple suites
  - JUnit XML output for CI/CD integration
- Optional: Chunked vector seqs for performance (semantics unchanged)
- Symbol lookup:
  - Interning + pointer-key env maps; cache resolution per AST node (optional)
- Symbol Resolution Optimization:
  - Cache resolved values directly in CljSymbol struct (cached_value pointer)
  - Fully qualified symbols (user/x) can be safely cached (always resolve to same value)
  - Targeted cache invalidation on redefinition (only affected symbol, not entire cache)
  - Cache stores Root-Binding for namespace variables
- Dynamic Bindings (after Macros):
  - Dynamic variable detection via earmuffs convention (*name*)
  - Cache pointer can serve as marker for dynamic binding state
  - Thread-local binding stack for dynamic variables
  - binding macro implementation for temporary rebinding
  - Symbol resolution: check dynamic stack first, fallback to Root-Binding
  - Note: Metadata handling (^:dynamic) will be optional and ignored in Release builds

Build & Benchmarks
------------------
- Release builds: macOS fast (-O3), Embedded size (-Os), with dead_strip/gc-sections and LTO when available (Target: <100KB binary size)
- Code-size and performance benchmarks tracked under `Reports/` with 2% significance threshold; baseline auto-update
- Test execution benchmarks: Unity vs MinUnit performance comparison
- Memory profiling integration: Test memory usage tracking and leak detection

Design Decisions (Snapshot)
---------------------------
- No UTF-8 normalization; equality is byte-based
- Borrowed-view iterators: container must outlive view; retain for long-lived views
- Single English README; docs consolidated
- Unity Test Framework: Single target with separate test files for better organization
- Command-line parameter support for test isolation and debugging
- Memory profiling integration with existing WITH_AUTORELEASE_POOL pattern

Housekeeping
------------
- All test_* files live in `Tests/` (root tests consolidated)
- Unity test files: memory_tests.c, parser_tests.c, exception_tests.c in `src/tests/`
- Generated binaries live in `build/`
- CSV reports live in `Reports/`
- Unity test framework: `external/unity/` as git submodule

