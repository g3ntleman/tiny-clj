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

#### Balance Rule
**Every `RETAIN()` or `make_*()` call must be balanced with a `RELEASE()` or `AUTORELEASE()` call:**
- Objects created with `make_*()` have `rc=1` and must be released or autoreleased
- Objects retained with `RETAIN()` must be released or autoreleased
- This ensures proper memory management and prevents leaks

### 2. Memory Management Patterns

**Important:** The `TEST()` macro already creates an autorelease pool automatically. You do **NOT** need to wrap test code with `WITH_AUTORELEASE_POOL`.

#### Test Code Pattern (Comfort & Readability)
```c
// ✅ CORRECT: TEST macro provides autorelease pool automatically
TEST(test_something) {
    ID vec = AUTORELEASE(make_vector(3, 1));
    ID list = AUTORELEASE(make_list());
    // Objects automatically freed - no manual cleanup needed
}

// ❌ REDUNDANT: Don't wrap TEST code with WITH_AUTORELEASE_POOL
TEST(test_something) {
    WITH_AUTORELEASE_POOL({  // ❌ UNNECESSARY: TEST already provides pool
        ID vec = AUTORELEASE(make_vector(3, 1));
        ID list = AUTORELEASE(make_list());
    });
}
```

#### Production Code Pattern (Performance)
```c
// Use explicit RELEASE() for performance-critical code
ID vec = make_vector(3, 1);
ID list = make_list();
// ... use objects ...
RELEASE(vec);
RELEASE(list);
```

**When to use:**
- **`AUTORELEASE()`**: Tests, debugging, prototyping, non-performance-critical code
- **`RELEASE()`**: Production, performance-critical code, real-time systems, objects not returned as values

#### Eval path
Evaluation (eval_let, eval_ast_call, eval_function_call_from_vector, etc.) **returns autoreleased references** for heap objects. Callers must **not** release these; the autorelease pool cleans them up. Immediates are returned as-is.

### 3. Object Lifecycle Rules

**Objects that permanently store references to other objects must call `RETAIN()` on those objects and use `ASSIGN()` for updates:**

```c
// ✅ CORRECT: Use RETAIN when storing objects permanently
data->items[i] = obj;
RETAIN(obj);  // Increment ref count for the data structure's ownership

// ✅ CORRECT: Use ASSIGN for safe updates (handles RETAIN/RELEASE automatically)
ASSIGN(data->items[i], new_obj);  // Old value released, new value retained automatically
```

### 4. Common Anti-Patterns

```c
// ❌ WRONG: Double release, forgetting to release, using AUTORELEASE in production loops
ID obj = make_vector(3, 1);
RELEASE(obj);
RELEASE(obj);  // ❌ CRASH: Double free

// ✅ CORRECT: Balance every make_*() with RELEASE() or AUTORELEASE()
ID obj = make_vector(3, 1);
RELEASE(obj);  // Or AUTORELEASE(obj) in tests
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

**These macros are NULL-safe** (and safe for immediate values); no explicit checks are needed before calling them.

This ensures consistent memory profiling and better tracking of all memory operations throughout the codebase.

### RETAIN/RELEASE/AUTORELEASE Macros - NULL and Immediate Value Safety

**Important:** The `RETAIN()`, `RELEASE()`, and `AUTORELEASE()` macros **do NOT require NULL checks or immediate value checks**. They safely handle NULL values and immediate values automatically.

```c
// ✅ CORRECT: No checks needed
ID obj = NULL;
CljValue num = make_fixnum(42);
RETAIN(obj);      // Safe - handles NULL automatically
RELEASE(num);     // Safe - handles immediate values automatically
AUTORELEASE(obj); // Safe - handles NULL automatically
```

### RETAIN/RELEASE Macros Return Values

**Important:** The `RETAIN()` and `RELEASE()` macros **return the object** for fluent usage:

```c
// ✅ CORRECT: Compact and fluent
return RETAIN(v->data[i]);  // Returns retained object

// ✅ CORRECT: Fluent chaining
v->data[i] = (RETAIN(val), val);
```

### ASSIGN Macro for Safe Object Assignment

The `ASSIGN(var, new_obj)` macro provides safe object assignment following the classic Objective-C pattern. It automatically handles RETAIN/RELEASE operations and NULL checks.

**Note:** ASSIGN is typically used for heap objects (CljObject*), not immediate values (CljValue).

```c
// ✅ CORRECT: ASSIGN handles everything automatically
ASSIGN(obj, new_obj);  // Old value released, new value retained
ASSIGN(obj, NULL);     // Safely releases obj and sets to NULL

// ❌ WRONG: Manual pattern (error-prone)
if (old_obj) RELEASE(old_obj);
if (new_obj) RETAIN(new_obj);
obj = new_obj;
```

## Function Return Value Memory Policy

**Critical Principle:** Functions that return objects are responsible for ensuring the caller can safely use the pointer until the enclosing autorelease pool is closed.

**Rules:**
1. **Objects created with `make_*()`** have `rc=1` and must be autoreleased before returning
2. **Objects from other functions** are already safe to use - just return them
3. **Only use AUTORELEASE** when you created the object yourself with `make_*()`

```c
// ✅ CORRECT: We created it, so we use AUTORELEASE
ID my_function() {
    ID obj = make_vector(10, 0);  // We created it
    return AUTORELEASE(obj);  // Transfer ownership to caller's pool
}

// ✅ CORRECT: We didn't create it, just return it
ID eval_symbol(CljSymbol *symbol, EvalState *st) {
    ID value = ns_resolve(st, symbol);  // We didn't create this
    return value;  // No AUTORELEASE needed - already safe
}
```

**Pattern: AUTORELEASE(RETAIN(obj))**

Use when there's a risk that the last strong reference might be lost:

```c
// ✅ CORRECT: Protect against losing the last reference
ID safe_return(ID obj) {
    return AUTORELEASE(RETAIN(obj));  // Ensures object survives until pool closes
}
```

## API Memory Policy

**Function Categories:**
- **Parse/Eval Functions** (`parse_string`, `eval_parsed`, etc.): Return autoreleased objects - no manual cleanup needed
- **Object Creation** (`make_vector`, `make_list`, etc.): Return objects with `rc=1` - must be released or autoreleased
- **Immediate Values** (`make_fixnum`, `make_char`, etc.): No memory management needed

**Invariant:** All `make_*` functions return a valid object or throw an exception (never a silent NULL). Exception: `make_seq(obj)` returns NULL for nil/empty (Clojure semantics); every **non-NULL** return is caller-owned (new with rc=1, or RETAIN when obj already CLJ_SEQ) and must be released or autoreleased.

```c
// ✅ CORRECT: API functions return autoreleased objects
ID result = eval_string(expr, eval_state);
// No manual cleanup needed

// ✅ CORRECT: make_*() requires manual management
ID vec = make_vector(10, 1);
RELEASE(vec);  // Or AUTORELEASE(vec) in tests

// ✅ CORRECT: Immediate values need no management
CljValue num = make_fixnum(42);  // No RELEASE() needed
```

## `_inplace` Functions for COW Optimizations

**Problem**: Functions like `map_assoc()`, `vector_conj()`, etc. return `AUTORELEASE(obj)`. When the caller uses `ASSIGN()` or `RETAIN()`, the reference count increases and COW optimizations (`rc == 1` check) fail.

**Solution**: Use `_inplace` functions for long-lived variables to maintain `rc=1` for optimal COW behavior.

### Available `_inplace` Functions

**Maps:**
- `map_assoc_inplace(CljPersistentMap **map_slot, ID key, ID value)`
- `map_remove_inplace(CljPersistentMap **map_slot, ID key)`

**Vectors:**
- `vector_conj_inplace(CljVector **vec_slot, ID item)`
- `vector_assoc_inplace(CljVector **vec_slot, unsigned int index, ID value)`
- `vector_insert_at_inplace(CljVector **vec_slot, unsigned int index, ID item)`
- `vector_remove_at_inplace(CljVector **vec_slot, unsigned int index)`
- `vector_pop_inplace(CljVector **vec_slot)`

### Usage Pattern

**Before (Problem - RC increases):**
```c
CljVector *vec = make_vector(10, CLJ_VECTOR_PERSISTENT);
ASSIGN(vec, vector_conj(vec, item));  // rc becomes 2 → COW fails
```

**After (Solution - RC stays 1):**
```c
CljVector *vec = make_vector(10, CLJ_VECTOR_PERSISTENT);
vector_conj_inplace(&vec, item);  // rc stays 1 → COW works!
```

### Best Practices

1. **For long-lived variables**: Use `_inplace` functions
   ```c
   CljVector *stack = make_vector(100, CLJ_VECTOR_PERSISTENT);
   vector_conj_inplace(&stack, item1);  // rc stays 1
   vector_conj_inplace(&stack, item2);  // rc stays 1, COW works
   vector_pop_inplace(&stack);  // Removes item2, rc stays 1
   ```

2. **In loops**: `_inplace` for performance
   ```c
   CljVector *result = make_vector(1000, CLJ_VECTOR_PERSISTENT);
   for (int i = 0; i < 1000; i++) {
       vector_conj_inplace(&result, item(i));  // rc stays 1, in-place possible
   }
   ```

3. **Builtin functions**: Use `_inplace` for internal operations
   - `native_subvec()` uses `vector_conj_inplace()` for better COW behavior

### Memory Management

`_inplace` functions automatically handle memory:
- If a new object is created (COW), the old object is `RELEASE()`d
- The pointer in `*slot` is updated to the new object
- `rc` remains 1 for optimal COW behavior

## Best Practices Summary

1. **Balance Rule**: Every `RETAIN()` or `make_*()` call must be balanced with `RELEASE()` or `AUTORELEASE()`
2. **Data Structures**: Use `RETAIN()` when storing objects, `ASSIGN()` for updates
3. **Return Values**: Objects created with `make_*()` must be autoreleased before returning
4. **Tests**: Use `AUTORELEASE()` for convenience
5. **Production**: Use `RELEASE()` for performance
6. **API Functions**: Trust that return values are safe to use until pool closes
7. **Macros**: Always use `RETAIN()`, `RELEASE()`, `AUTORELEASE()`, `ASSIGN()` instead of direct function calls
8. **COW Optimizations**: Use `_inplace` functions for long-lived variables to maintain `rc=1`

## Autorelease Pool Management

**Important:** `WITH_AUTORELEASE_POOL` provides exception-safe memory cleanup. The same object can appear multiple times in a pool - this is normal.

### Balanced Pool Usage

**Unbalanced use** (AUTORELEASE without pool, early return or throw from `WITH_AUTORELEASE_POOL`):
- Missing `WITH_AUTORELEASE_POOL` wrappers
- Early returns from pool scope
- Exceptions jumping out of pool scope

`autorelease_pool_drain_to_depth(d)` when depth is already ≤ d is a no-op.

```c
// ❌ WRONG: AUTORELEASE without pool, early return, exception from pool
AUTORELEASE(obj);  // ❌ ERROR: No pool!
WITH_AUTORELEASE_POOL({
    if (error) return;  // ❌ ERROR: Jumps out of pool!
    throw_exception("Error");  // ❌ ERROR: Jumps out of pool!
});

// ✅ CORRECT: Pool wraps all AUTORELEASE usage
WITH_AUTORELEASE_POOL({
    CljValue result = parse("42", st);
    AUTORELEASE(result);
    // Pool automatically drained at end
});
```

### Transferring Objects Between Pools

**Use RETAIN before pool drain, then AUTORELEASE for outer pool:**

```c
CljValue parse_from_reader(Reader *reader, EvalState *st) {
  CljValue result = NULL;
  WITH_AUTORELEASE_POOL({
    result = value_by_parsing_expr(reader, st);
    if (result && !IS_IMMEDIATE(result)) {
      RETAIN(result);  // Prevents inner pool from freeing it
    }
  });  // Pool popped, but rc > 0, so object survives
  return AUTORELEASE(result);  // Transfer to outer pool
}
```

## Common Mistakes

```c
// ❌ WRONG: Unnecessary RELEASE on autoreleased objects
ID parsed = parse_string(expr, eval_state);
RELEASE(parsed);  // ❌ UNNECESSARY: parse_string returns autoreleased object

// ❌ WRONG: Manual retain/release assignment (error-prone)
if (obj) RELEASE(obj);
if (new_obj) RETAIN(new_obj);
obj = new_obj;

// ✅ CORRECT: Trust the API design, use ASSIGN macro
ID parsed = parse_string(expr, eval_state);
// No manual cleanup needed - autoreleased

ASSIGN(obj, new_obj);  // ✅ SAFE: Handles retain/release automatically
```
