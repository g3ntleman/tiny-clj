# Memory Profiler Documentation

## Overview

The Memory Profiler is a comprehensive debugging tool for Tiny-CLJ that tracks object allocation, deallocation, and memory usage patterns. It provides detailed statistics and leak detection capabilities, but **only in DEBUG builds** - it has zero overhead in RELEASE builds.

## Files

- **`src/memory_profiler.h`** - Header file with declarations and macros
- **`src/memory_profiler.c`** - Implementation of memory profiling functionality

## Key Features

### 🔍 **Memory Tracking**
- **Object Allocations/Deallocations** - Tracks all `malloc()`/`free()` calls
- **CljObject Lifecycle** - Monitors object creation and destruction
- **Reference Counting** - Tracks `retain()` and `release()` operations
- **Autorelease Pools** - Monitors `autorelease()` operations
- **Memory Leak Detection** - Identifies potential memory leaks

### 📊 **Statistics**
- **Peak Memory Usage** - Maximum memory consumption
- **Current Memory Usage** - Real-time memory consumption
- **Object Type Breakdown** - Statistics per CljObject type
- **Retention Ratios** - Reference counting efficiency metrics

### 🧪 **Test Integration**
- **Test Memory Profiling** - `WITH_AUTORELEASE_POOL` macro
- **Benchmark Support** - Memory usage comparison between tests
- **Leak Detection** - Automatic leak detection in tests

## Core Data Structures

### MemoryStats Structure

```c
typedef struct {
    size_t total_allocations;     // Total malloc() calls
    size_t total_deallocations;   // Total free() calls
    size_t peak_memory_usage;     // Peak memory in bytes
    size_t current_memory_usage;  // Current memory in bytes
    size_t object_creations;      // CljObject creations
    size_t object_destructions;   // CljObject destructions
    size_t retain_calls;          // retain() calls
    size_t release_calls;         // release() calls
    size_t autorelease_calls;     // autorelease() calls
    size_t memory_leaks;          // Potential memory leaks
    
    // Object type breakdown
    size_t allocations_by_type[CLJ_TYPE_COUNT];   // Per-type allocations
    size_t deallocations_by_type[CLJ_TYPE_COUNT]; // Per-type deallocations
} MemoryStats;
```

## API Reference

### Core Functions

#### Initialization and Control
```c
void memory_profiler_init(void);                    // Initialize profiler
void memory_profiler_reset(void);                   // Reset statistics
void memory_profiler_cleanup(void);                 // Cleanup and final report
void enable_memory_profiling(bool enabled);         // Enable/disable profiling
bool is_memory_profiling_enabled(void);            // Check if profiling is enabled
```

#### Statistics
```c
MemoryStats memory_profiler_get_stats(void);       // Get current statistics
void memory_profiler_print_stats(const char *test_name); // Print formatted stats
```

#### Memory Tracking
```c
void memory_profiler_track_allocation(size_t size);           // Track malloc()
void memory_profiler_track_deallocation(size_t size);         // Track free()
void memory_profiler_track_object_creation(CljObject *obj);   // Track object creation
void memory_profiler_track_object_destruction(CljObject *obj); // Track object destruction
void memory_profiler_track_retain(CljObject *obj);            // Track retain()
void memory_profiler_track_release(CljObject *obj);           // Track release()
void memory_profiler_track_autorelease(CljObject *obj);      // Track autorelease()
```

#### Leak Detection
```c
void memory_profiler_check_leaks(const char *location);       // Check for leaks
bool memory_profiler_has_leaks(void);                         // Check if leaks exist
```

### Macros (DEBUG builds only)

#### Basic Profiling
```c
MEMORY_PROFILER_INIT()                    // Initialize profiler
MEMORY_PROFILER_RESET()                   // Reset statistics
MEMORY_PROFILER_CLEANUP()                 // Cleanup profiler
MEMORY_PROFILER_PRINT_STATS(test_name)    // Print statistics
MEMORY_PROFILER_CHECK_LEAKS(location)     // Check for leaks
```

#### Object Tracking
```c
MEMORY_PROFILER_TRACK_ALLOCATION(size)        // Track allocation
MEMORY_PROFILER_TRACK_DEALLOCATION(size)       // Track deallocation
MEMORY_PROFILER_TRACK_OBJECT_CREATION(obj)     // Track object creation
MEMORY_PROFILER_TRACK_OBJECT_DESTRUCTION(obj) // Track object destruction
MEMORY_PROFILER_TRACK_RETAIN(obj)              // Track retain
MEMORY_PROFILER_TRACK_RELEASE(obj)             // Track release
MEMORY_PROFILER_TRACK_AUTORELEASE(obj)         // Track autorelease
```

#### Test Integration
```c
MEMORY_TEST_START(test_name)              // Start test profiling
MEMORY_TEST_END(test_name)                // End test profiling
MEMORY_TEST_BENCHMARK_START(test_name)    // Start benchmark
MEMORY_TEST_BENCHMARK_END(test_name)      // End benchmark
```

## Usage Examples

### Basic Memory Profiling

```c
#include "memory_profiler.h"

void my_function(void) {
    // Initialize profiling
    MEMORY_PROFILER_INIT();
    
    // Your code here
    CljObject *obj = make_int(42);
    // ... use object ...
    RELEASE(obj);
    
    // Print statistics
    MEMORY_PROFILER_PRINT_STATS("my_function");
    
    // Check for leaks
    MEMORY_PROFILER_CHECK_LEAKS("my_function");
    
    // Cleanup
    MEMORY_PROFILER_CLEANUP();
}
```

### Test Memory Profiling

```c
#include "memory.h"  // Includes WITH_AUTORELEASE_POOL

static char *test_my_function(void) {
    WITH_AUTORELEASE_POOL({
        // Test code here
        CljObject *obj = AUTORELEASE(make_int(42));
        mu_assert("object created", obj != NULL);
        // Objects automatically released when pool pops
    });
    return 0;
}
```

### Benchmark Comparison

```c
void benchmark_function(void) {
    MEMORY_TEST_BENCHMARK_START("benchmark");
    
    // First operation
    CljObject *obj1 = make_int(100);
    RELEASE(obj1);
    
    // Second operation  
    CljObject *obj2 = make_int(200);
    RELEASE(obj2);
    
    MEMORY_TEST_BENCHMARK_END("benchmark");
}
```

## Output Format

### Statistics Table
```
📊 Memory Statistics for test_name:
  ┌─────────────────────────────────────────────────────────┐
  │ Memory Operations                                       │
  ├─────────────────────────────────────────────────────────┤
  │ Allocations:               3                                │
  │ Deallocations:             3                                │
  │ Peak Memory:              48 bytes                          │
  │ Current Memory:            0 bytes                          │
  │ Memory Leaks:              0                                │
  ├─────────────────────────────────────────────────────────┤
  │ CljObject Operations                                    │
  ├─────────────────────────────────────────────────────────┤
  │ Object Creations:          3                                │
  │ Object Destructions:        3                              │
  │ retain() calls:            0                                │
  │ release() calls:           3                                │
  │ autorelease() calls:       4                              │
  └─────────────────────────────────────────────────────────┘
```

### Object Type Breakdown
```
📋 Object Type Breakdown:
  ┌─────────────────────────────────────────────────────────┐
  │ Type                │ Allocations │ Deallocations │ Leaks │
  ├─────────────────────────────────────────────────────────┤
  │ INT                │          1 │            1 │     0 │
  │ FLOAT              │          1 │            1 │     0 │
  │ VECTOR             │          1 │            0 │     1 │
  ├─────────────────────────────────────────────────────────┤
  │ TOTAL              │          3 │            3 │     1 │
  └─────────────────────────────────────────────────────────┘
```

### Efficiency Metrics
```
📈 Retention Ratio: 0.00 (retain calls per object)
📈 Deallocation Ratio: 1.00 (deallocations per allocation)
✅ Perfect Memory Management: All allocations freed
✅ Memory Clean at test_name: All allocations freed
```

## Important Notes

### ⚠️ **VECTOR "Leak" in AUTORELEASE Tests**

In AUTORELEASE tests, you may see:
```
│ VECTOR             │          1 │            0 │     1 │
```

This is **NOT a real memory leak**! It's the **Autorelease Pool itself** that gets freed **after** the statistics are printed. This is a **timing artifact** of the profiling system.

### 🔧 **Debug vs Release Builds**

- **DEBUG builds**: Full memory profiling enabled
- **RELEASE builds**: All macros are no-ops (zero overhead)

### 🧪 **Test Integration**

The Memory Profiler integrates seamlessly with the test framework:

```c
// In test files
WITH_AUTORELEASE_POOL({
    // Test code
    CljObject *obj = AUTORELEASE(make_int(42));
    mu_assert("test passed", obj != NULL);
    // Automatic cleanup
});
```

### 📊 **Memory Leak Detection**

The profiler automatically detects potential memory leaks:
- **Allocations > Deallocations** = Potential leak
- **Object Creations > Object Destructions** = Potential leak
- **Peak Memory > 0** after cleanup = Potential leak

## Integration with Memory Hooks

The Memory Profiler works in conjunction with `memory_hooks.c` to automatically track memory operations:

```c
// Automatic tracking via hooks
void memory_profiling_init_with_hooks(void) {
    memory_profiler_init();
    memory_hooks_register(memory_profiler_hook);
}

void memory_profiling_cleanup_with_hooks(void) {
    memory_hooks_unregister();
    memory_profiler_cleanup();
}
```

## Performance Impact

- **DEBUG builds**: Minimal overhead for comprehensive tracking
- **RELEASE builds**: Zero overhead (all macros are no-ops)
- **Memory usage**: ~1KB for statistics storage
- **CPU overhead**: <1% for typical workloads

## Troubleshooting

### Common Issues

1. **"VECTOR Leak" in AUTORELEASE tests**
   - **Solution**: This is normal - it's the Autorelease Pool itself
   - **Note**: No action needed, it's a timing artifact

2. **Memory leaks not detected**
   - **Check**: Ensure profiling is enabled in DEBUG builds
   - **Verify**: Use `MEMORY_PROFILER_CHECK_LEAKS()` after operations

3. **Statistics not updating**
   - **Check**: Ensure `MEMORY_PROFILER_INIT()` is called
   - **Verify**: Memory hooks are properly registered

### Debug Tips

```c
// Enable detailed logging
enable_memory_profiling(true);

// Check if profiling is active
if (is_memory_profiling_enabled()) {
    printf("Memory profiling is active\n");
}

// Get current statistics
MemoryStats stats = memory_profiler_get_stats();
printf("Current allocations: %zu\n", stats.total_allocations);
```

## Future Enhancements

- **Memory usage graphs** - Visual representation of memory usage over time
- **Leak stack traces** - Track where leaked objects were allocated
- **Memory pressure detection** - Alert when memory usage is high
- **Custom allocators** - Support for different memory allocation strategies
