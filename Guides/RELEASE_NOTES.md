# Tiny-CLJ Release Notes (2025-10-20)

## Highlights
- **Float16 Support**: Complete half-precision floating-point implementation for embedded systems
- **Arithmetic Operations**: Full support for +, -, *, / with mixed int/float operations
- **Comparison Operators**: Complete set of comparison operators (=, <, >, <=, >=) with type promotion
- **DRY Refactoring**: Eliminated code duplication in comparison operators (~200 lines reduced)
- **Memory Safety**: Fixed immediate value handling in memory management
- **Release Target Optimization**: Separate STM32 builds optimized for embedded deployment

## Float16 Implementation
- **Type System**: Float16 stored as immediate values (no heap allocation)
- **Type Promotion**: Automatic promotion to float32 for internal calculations
- **Mixed Operations**: Seamless int/float arithmetic with single-pass processing
- **Precision**: ~3-4 decimal digits following IEEE-754 binary16 standard
- **Compiler Support**: Uses `_Float16`/`__fp16` when available, portable fallback otherwise

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
