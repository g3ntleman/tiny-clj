# Tiny-CLJ Memory Policy

## Overview

Tiny-CLJ uses manual reference counting for memory management, following Objective-C's pre-ARC pattern with `retain()`, `release()`, and `autorelease()`. Additionally, Tiny-CLJ uses **immediate values** (32-bit tagged pointers) for small data types that don't require heap allocation or reference counting.

## Core Principles

### 1. Immediate Values (No Memory Management Required)

Tiny-CLJ uses **32-bit tagged pointers** for immediate values that don't require heap allocation or reference counting:

#### Supported Immediate Types:
- **Fixnums**: 29-bit signed integers (range: -536,870,912 to 536,870,911)
- **Characters**: 21-bit Unicode characters (range: 0 to 2,097,151)
- **Booleans**: `true` and `false` values
- **Nil**: Represented as `NULL` pointer
- **Fixed-Point**: Q16.13 fixed-point numbers (29-bit immediate values)

#### Memory Management for Immediates:
```c
// ✅ CORRECT: No memory management needed for immediates
CljValue num = make_fixnum(42);        // No RELEASE() needed
CljValue ch = make_char('A');          // No RELEASE() needed  
CljValue flag = make_special(SPECIAL_TRUE); // No RELEASE() needed
CljValue nil_val = NULL;               // No RELEASE() needed

// ✅ CORRECT: Check if value is immediate before releasing
if (!is_immediate(value)) {
    RELEASE((CljObject*)value);  // Only release heap objects
}
```

#### Benefits of Immediate Values:
- **Zero allocation overhead** - No heap allocation required
- **No reference counting** - No retain/release calls needed
- **Better performance** - Direct value access without pointer dereferencing
- **Memory efficiency** - Small values stored directly in pointers

### 2. Reference Counting Rules
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

**Note:** ASSIGN is typically used for heap objects (CljObject*), not immediate values (CljValue). For immediate values, direct assignment is sufficient since they don't require reference counting.

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
5. **Handles NULL safely** in both `var` and `new_obj` parameters

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

**NULL to NULL Assignment:**
```c
CljObject *obj = NULL;
ASSIGN(obj, NULL);  // Safe no-op - no operations performed
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

#### Immediate Value Functions
```c
CljValue make_fixnum(int32_t value);
CljValue make_char(uint32_t codepoint);
CljValue make_special(uint8_t special);
CljValue make_fixed(float value);
```
- **Returns**: Immediate value (no memory management needed)
- **Memory**: No heap allocation, no reference counting
- **Usage**: No `release()` needed - values are stored directly in pointers

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

#### Immediate Values (no memory management):
```c
// Immediate values - no memory management needed
CljValue num = make_fixnum(42);        // No RELEASE() needed
CljValue ch = make_char('A');          // No RELEASE() needed
CljValue flag = make_special(SPECIAL_TRUE); // No RELEASE() needed

// Mixed usage - check type before releasing
CljValue value = get_some_value();
if (!is_immediate(value)) {
    RELEASE((CljObject*)value);  // Only release heap objects
}
```

## Best Practices Summary

1. **API Functions**: Return autoreleased objects (no manual cleanup)
2. **Object Creation**: Return objects with `rc=1` (manual cleanup required)
3. **Immediate Values**: No memory management needed - stored directly in pointers
4. **Tests**: Use `AUTORELEASE()` for convenience and readability
5. **Production**: Use `RELEASE()` for performance
6. **Non-return values**: Use `RELEASE()` when object is not returned as function result
7. **Data Structures**: Use `RETAIN()` when storing objects
8. **Type Checking**: Always check `is_immediate()` before releasing values
9. **Profiling**: Always track memory usage in tests
10. **Debugging**: Use memory profiler to find leaks
11. **Trust API Design**: Follow documented memory policy

## Implementation Notes

- Memory profiling is **DEBUG-only** (zero overhead in release builds)
- Hook-based system allows clean separation of profiling from business logic
- Vector elements are automatically freed by `release_object_deep()`
- Singletons (empty vectors/lists) skip reference counting
- **Immediate values** (fixnums, chars, booleans, nil, fixed-point) are not reference counted
- **32-bit tagged pointers** store immediate values directly without heap allocation
- **Type checking** via `is_immediate()` determines if value needs memory management

## Autorelease Pool Management

### Critical Rule: Balanced Push/Pop Operations

**`autorelease_pool_pop()` on empty stack** indicates **unbalanced autorelease pool operations**, not too many pools. This happens when:

1. **Missing `WITH_AUTORELEASE_POOL` wrappers** - Code uses `AUTORELEASE` without a pool
2. **Early returns** - Code jumps out of pool with `return` statements  
3. **Exception handling** - Code jumps out of pool with exceptions

### ❌ Common Unbalanced Pool Patterns

```c
// WRONG: AUTORELEASE without pool
CljValue result = parse("42", st);  // parse() uses AUTORELEASE internally
AUTORELEASE(result);  // ❌ ERROR: No WITH_AUTORELEASE_POOL wrapper!

// WRONG: Early return from pool
WITH_AUTORELEASE_POOL({
    if (error) {
        return;  // ❌ ERROR: Jumps out of pool, never popped!
    }
    // Pool never gets popped
});

// WRONG: Exception from pool
WITH_AUTORELEASE_POOL({
    throw_exception("Error");  // ❌ ERROR: Jumps out of pool, never popped!
    // Pool never gets popped
});
```

### ✅ Correct Pool Management

```c
// CORRECT: Pool wraps all AUTORELEASE usage
WITH_AUTORELEASE_POOL({
    CljValue result = parse("42", st);
    AUTORELEASE(result);
    // Pool automatically popped at end
});

// CORRECT: Exception handling with pool
WITH_AUTORELEASE_POOL_TRY_CATCH({
    CljValue result = parse("42", st);
    AUTORELEASE(result);
}, {
    // Exception handler - pool still gets popped
});

// CORRECT: No early returns from pool
WITH_AUTORELEASE_POOL({
    CljValue result = NULL;
    if (!error) {
        result = parse("42", st);
        AUTORELEASE(result);
    }
    // Pool automatically popped at end
});
```

### Debugging Unbalanced Pools

When you see `autorelease_pool_pop() called on empty stack` warnings:

1. **Check for missing `WITH_AUTORELEASE_POOL`** around code that uses `AUTORELEASE`
2. **Look for early returns** that jump out of pool scope
3. **Check for exceptions** that jump out of pool scope
4. **Verify pool nesting** - ensure every `push` has a corresponding `pop`

### Memory Policy Impact

- **Unbalanced pools** cause memory leaks (objects never freed)
- **Missing pools** cause crashes (AUTORELEASE called without active pool)
- **Early returns** prevent proper cleanup
- **Exception handling** must use `WITH_AUTORELEASE_POOL_TRY_CATCH`

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

// WRONG: Trying to release immediate values
CljValue num = make_fixnum(42);
RELEASE((CljObject*)num);  // ❌ CRASH: Immediate values are not heap objects

// WRONG: Not checking if value is immediate before releasing
CljValue value = get_some_value();
RELEASE((CljObject*)value);  // ❌ CRASH: May be immediate value
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

// CORRECT: Immediate values need no memory management
CljValue num = make_fixnum(42);        // ✅ SAFE: No RELEASE() needed
CljValue ch = make_char('A');          // ✅ SAFE: No RELEASE() needed
CljValue flag = make_special(SPECIAL_TRUE); // ✅ SAFE: No RELEASE() needed

// CORRECT: Check if value is immediate before releasing
CljValue value = get_some_value();
if (!is_immediate(value)) {
    RELEASE((CljObject*)value);  // ✅ SAFE: Only release heap objects
}
```

### Memory Policy Verification
- **Always check Doxygen documentation** for memory policy
- **Use memory profiling** to validate assumptions
- **Test-First development** prevents incorrect implementations
- **Check `is_immediate()`** before releasing any value
- **Trust existing API design** unless proven otherwise
