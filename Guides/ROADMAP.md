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
  - Binary size: 569KB (repl), 84KB (STM32-main), 327KB (STM32-repl)

- Code Quality & DRY Refactoring: ✅ COMPLETED
  - Eliminated ~200 lines of duplicated comparison operator code
  - Generic comparison functions with type promotion
  - Centralized error message constants
  - Memory safety improvements for immediate value handling
  - Release target optimization for embedded deployment

- UTF-8 (Phase 1): ✅ COMPLETED
  - Vendor sheredom/utf8.h (done)
  - Parser: validate & iterate codepoints for symbols/strings (no normalization)
  - Tests for UTF-8 roundtrip & delimiters
  - Measure code size impact (Release) and record in `Reports/` (Target: <60KB binary size)
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
- Core Control Flow: `(do)` Implementation
  - Implement `(do)` special form for sequential evaluation
  - Support multiple expressions in sequence: `(do expr1 expr2 ... exprN)`
  - Return value of last expression, nil for empty do
  - Integration with existing special forms (if, let, fn, etc.)
  - Unit tests for do evaluation and return values
- Test Framework Enhancements:
  - Migrate remaining MinUnit tests to Unity (namespace, function, ui tests)
  - Implement individual test execution: `./unity-tests memory allocation`
  - Add CTest integration for CI/CD pipeline
  - Performance benchmarks for test execution time

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

Build & Benchmarks
------------------
- Release builds: macOS fast (-O3), Embedded size (-Os), with dead_strip/gc-sections and LTO when available (Target: <60KB binary size)
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
- Generated binaries live in `build/` or `build-release/`
- CSV reports live in `Reports/`
- Unity test framework: `external/unity/` as git submodule

