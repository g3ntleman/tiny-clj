# Float16 Support in Tiny-CLJ

## Overview

Tiny-CLJ now includes comprehensive Float16 (half-precision floating-point) support, providing efficient numerical operations optimized for 32-bit embedded systems.

## Features

### ✅ Arithmetic Operations
- **Addition** (`+`): Supports mixed int/float operations
- **Subtraction** (`-`): Full Float16 support with type promotion
- **Multiplication** (`*`): Optimized for embedded systems
- **Division** (`/`): Integer division returns int if no remainder, otherwise Float16

### ✅ Comparison Operators
- **Equality** (`=`): Supports int-int, float-float, and mixed comparisons
- **Less than** (`<`): Type promotion to float32 for comparisons
- **Greater than** (`>`): Consistent with Clojure semantics
- **Less or equal** (`<=`): Full Float16 support
- **Greater or equal** (`>=`): Mixed-type comparisons supported

### ✅ Type System
- **Immediate Values**: Float16 stored as tagged pointers (no heap allocation)
- **Type Promotion**: Automatic promotion to float32 for internal calculations
- **Mixed Operations**: Seamless int/float arithmetic
- **Precision**: ~3-4 decimal digits (IEEE-754 binary16 standard)

## Usage Examples

### Basic Arithmetic
```clojure
(+ 1.5 2.25)        ; => 3.75
(- 5.0 2.5)         ; => 2.5
(* 1.2 3.0)         ; => 3.6
(/ 7 2)             ; => 3 (integer division, no remainder)
(/ 7 3)             ; => 2.333 (Float16 result)
```

### Mixed-Type Operations
```clojure
(+ 1 1.5)           ; => 2.5 (int promoted to float)
(< 1 2.0)           ; => true
(= 1.0 1)           ; => true (mixed comparison)
```

### Comparison Operators
```clojure
(< 1.5 2.0)         ; => true
(> 3.0 2.5)         ; => true
(<= 1.0 1.0)        ; => true
(>= 2.0 1.5)        ; => true
```

## Implementation Details

### Type Promotion Rules
1. **Pure Integers**: Remain integers until float encountered
2. **Float Encountered**: All operands promoted to float32
3. **Internal Calculations**: Always use float32 for precision
4. **Result Type**: Float16 for display, float32 for internal math

### Memory Management
- **Immediate Values**: No heap allocation for Float16
- **Type Safety**: Proper immediate value handling in memory management
- **Efficient Storage**: Tagged pointer representation

### Precision Considerations
- **Float16 Range**: ±65,504 with ~3-4 decimal digits precision
- **Rounding**: IEEE-754 binary16 round-to-nearest-even
- **Special Values**: ±Inf for division by zero, NaN for 0/0
- **Mixed Operations**: May show precision limitations (e.g., 2.2 → 2.199)

## Performance Characteristics

### Binary Sizes
- **tiny-clj-repl**: 569KB (development with profiling)
- **tiny-clj-stm32-main**: 84KB (minimal embedded)
- **tiny-clj-stm32**: 327KB (embedded REPL)

### Optimization Features
- **Single-Pass Processing**: Efficient variadic function implementation
- **Type Promotion**: Minimal overhead for mixed operations
- **Compiler Support**: Uses `_Float16`/`__fp16` when available
- **Fallback Implementation**: Portable IEEE-754 binary16 when needed

## Error Handling

### Division by Zero
```clojure
(/ 1 0)             ; => +Inf
(/ -1 0)            ; => -Inf
(/ 0 0)             ; => NaN
```

### Type Errors
```clojure
(+ "string" 1)      ; => TypeError: Expected number
(< 1 "string")      ; => TypeError: Expected number
```

## Target Systems

### 32-bit Embedded Systems
- **STM32**: Optimized builds available
- **ESP32**: Compatible with Float16 support
- **Memory Efficient**: Immediate value storage
- **Performance**: Optimized for embedded constraints

### 64-bit Systems
- **Future**: Float32 support planned for 64-bit systems
- **Current**: Float16 provides sufficient precision for 32-bit targets

## Technical Specifications

### IEEE-754 Binary16 Format
- **Sign**: 1 bit
- **Exponent**: 5 bits
- **Mantissa**: 10 bits
- **Total**: 16 bits

### Compiler Support
- **Clang**: `__fp16` extension
- **GCC**: `_Float16` (C23)
- **ARM**: Native half-precision support
- **Fallback**: Portable implementation

## Migration Notes

### From Previous Versions
- **Decimal Literals**: Now parsed as Float16 by default
- **Arithmetic**: Enhanced with mixed-type support
- **Comparisons**: New comparison operators available
- **Error Messages**: Centralized error message constants

### Breaking Changes
- **None**: All existing functionality preserved
- **Enhancements**: New Float16 features are additive

## Future Enhancements

### Planned Features
- **Float32 Support**: For 64-bit systems
- **Extended Precision**: Optional higher precision modes
- **Vectorized Operations**: SIMD optimizations
- **Mathematical Functions**: sin, cos, sqrt, etc.

### Performance Improvements
- **JIT Compilation**: Runtime optimization
- **Vectorization**: SIMD instruction usage
- **Memory Pooling**: Reduced allocation overhead

## Testing

### Test Coverage
- **Unit Tests**: 105 tests passing
- **Float16 Tests**: 8 comprehensive test suites
- **Mixed-Type Tests**: Full coverage of int/float operations
- **Error Handling**: Division by zero and type error tests

### Test Suites
- `test_float16_creation_and_conversion`
- `test_float16_arithmetic_operations`
- `test_float16_mixed_type_operations`
- `test_float16_division_with_remainder`
- `test_float16_precision_limits`
- `test_float16_variadic_operations`
- `test_float16_comparison_operators`

## Conclusion

Float16 support in Tiny-CLJ provides efficient numerical operations optimized for embedded systems while maintaining Clojure compatibility. The implementation balances precision, performance, and memory efficiency for 32-bit targets.

For more technical details, see the implementation in:
- `src/value.h`: Float16 type definitions and conversion functions
- `src/builtins.c`: Arithmetic operation implementations
- `src/function_call.c`: Comparison operator implementations
- `src/tests/unit_tests.c`: Comprehensive test suite
