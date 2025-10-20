# Numerical Promotion Rules in Tiny-CLJ

## Overview

Tiny-CLJ implements a sophisticated numerical promotion system that balances precision, performance, and memory efficiency for embedded systems. This document details the promotion rules and internal calculation strategies.

## Core Principles

### 1. Immediate Value Storage
- **Integers**: Stored as immediate values (tagged pointers)
- **Float16**: Stored as immediate values (tagged pointers)
- **No Heap Allocation**: Immediate values avoid memory management overhead

### 2. Type Promotion Strategy
- **Lazy Promotion**: Only promote to float when float operand encountered
- **Single-Pass Processing**: Process all operands in one pass
- **Internal Precision**: Always use float32 for internal calculations
- **Result Type**: Return appropriate type based on operation result

## Promotion Rules

### Arithmetic Operations (+, -, *, /)

#### Rule 1: Pure Integer Operations
```clojure
(+ 1 2 3)           ; => 6 (integer)
(- 10 3)            ; => 7 (integer)
(* 2 3 4)           ; => 24 (integer)
(/ 8 2)             ; => 4 (integer, no remainder)
```

#### Rule 2: Float Encountered
```clojure
(+ 1 2.0 3)         ; => 6.0 (float16)
(- 10 3.5)          ; => 6.5 (float16)
(* 2 3.0 4)         ; => 24.0 (float16)
(/ 7 2.0)           ; => 3.5 (float16)
```

#### Rule 3: Division with Remainder
```clojure
(/ 7 3)             ; => 2.333 (float16, has remainder)
(/ 8 3)             ; => 2.667 (float16, has remainder)
(/ 6 2)             ; => 3 (integer, no remainder)
```

### Comparison Operations (=, <, >, <=, >=)

#### Rule 1: Type Promotion for Comparisons
```clojure
(< 1 2.0)           ; => true (1 promoted to 1.0)
(> 2.5 2)           ; => true (2 promoted to 2.0)
(= 1.0 1)           ; => true (mixed comparison)
```

#### Rule 2: Consistent Float32 Internal Calculations
- All comparisons use float32 internally
- Type promotion happens before comparison
- Results maintain precision consistency

## Implementation Details

### Single-Pass Processing

The arithmetic functions use a single-pass fold approach:

```c
static ID reduce_add(ID *args, int argc) {
    if (argc == 0) return OBJ_TO_ID((CljObject*)make_fixnum(0));
    if (argc == 1) return OBJ_TO_ID(RETAIN(ID_TO_OBJ(args[0])));
    
    bool sawFloat = false;
    int acc_i = 0;
    float acc_f = 0.0f;
    
    for (int i = 0; i < argc; i++) {
        if (!args[i] || (!IS_FIXNUM(args[i]) && !IS_FLOAT16(args[i]))) {
            throw_exception_formatted("TypeError", __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
            return OBJ_TO_ID(NULL);
        }
        
        if (!sawFloat) {
            if (IS_FIXNUM(args[i])) {
                acc_i += AS_FIXNUM(args[i]);
            } else {
                sawFloat = true;
                acc_f = (float)acc_i + AS_FLOAT16(args[i]);
            }
        } else {
            acc_f += IS_FIXNUM(args[i]) ? (float)AS_FIXNUM(args[i]) : AS_FLOAT16(args[i]);
        }
    }
    
    return sawFloat ? OBJ_TO_ID((CljObject*)make_float16(acc_f))
                    : OBJ_TO_ID((CljObject*)make_fixnum(acc_i));
}
```

### Type Promotion Functions

#### Extract Numeric Values
```c
static bool extract_numeric_values(CljObject *a, CljObject *b, float *val_a, float *val_b) {
    // Extract value from first object
    if (is_fixnum((CljValue)a)) {
        *val_a = (float)as_fixnum((CljValue)a);
    } else if (is_float16((CljValue)a)) {
        *val_a = as_float16((CljValue)a);
    } else {
        return false; // Invalid type
    }
    
    // Extract value from second object
    if (is_fixnum((CljValue)b)) {
        *val_b = (float)as_fixnum((CljValue)b);
    } else if (is_float16((CljValue)b)) {
        *val_b = as_float16((CljValue)b);
    } else {
        return false; // Invalid type
    }
    
    return true;
}
```

## Precision Considerations

### Float16 Precision
- **Range**: ±65,504 with ~3-4 decimal digits precision
- **Rounding**: IEEE-754 binary16 round-to-nearest-even
- **Mixed Operations**: May show precision limitations

### Example Precision Effects
```clojure
(+ 1 1.2)           ; => 2.199 (not 2.2)
                    ; Reason: float32 -> float16 -> float32 conversion
                    ; This is expected behavior for half-precision
```

### Internal Calculations
- **Always float32**: Internal calculations use 32-bit precision
- **Result Conversion**: Convert to Float16 only for final result
- **Consistency**: All operations maintain consistent precision

## Error Handling

### Type Errors
```clojure
(+ "string" 1)      ; => TypeError: Expected number
(< 1 "string")      ; => TypeError: Expected number
```

### Division by Zero
```clojure
(/ 1 0)             ; => +Inf
(/ -1 0)            ; => -Inf
(/ 0 0)             ; => NaN
```

## Performance Characteristics

### Memory Efficiency
- **Immediate Values**: No heap allocation for numbers
- **Type Safety**: Proper immediate value handling
- **Single Pass**: Efficient variadic function processing

### Computational Efficiency
- **Lazy Promotion**: Only promote when necessary
- **Compiler Support**: Uses native half-precision when available
- **Optimized Paths**: Separate code paths for pure integer operations

## Target System Considerations

### 32-bit Embedded Systems
- **Float16**: Optimal precision for 32-bit targets
- **Memory**: Immediate value storage saves memory
- **Performance**: Single-pass processing reduces overhead

### 64-bit Systems (Future)
- **Float32**: Planned for higher precision on 64-bit systems
- **Backward Compatibility**: Float16 support maintained
- **Migration Path**: Gradual transition to higher precision

## Testing and Validation

### Test Coverage
- **Unit Tests**: 105 tests passing
- **Float16 Tests**: 8 comprehensive test suites
- **Mixed-Type Tests**: Full coverage of promotion scenarios
- **Error Handling**: Division by zero and type error tests

### Test Categories
1. **Creation and Conversion**: Float16 value creation
2. **Arithmetic Operations**: All basic operations
3. **Mixed-Type Operations**: Int/float combinations
4. **Division with Remainder**: Integer vs float results
5. **Precision Limits**: Float16 precision boundaries
6. **Variadic Operations**: Multiple argument processing
7. **Comparison Operators**: All comparison types
8. **Error Handling**: Type errors and special cases

## Best Practices

### For Developers
1. **Use Mixed Operations**: Leverage automatic type promotion
2. **Understand Precision**: Be aware of Float16 limitations
3. **Test Edge Cases**: Validate behavior with boundary values
4. **Consider Target**: Choose appropriate precision for use case

### For Embedded Deployment
1. **STM32 Targets**: Use optimized builds (84KB/327KB)
2. **Memory Management**: Immediate values reduce allocation overhead
3. **Performance**: Single-pass processing optimizes for embedded constraints
4. **Precision**: Float16 provides sufficient precision for most embedded applications

## Future Enhancements

### Planned Improvements
- **Float32 Support**: Higher precision for 64-bit systems
- **Vectorized Operations**: SIMD optimizations
- **Mathematical Functions**: sin, cos, sqrt, etc.
- **Extended Precision**: Optional higher precision modes

### Performance Optimizations
- **JIT Compilation**: Runtime optimization
- **Memory Pooling**: Reduced allocation overhead
- **Compiler Integration**: Better native type support

## Conclusion

The numerical promotion system in Tiny-CLJ provides an efficient balance between precision, performance, and memory usage for embedded systems. The single-pass processing and immediate value storage make it particularly well-suited for 32-bit targets while maintaining Clojure compatibility.

For implementation details, see:
- `src/value.h`: Type definitions and conversion functions
- `src/builtins.c`: Arithmetic operation implementations
- `src/function_call.c`: Comparison operator implementations
- `src/tests/unit_tests.c`: Comprehensive test suite
