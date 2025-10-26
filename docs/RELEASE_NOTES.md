# Tiny-CLJ Release Notes (2025-01-26)

## Latest Updates (Recent Commits)

### Documentation Cleanup & Testing Improvements
- **Documentation Consolidation**: Removed 25 unused markdown files, keeping only documented files
- **Testing Framework**: Enhanced TESTING_GUIDE.md with efficient debugging workflows
- **Test Consolidation**: Moved CljValue tests to dedicated test_values.c file
- **Exception Handling**: Fixed autorelease pool exception propagation bug
- **Test Coverage**: All 145 tests now pass successfully

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
