/*
 * Memory Profiler for Tiny-CLJ
 * 
 * Tracks object allocation and deallocation for heap analysis.
 * Only active in DEBUG builds - zero overhead in RELEASE builds.
 */

#ifndef TINY_CLJ_MEMORY_PROFILER_H
#define TINY_CLJ_MEMORY_PROFILER_H

#include "CljObject.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// MEMORY PROFILING (DEBUG ONLY)
// ============================================================================

// Always declare functions (implementation depends on DEBUG)

// Memory statistics structure
typedef struct {
    size_t total_allocations;     // Total number of malloc calls
    size_t total_deallocations;   // Total number of free calls
    size_t peak_memory_usage;     // Peak memory usage in bytes
    size_t current_memory_usage;  // Current memory usage in bytes
    size_t object_creations;      // CljObject creations
    size_t object_destructions;   // CljObject destructions
    size_t retain_calls;          // retain() calls
    size_t release_calls;         // release() calls
    size_t autorelease_calls;     // autorelease() calls
    size_t memory_leaks;          // Potential memory leaks (allocations - deallocations)
} MemoryStats;

// Global memory statistics
extern MemoryStats g_memory_stats;

// Memory profiling functions
void memory_profiler_init(void);
void memory_profiler_reset(void);
void memory_profiler_cleanup(void);
MemoryStats memory_profiler_get_stats(void);
void memory_profiler_print_stats(const char *test_name);

// Memory tracking functions
void memory_profiler_track_allocation(size_t size);
void memory_profiler_track_deallocation(size_t size);
void memory_profiler_track_object_creation(CljObject *obj);
void memory_profiler_track_object_destruction(CljObject *obj);
void memory_profiler_track_retain(CljObject *obj);
void memory_profiler_track_release(CljObject *obj);
void memory_profiler_track_autorelease(CljObject *obj);

// Memory leak detection
void memory_profiler_check_leaks(const char *location);
bool memory_profiler_has_leaks(void);

// Helper functions for test comparisons
MemoryStats memory_profiler_diff_stats(const MemoryStats *after, const MemoryStats *before);
void memory_profiler_print_diff(MemoryStats diff, const char *test_name);

#ifdef DEBUG
// Enable memory profiling in debug builds

// Memory usage macros
#define MEMORY_PROFILER_INIT() memory_profiler_init()
#define MEMORY_PROFILER_RESET() memory_profiler_reset()
#define MEMORY_PROFILER_CLEANUP() memory_profiler_cleanup()
#define MEMORY_PROFILER_PRINT_STATS(test_name) memory_profiler_print_stats(test_name)
#define MEMORY_PROFILER_CHECK_LEAKS(location) memory_profiler_check_leaks(location)

// Object tracking macros
#define MEMORY_PROFILER_TRACK_ALLOCATION(size) memory_profiler_track_allocation(size)
#define MEMORY_PROFILER_TRACK_DEALLOCATION(size) memory_profiler_track_deallocation(size)
#define MEMORY_PROFILER_TRACK_OBJECT_CREATION(obj) memory_profiler_track_object_creation(obj)
#define MEMORY_PROFILER_TRACK_OBJECT_DESTRUCTION(obj) memory_profiler_track_object_destruction(obj)
#define MEMORY_PROFILER_TRACK_RETAIN(obj) memory_profiler_track_retain(obj)
#define MEMORY_PROFILER_TRACK_RELEASE(obj) memory_profiler_track_release(obj)
#define MEMORY_PROFILER_TRACK_AUTORELEASE(obj) memory_profiler_track_autorelease(obj)

// Memory comparison macros for tests
#define MEMORY_PROFILER_COMPARE_STATS(before, after, test_name) do { \
    MemoryStats diff = memory_profiler_diff_stats(&(after), &(before)); \
    memory_profiler_print_diff(diff, test_name); \
} while(0)

#else
// No-op macros for release builds (zero overhead)

#define MEMORY_PROFILER_INIT() ((void)0)
#define MEMORY_PROFILER_RESET() ((void)0)
#define MEMORY_PROFILER_CLEANUP() ((void)0)
#define MEMORY_PROFILER_PRINT_STATS(test_name) ((void)0)
#define MEMORY_PROFILER_CHECK_LEAKS(location) ((void)0)

#define MEMORY_PROFILER_TRACK_ALLOCATION(size) ((void)0)
#define MEMORY_PROFILER_TRACK_DEALLOCATION(size) ((void)0)
#define MEMORY_PROFILER_TRACK_OBJECT_CREATION(obj) ((void)0)
#define MEMORY_PROFILER_TRACK_OBJECT_DESTRUCTION(obj) ((void)0)
#define MEMORY_PROFILER_TRACK_RETAIN(obj) ((void)0)
#define MEMORY_PROFILER_TRACK_RELEASE(obj) ((void)0)
#define MEMORY_PROFILER_TRACK_AUTORELEASE(obj) ((void)0)

#define MEMORY_PROFILER_COMPARE_STATS(before, after, test_name) ((void)0)

#endif // DEBUG

// ============================================================================
// MEMORY PROFILING FOR TESTS
// ============================================================================

#ifdef DEBUG
// Test memory profiling macros
#define MEMORY_TEST_START(test_name) do { \
    MEMORY_PROFILER_RESET(); \
    printf("🔍 Memory Profiling: %s\n", test_name); \
} while(0)

#define MEMORY_TEST_END(test_name) do { \
    MEMORY_PROFILER_PRINT_STATS(test_name); \
    MEMORY_PROFILER_CHECK_LEAKS(test_name); \
} while(0)

#define MEMORY_TEST_BENCHMARK_START(test_name) do { \
    MemoryStats before = memory_profiler_get_stats(); \
    printf("🔍 Memory Benchmark: %s\n", test_name); \
} while(0)

#define MEMORY_TEST_BENCHMARK_END(test_name) do { \
    MemoryStats after = memory_profiler_get_stats(); \
    MEMORY_PROFILER_COMPARE_STATS(before, after, test_name); \
} while(0)

#else
// No-op macros for release builds
#define MEMORY_TEST_START(test_name) ((void)0)
#define MEMORY_TEST_END(test_name) ((void)0)
#define MEMORY_TEST_BENCHMARK_START(test_name) ((void)0)
#define MEMORY_TEST_BENCHMARK_END(test_name) ((void)0)

#endif // DEBUG

#ifdef __cplusplus
}
#endif

#endif /* TINY_CLJ_MEMORY_PROFILER_H */
