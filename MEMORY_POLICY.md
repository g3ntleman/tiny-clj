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

#### Test Code Pattern (Comfort)
```c
// Use autorelease() for cleaner, less verbose test code
CljObject *vec = autorelease(make_vector(3, 1));
CljObject *list = autorelease(make_list());
// Objects automatically freed at test end
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

#### Use `release()` in Production:
- ✅ **Performance-critical code**: No autorelease pool overhead
- ✅ **Real-time systems**: Predictable memory timing
- ✅ **Long-running processes**: Explicit control over object lifetime
- ✅ **Memory-constrained environments**: Immediate cleanup

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
// For temporary objects in tests:
CljObject *temp = autorelease(make_list());
// Automatically cleaned up
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

// Test pattern with autorelease
CljObject *obj = autorelease(make_vector(3, 1));

// Production loop pattern
for (int i = 0; i < 1000000; i++) {
    CljObject *temp = make_vector(10, 1);
    // ... use temp ...
    release(temp);  // ✅ FAST: Immediate cleanup
}
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

## Best Practices Summary

1. **Tests**: Use `autorelease()` for convenience
2. **Production**: Use `release()` for performance
3. **Data Structures**: Use `retain()` when storing objects
4. **Functions**: Return objects with caller ownership
5. **Profiling**: Always track memory usage in tests
6. **Debugging**: Use memory profiler to find leaks

## Implementation Notes

- Memory profiling is **DEBUG-only** (zero overhead in release builds)
- Hook-based system allows clean separation of profiling from business logic
- Vector elements are automatically freed by `release_object_deep()`
- Singletons (empty vectors/lists) skip reference counting
- Primitive types (int, float) are not reference counted
