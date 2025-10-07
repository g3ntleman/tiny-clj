Tiny-CLJ
========

Overview
--------
Tiny-CLJ is an **embedded-first Clojure interpreter** designed to run on microcontrollers (STM32, ARM Cortex-M) and desktop platforms (macOS, Linux). Written in pure C99/C11 for maximum portability and minimal resource usage.

**Primary Target:** STM32 and other embedded controllers  
**Development Platform:** macOS/Linux for testing and REPL

Project Goals
-------------
### Primary Objective
**Follow the Clojure language as good as possible.** Maximum compatibility with standard Clojure features, syntax, and behavior.

### Core Design
- Keep the core source base minimal, readable, and lean
- Pure C99/C11 - no POSIX-only features for embedded compatibility
- Provide essential Lisp/Clojure primitives (numbers, booleans, strings, symbols, lists, vectors, maps)
- Manual reference counting for predictable memory behavior on embedded systems

### Embedded Focus
- **Primary:** Run on STM32, ESP32, ARM Cortex-M microcontrollers
- Support common embedded capabilities: timers, GPIO, networking (REST calls) via platform abstractions
- Small binary size (<200KB) and minimal RAM usage
- No dynamic linking (dlopen/dlsym) - all symbols compiled-in

### Desktop Development
- macOS/Linux for REPL development and testing
- Compatible with Clojure libraries without native dependencies
- Fast iteration cycle for embedded code development

### Quality
- Robust test and benchmark setup with automated comparison and history
- Clojure-compatible syntax where possible (`*ns*`, `def`, `fn`, etc)
- Consistent error handling and memory management

Architecture
------------
- Core Types: `CljObject` plus specific structs for vectors, lists, maps, strings, symbols, exceptions.
- Evaluation: Simple list evaluation and function call handling; built-ins implemented in C.
- Parser: Tokenization and parsing into AST nodes represented by `CljObject` structures.
- Runtime/Memory: Manual reference counting with `retain`, `release`, and `autorelease` pools (no autorelease for known singletons).
- Platform Abstraction: Minimal I/O/console helpers per platform.
- REPL/CLI: `tiny-clj-repl` supports `--no-core`, `-e/--eval`, and `-f/--file` for scripted evaluation.

Design Decisions
----------------
### Embedded-First Constraints
- **Pure C99/C11:** No POSIX-only APIs (dlsym, pthread, etc) for STM32 compatibility
- **Static Linking:** All code compiled-in, no dynamic loading
- **Manual Registry:** Test and function discovery via compile-time registry, not runtime symbol lookup
- **Small Binary:** Target <200KB for embedded deployment
- **Predictable Memory:** Manual reference counting, no garbage collection

### Memory Model
- Manual reference counting inspired by Objective‑C pre‑ARC (retain/release/autorelease)
- Exceptions are never autoreleased; ownership is transferred on throw/catch
- Singletons (`nil`, `true`, `false`) skip RC entirely
- Reference Counting applies to heap `CljObject`s; primitives/singletons do not participate
- Double release throws exception

### Code Quality
- Early Return: Prefer early returns over deep if‑else pyramids
- Expressive Macros: Use small, expressive C macros in tests; avoid macro overuse in core
- Core vs Tests: Core code remains small; tests/benchmarks live separately
- Benchmarks: 2% significance threshold; auto‑baseline update and append significant changes to history

Coding Style
------------
- Comments and documentation in English.
- Prefer early-return and straightforward control flow; avoid deep nesting.
- Descriptive, full-word identifiers; avoid abbreviations.
- Only add non-obvious comments (rationale, invariants, caveats).
- Keep formatting consistent; do not reformat unrelated code in edits.

Building & Running
------------------
- Build: `cmake . && make -j`
- Release (macOS speed / Embedded size): `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release -j`
- REPL: `./tiny-clj-repl [--no-core] [-e EXPR] [-f FILE] [--repl]`
- Tests: `ctest --output-on-failure`
- Benchmarks: `./test-benchmark --all` (see `--list`, `--report`, `--compare`)

Reports & Artifacts
-------------------
- CSV/Reports liegen unter `Reports/` (Baseline, History, Results).
- Generierte Binaries liegen unter `build/` bzw. `build-release/`.

Documentation
-------
**Main Guides** (in `Guides/`):
- **`ROADMAP.md`** - Geplante Arbeiten (UTF‑8, Seq‑Semantik, Large‑Map, optionale Chunked‑Seqs) und Status
- **`DEVELOPMENT_INSIGHTS.md`** - API Design, Memory Management, Test-First-Strategie und Code-Qualität
- **`MEMORY_POLICY.md`** - Detaillierte Memory-Management-Richtlinien und Best Practices
- **`MEMORY_PROFILING_GUIDE.md`** - Anleitung für Memory-Profiling und Leak-Detection
- **`PERFORMANCE_GUIDE.md`** - Performance-Optimierung und Benchmarking
- **`ERROR_HANDLING_GUIDE.md`** - Fehlerbehandlung und TRY/CATCH Exception-Management
- **`IMPROVEMENTS_SUMMARY.md`** - Zusammenfassung der letzten Verbesserungen
- **`RELEASE_NOTES.md`** - Versionshinweise und Änderungen

**Test Documentation:**
- **`src/tests/README.md`** - Test-Dokumentation und -Struktur

**Reports & Analysis** (in `Reports/`):
- **`test_data_analysis.md`** - Test-Datenanalyse und -Statistiken
- **`memory_usage_comparison.md`** - Memory-Usage-Vergleiche zwischen Implementierungen
- **`seq_performance_analysis.md`** - Seq-Performance-Analyse und -Optimierung
- **`utf8_size_impact.md`** - UTF-8 Implementierung und Code-Size-Impact

Command-Line Examples
---------------------
- Eval once without loading core: `./tiny-clj-repl --no-core -e "(+ 1 2)"`
- Eval forms from file: `./tiny-clj-repl -f program.clj`

Contributing
------------
- Keep the core small and clean; adhere to coding style above.
- Favor small, focused PRs with tests and benchmark updates.


# Test commit for hooks
