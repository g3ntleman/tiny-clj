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

#### Use `AUTORELEASE()` in Tests:
- ✅ **Test code**: Less verbose, cleaner syntax
- ✅ **Debugging**: Automatic cleanup, fewer manual calls
- ✅ **Prototyping**: Quick iteration without cleanup concerns
- ✅ **Non-performance-critical code**: Convenience over speed
- ✅ **Readability**: More readable test code with less cleanup boilerplate
- ✅ **Maintainability**: Easier to write and maintain test code
- ✅ **Compatibility**: Funktioniert mit `setjmp`/`longjmp` (wie Foundation pre-ARC)

#### Use `RELEASE()` in Production:
- ✅ **Performance-critical code**: No autorelease pool overhead
- ✅ **Real-time systems**: Predictable memory timing
- ✅ **Long-running processes**: Explicit control over object lifetime
- ✅ **Memory-constrained environments**: Immediate cleanup
- ✅ **Objects not returned as values**: Use `RELEASE()` when result is not used as return value
- ✅ **Compatibility**: Funktioniert mit `setjmp`/`longjmp` exception handling

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
- ✅ **Use `ASSIGN(var, new_obj)`** instead of manual retain/release patterns

This ensures consistent memory profiling and better tracking of all memory operations throughout the codebase.

### RETAIN/RELEASE Macros Return Values

**Important:** The `RETAIN()` and `RELEASE()` macros **return the object** for fluent usage:

```c
// ✅ CORRECT: Compact and fluent
CljObject* nth2(CljObject *vec, CljObject *idx) {
    if (!v || i < 0 || i >= v->count) return NULL;
    return RETAIN(v->data[i]);  // Returns retained object
}

// ❌ VERBOSE: Don't do this
CljObject* nth2(CljObject *vec, CljObject *idx) {
    if (!v || i < 0 || i >= v->count) return NULL;
    RETAIN(v->data[i]);
    return v->data[i];  // Unnecessary extra line
}

// ✅ CORRECT: Fluent chaining
if (is_mutable) {
    v->data[i] = (RETAIN(val), val);
    return RETAIN(vec);  // Both retain and return in one expression
}

// ✅ CORRECT: Test and assign in one expression
for (int i = 0; i < count; i++) {
    CljObject *val = get_value(i);
    if ((vec->data[i] = RETAIN(val))) {  // Assign and test in one line
      vec->count++;
    }
}
```

**Macro Definition:**
```c
// DEBUG build (with profiling)
#define RETAIN(obj) ({ \
    typeof(obj) _tmp = (obj); \
    memory_hook_trigger(MEMORY_HOOK_RETAIN, _tmp, 0); \
    retain(_tmp); \
    _tmp; \  // Returns the object
})

// RELEASE build (optimized)
#define RETAIN(obj) ({ \
    typeof(obj) _tmp = (obj); \
    retain(_tmp); \
    _tmp; \  // Returns the object
})
```

**Benefits:**
- **Concise code** - One line instead of two
- **Better readability** - Intent is clearer
- **Fluent API** - Enables chaining patterns
- **Consistent style** - Same pattern as `AUTORELEASE()`

### ASSIGN Macro for Safe Object Assignment

The `ASSIGN(var, new_obj)` macro provides safe object assignment following the classic Objective-C pattern. It handles retain/release operations automatically and prevents common memory management errors.

#### Usage Pattern:
```c
CljObject *obj = NULL;
CljObject *new_obj = make_int(42);

// Safe assignment - handles retain/release automatically
ASSIGN(obj, new_obj);  // obj is now retained, old value (if any) is released
```

#### What ASSIGN Does:
1. **Retains the new object** (if not NULL)
2. **Releases the old object** (if not NULL) 
3. **Assigns the new object** to the variable
4. **Optimizes self-assignment** (skips operations if `new_obj == var`)

#### Classic Objective-C Pattern:
```c
// ❌ Manual pattern (error-prone):
if (old_obj) {
    release(old_obj);
}
if (new_obj) {
    retain(new_obj);
}
obj = new_obj;

// ✅ ASSIGN macro (safe and concise):
ASSIGN(obj, new_obj);
```

#### Common Use Cases:

**Property-like Assignment:**
```c
// Setting instance variables safely
ASSIGN(self->name, new_name);
ASSIGN(self->value, new_value);
```

**Replacing Objects:**
```c
CljObject *old_vec = make_vector(3, 1);
CljObject *new_vec = make_vector(5, 2);

ASSIGN(old_vec, new_vec);  // old_vec now points to new_vec, old data released
```

**NULL Assignment (Cleanup):**
```c
CljObject *obj = make_int(42);
// ... use obj ...
ASSIGN(obj, NULL);  // Safely releases obj and sets to NULL
```

**Self-Assignment Optimization:**
```c
CljObject *obj = make_int(42);
ASSIGN(obj, obj);  // Optimized - no operations performed
```

#### Macro Definition:
```c
#define ASSIGN(var, new_obj) do { \
    typeof(var) _tmp = (new_obj); \
    if (_tmp != (var)) { \
        if (_tmp != NULL) { \
            memory_hook_trigger(MEMORY_HOOK_RETAIN, _tmp, 0); \
            retain(_tmp); \
        } \
        if ((var) != NULL) { \
            memory_hook_trigger(MEMORY_HOOK_RELEASE, (var), 0); \
            release(var); \
        } \
        (var) = _tmp; \
    } \
} while(0)
```

#### Benefits:
- **Memory Safety** - Prevents leaks and double-frees
- **Self-Assignment Safe** - Optimizes `ASSIGN(obj, obj)` 
- **NULL Safe** - Handles NULL assignments correctly
- **Consistent** - Same pattern as classic Objective-C
- **Profiled** - Integrates with memory profiling system

## API Memory Policy

### Public API Functions

#### Parse Functions
```c
CljObject* parse_string(const char* expr_str, EvalState *eval_state);
```
- **Returns**: Autoreleased object
- **Memory**: Automatically managed by autorelease pool
- **Usage**: No manual `RELEASE()` needed

#### Evaluation Functions
```c
CljObject* eval_parsed(CljObject *parsed_expr, EvalState *eval_state);
CljObject* eval_string(const char* expr_str, EvalState *eval_state);
```
- **Returns**: Autoreleased object
- **Memory**: Automatically managed by autorelease pool
- **Usage**: No manual `RELEASE()` needed

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
3. **Tests**: Use `AUTORELEASE()` for convenience and readability
4. **Production**: Use `RELEASE()` for performance
5. **Non-return values**: Use `RELEASE()` when object is not returned as function result
6. **Data Structures**: Use `RETAIN()` when storing objects
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
RELEASE(parsed);  // ❌ UNNECESSARY: parse_string returns autoreleased object

// WRONG: Manual retain/release assignment (error-prone)
CljObject *obj = NULL;
CljObject *new_obj = make_int(42);
if (obj) release(obj);  // ❌ ERROR-PRONE: Easy to forget or get order wrong
if (new_obj) retain(new_obj);
obj = new_obj;
```

### ✅ Correct Usage
```c
// CORRECT: Trust the API design
CljObject *parsed = parse_string(expr, eval_state);
CljObject *result = eval_parsed(parsed, eval_state);
// Both are autoreleased - no manual cleanup needed

// CORRECT: Use ASSIGN macro for safe assignment
CljObject *obj = NULL;
CljObject *new_obj = make_int(42);
ASSIGN(obj, new_obj);  // ✅ SAFE: Handles retain/release automatically
```

### Memory Policy Verification
- **Always check Doxygen documentation** for memory policy
- **Use memory profiling** to validate assumptions
- **Test-First development** prevents incorrect implementations
- **Trust existing API design** unless proven otherwise
