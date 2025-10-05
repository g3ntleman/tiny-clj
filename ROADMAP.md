Tiny-CLJ Roadmap
================

Active/Next
-----------
- UTF-8 (Phase 1): ✅ COMPLETED
  - Vendor sheredom/utf8.h (done)
  - Parser: validate & iterate codepoints for symbols/strings (no normalization)
  - Tests for UTF-8 roundtrip & delimiters
  - Measure code size impact (Release) and record in `Reports/`
       - Seq Semantics (Core): ✅ COMPLETED
         - seq() for list, vector, map (entry view), string, nil
         - first/rest/next via iterator views (no heap alloc)
         - reduce/concat/apply on iterator basis
         - Equality across seqables (value-based)
       - For-Loops: ✅ COMPLETED
         - for, doseq, dotimes implementations
         - Seq-based iteration with environment binding
         - Performance benchmarks and optimization analysis

Next Priority
-------------
- Large-Map:
  - Small→Large promotion; open addressing; pointer-key fastpath for interned symbols
  - Benchmarks vs small maps (N≥16)

Planned
-------
- Large-Map:
  - Small→Large promotion; open addressing; pointer-key fastpath for interned symbols
  - Benchmarks vs small maps (N≥16)
- Optional: Chunked vector seqs for performance (semantics unchanged)
- Symbol lookup:
  - Interning + pointer-key env maps; cache resolution per AST node (optional)

Build & Benchmarks
------------------
- Release builds: macOS fast (-O3), Embedded size (-Os), with dead_strip/gc-sections and LTO when available
- Code-size and performance benchmarks tracked under `Reports/` with 2% significance threshold; baseline auto-update

Design Decisions (Snapshot)
---------------------------
- No UTF-8 normalization; equality is byte-based
- Borrowed-view iterators: container must outlive view; retain for long-lived views
- Single English README; docs consolidated

Housekeeping
------------
- All test_* files live in `Tests/` (root tests consolidated)
- Generated binaries live in `build/` or `build-release/`
- CSV reports live in `Reports/`

