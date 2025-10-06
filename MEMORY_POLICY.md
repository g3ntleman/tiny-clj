# Tiny-CLJ Memory Policy

## Overview

Tiny-CLJ uses manual reference counting for memory management, following Objective-C's pre-ARC pattern with `retain()`, `release()`, and `autorelease()`.

## Core Principles

### 1. Reference Counting Rules
- **Every `make_*()` call** creates an object with `rc=1`
- **`retain(obj)`** increments reference count
- **`release(obj)`** decrements reference count and frees when `rc=0`
- **`autorelease(obj)`** adds object to autorelease pool for later cleanup

### 2. Memory Management Patterns

#### Test Code Pattern (Comfort & Readability)
```c
// Use autorelease() for cleaner, more readable test code
CljObject *vec = autorelease(make_vector(3, 1));
CljObject *list = autorelease(make_list());
// Objects automatically freed at test end - no manual cleanup needed
```

#### Production Code Pattern (Performance)
```c
// Use explicit release() for performance-critical code
CljObject *vec = make_vector(3, 1);
CljObject *list = make_list();
// ... use objects ...
release(vec);
release(list);
```

### 3. When to Use Each Pattern

#### Use `autorelease()` in Tests:
- ✅ **Test code**: Less verbose, cleaner syntax
- ✅ **Debugging**: Automatic cleanup, fewer manual calls
- ✅ **Prototyping**: Quick iteration without cleanup concerns
- ✅ **Non-performance-critical code**: Convenience over speed
- ✅ **Readability**: More readable test code with less cleanup boilerplate
- ✅ **Maintainability**: Easier to write and maintain test code

#### Use `release()` in Production:
- ✅ **Performance-critical code**: No autorelease pool overhead
- ✅ **Real-time systems**: Predictable memory timing
- ✅ **Long-running processes**: Explicit control over object lifetime
- ✅ **Memory-constrained environments**: Immediate cleanup
- ✅ **Objects not returned as values**: Use `release()` when result is not used as return value

### 4. Object Lifecycle Rules

#### Creating Objects:
```c
// Correct: Object starts with rc=1
CljObject *obj = make_vector(10, 1);
```

#### Storing in Data Structures:
```c
// If storing in another object's data structure:
data->items[i] = obj;
retain(obj);  // Increment ref count for the data structure's ownership
```

#### Returning Objects:
```c
// If returning from function (caller gets ownership):
return obj;  // Caller must release()
```

#### Temporary Objects:
```c
// For temporary objects in tests (improved readability):
CljObject *temp = autorelease(make_list());
// Automatically cleaned up - no manual release() needed
```

### 5. Common Anti-Patterns

#### ❌ Don't Do This:
```c
// Double release
CljObject *obj = make_vector(3, 1);
release(obj);
release(obj);  // ❌ CRASH: Double free

// Forgetting to release
CljObject *obj = make_vector(3, 1);
// ... use obj ...
// ❌ LEAK: Never released

// Using autorelease in production loops
for (int i = 0; i < 1000000; i++) {
    CljObject *temp = autorelease(make_vector(10, 1));  // ❌ SLOW: Pool overhead
}
```

#### ✅ Do This Instead:
```c
// Correct release pattern
CljObject *obj = make_vector(3, 1);
release(obj);

// Test pattern with autorelease (improved readability)
CljObject *obj = autorelease(make_vector(3, 1));
// No manual cleanup needed - automatically freed

// Production loop pattern
for (int i = 0; i < 1000000; i++) {
    CljObject *temp = make_vector(10, 1);
    // ... use temp ...
    release(temp);  // ✅ FAST: Immediate cleanup
}

// Production pattern for non-return values
CljObject *int_obj = make_int(i);
tail_data->head = int_obj;
// ... use int_obj ...
release(int_obj);  // ✅ FAST: No autorelease pool overhead
```

## Memory Profiling

The memory profiling system tracks:
- **Allocations**: Objects created with `make_*()`
- **Deallocations**: Objects freed with `release()`
- **Reference Counting**: `retain()` and `release()` calls
- **Memory Leaks**: Objects created but never freed

### Profiling Macros:
```c
// In test code:
MEMORY_TEST_START("Test Name");
// ... test code ...
MEMORY_TEST_END("Test Name");

// In production code (DEBUG builds only):
CREATE(obj);    // Object creation
DEALLOC(obj);   // Object destruction
RETAIN(obj);    // Reference increment
RELEASE(obj);   // Reference decrement
```

### Memory Management Macro Rule:
**ALWAYS use memory management macros instead of direct function calls:**
- ✅ **Use `RETAIN(obj)`** instead of `retain(obj)`
- ✅ **Use `RELEASE(obj)`** instead of `release(obj)`
- ✅ **Use `AUTORELEASE(obj)`** instead of `autorelease(obj)`
- ✅ **Use `CREATE(obj)`** instead of `make_*()` functions
- ✅ **Use `DEALLOC(obj)`** instead of `free(obj)`

This ensures consistent memory profiling and better tracking of all memory operations throughout the codebase.

## API Memory Policy

### Public API Functions

#### Parse Functions
```c
CljObject* parse_string(const char* expr_str, EvalState *eval_state);
```
- **Returns**: Autoreleased object
- **Memory**: Automatically managed by autorelease pool
- **Usage**: No manual `release()` needed

#### Evaluation Functions
```c
CljObject* eval_parsed(CljObject *parsed_expr, EvalState *eval_state);
CljObject* eval_string(const char* expr_str, EvalState *eval_state);
```
- **Returns**: Autoreleased object
- **Memory**: Automatically managed by autorelease pool
- **Usage**: No manual `release()` needed

#### Object Creation Functions
```c
CljObject* make_vector(int count, int initial_value);
CljObject* make_list();
CljObject* make_string(const char* str);
```
- **Returns**: Object with `rc=1` (caller must release)
- **Memory**: Manual management required
- **Usage**: Must call `release()` when done

### API Usage Patterns

#### Correct API Usage:
```c
// Parse and evaluate (both return autoreleased objects)
CljObject *parsed = parse_string(expr, eval_state);
CljObject *result = eval_parsed(parsed, eval_state);
// No manual cleanup needed - both are autoreleased

// Convenience function (also returns autoreleased object)
CljObject *result = eval_string(expr, eval_state);
// No manual cleanup needed - result is autoreleased
```

#### Object Creation (manual management):
```c
// Object creation requires manual release
CljObject *vec = make_vector(10, 1);
// ... use vec ...
release(vec);  // Must release manually
```

## Best Practices Summary

1. **API Functions**: Return autoreleased objects (no manual cleanup)
2. **Object Creation**: Return objects with `rc=1` (manual cleanup required)
3. **Tests**: Use `autorelease()` for convenience and readability
4. **Production**: Use `release()` for performance
5. **Non-return values**: Use `release()` when object is not returned as function result
6. **Data Structures**: Use `retain()` when storing objects
7. **Profiling**: Always track memory usage in tests
8. **Debugging**: Use memory profiler to find leaks
9. **Trust API Design**: Follow documented memory policy

## Implementation Notes

- Memory profiling is **DEBUG-only** (zero overhead in release builds)
- Hook-based system allows clean separation of profiling from business logic
- Vector elements are automatically freed by `release_object_deep()`
- Singletons (empty vectors/lists) skip reference counting
- Primitive types (int, float) are not reference counted

## Common Mistakes and Solutions

### ❌ Incorrect Assumptions
```c
// WRONG: Assuming parse_string needs manual release
CljObject *parsed = parse_string(expr, eval_state);
release(parsed);  // ❌ UNNECESSARY: parse_string returns autoreleased object
```

### ✅ Correct Usage
```c
// CORRECT: Trust the API design
CljObject *parsed = parse_string(expr, eval_state);
CljObject *result = eval_parsed(parsed, eval_state);
// Both are autoreleased - no manual cleanup needed
```

### Memory Policy Verification
- **Always check Doxygen documentation** for memory policy
- **Use memory profiling** to validate assumptions
- **Test-First development** prevents incorrect implementations
- **Trust existing API design** unless proven otherwise
