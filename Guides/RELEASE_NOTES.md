# Tiny-CLJ Release Notes (2025-10-05)

## Highlights
- UTF-8 parsing: validation & iteration for symbols/strings (no normalization).
- New UTF-8 tests incl. checkmark character (✓) for symbol and string roundtrip.
- Autorelease pool now backed by CljWeakVector for better locality and fewer allocations.
- README: Documented ObjC pre-ARC style memory model (retain/release/autorelease); exceptions are not autoreleased; singletons skip RC.

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
