Tiny-CLJ
========

Overview
--------
Tiny-CLJ is a small C-based interpreter for a Clojure-like language, optimized for clarity, small core, and portability (macOS and embedded targets).

Project Goals
-------------
- Keep the core source base minimal, readable, and lean.
- Provide essential Lisp/Clojure primitives (numbers, booleans, strings, symbols, lists, vectors, maps) and a simple evaluator.
- Implement built-ins and a few core special forms (e.g., if, when) with clear semantics and consistent error handling.
- Maintain a robust test and benchmark setup, with automated comparison and history.
- Embedded focus: support common embedded controller capabilities (timers, GPIO, networking incl. REST calls) via platform abstractions.
- macOS focus: allow usage of Clojure libraries that have no special native dependencies.

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
- Memory Model: Manual reference counting inspired by Objective‑C pre‑ARC (retain/release/autorelease). Exceptions are never autoreleased; ownership is transferred on throw/catch. Singletons skip RC entirely.
- Singletons: `nil`, `true`, `false`, and empty‑collection singletons are static and never autoreleased.
- Reference Counting: Applies to heap `CljObject`s; primitives/singletons do not participate.
- Exceptions: Use `throw_exception` rather than "error objects as values". Double release throws.
- Early Return: Prefer early returns over deep if‑else pyramids.
- Expressive Macros: Use small, expressive C macros in tests and where readability benefits; avoid macro overuse in core.
- Core vs Tests: Core code remains small; tests/benchmarks live separately and should not bloat core.
- Benchmarks: 2% significance threshold; auto‑baseline update and append significant changes to history.

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
- **`ROADMAP.md`** - Geplante Arbeiten (UTF‑8, Seq‑Semantik, Large‑Map, optionale Chunked‑Seqs) und Status
- **`DEVELOPMENT_INSIGHTS.md`** - API Design, Memory Management, Test-First-Strategie und Code-Qualität
- **`MEMORY_POLICY.md`** - Detaillierte Memory-Management-Richtlinien und Best Practices
- **`MEMORY_PROFILING_GUIDE.md`** - Anleitung für Memory-Profiling und Leak-Detection
- **`PERFORMANCE_GUIDE.md`** - Performance-Optimierung und Benchmarking
- **`ERROR_HANDLING_GUIDE.md`** - Fehlerbehandlung und Exception-Management
- **`IMPROVEMENTS_SUMMARY.md`** - Zusammenfassung der letzten Verbesserungen
- **`RELEASE_NOTES.md`** - Versionshinweise und Änderungen
- **`src/tests/README.md`** - Test-Dokumentation und -Struktur

**Reports & Analysis:**
- **`Reports/test_data_analysis.md`** - Test-Datenanalyse und -Statistiken
- **`Reports/memory_usage_comparison.md`** - Memory-Usage-Vergleiche zwischen Implementierungen
- **`Reports/seq_performance_analysis.md`** - Seq-Performance-Analyse und -Optimierung
- **`Reports/utf8_size_impact.md`** - UTF-8 Implementierung und Code-Size-Impact

Command-Line Examples
---------------------
- Eval once without loading core: `./tiny-clj-repl --no-core -e "(+ 1 2)"`
- Eval forms from file: `./tiny-clj-repl -f program.clj`

Contributing
------------
- Keep the core small and clean; adhere to coding style above.
- Favor small, focused PRs with tests and benchmark updates.


# Test commit for hooks
