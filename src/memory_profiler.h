/*
 * Memory Profiler for Tiny-CLJ
 * 
 * Comprehensive memory tracking and profiling system for heap analysis.
 * Provides detailed statistics on object allocation, deallocation, and memory usage.
 * 
 * Features:
 * - Object allocation/deallocation tracking
 * - Memory leak detection
 * - Peak memory usage monitoring
 * - Object type breakdown
 * - Reference counting operations tracking
 * - Test-specific memory profiling
 * 
 * Usage:
 * - DEBUG builds: Full profiling enabled with detailed statistics
 * - RELEASE builds: Zero overhead (all macros are no-ops)
 * 
 * Test Integration:
 * - WITH_MEMORY_PROFILING() macro for automatic test profiling (recommended)
 * - MEMORY_TEST_START/END for manual test profiling (legacy)
 * - Detailed statistics output with leak detection
 * 
 * Memory Statistics:
 * - Total allocations/deallocations
 * - Peak and current memory usage
 * - Object creation/destruction counts
 * - Retain/release/autorelease operation counts
 * - Per-type allocation breakdown
 * - Memory leak detection and reporting
 * 
 * @author Tiny-CLJ Team
 * @version 1.0
 * @since 2024
 */

#ifndef TINY_CLJ_MEMORY_PROFILER_H
#define TINY_CLJ_MEMORY_PROFILER_H

#include "object.h"
#include <stdbool.h>

// Forward declaration to avoid circular include
typedef enum {
    MEMORY_HOOK_DEALLOCATION,
    MEMORY_HOOK_RETAIN,
    MEMORY_HOOK_RELEASE,
    MEMORY_HOOK_AUTORELEASE
} MemoryHookType;

// ============================================================================
// MEMORY HOOKS DEFINITIONS
// ============================================================================

// Memory operation types are now defined in memory.h

// Hook function type
typedef void (*MemoryHookFunc)(MemoryHookType type, void *ptr, size_t size);

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// MEMORY PROFILING (DEBUG ONLY)
// ============================================================================

// Always declare functions (implementation depends on DEBUG)

/**
 * @brief Memory statistics structure for comprehensive heap analysis
 * 
 * Tracks all memory operations including allocations, deallocations,
 * object lifecycle events, and reference counting operations.
 * 
 * Fields:
 * - total_allocations: Total malloc() calls tracked
 * - total_deallocations: Total free() calls tracked
 * - peak_memory_usage: Maximum memory usage in bytes
 * - current_memory_usage: Current memory usage in bytes
 * - total_allocations: Total object allocation count
 * - object_destructions: CljObject destruction count
 * - retain_calls: retain() operation count
 * - release_calls: release() operation count
 * - autorelease_calls: autorelease() operation count
 * - memory_leaks: Potential memory leaks (allocations - deallocations)
 * - allocations_by_type: Per-type allocation counts
 * - deallocations_by_type: Per-type deallocation counts
 */
typedef struct {
    size_t total_allocations;     // Total number of object allocations
    size_t total_deallocations;   // Total number of object deallocations
    size_t peak_memory_usage;     // Peak memory usage in bytes
    size_t current_memory_usage;  // Current memory usage in bytes
    size_t object_destructions;   // CljObject destructions
    size_t retain_calls;          // retain() calls
    size_t release_calls;         // release() calls
    size_t autorelease_calls;     // autorelease() calls
    size_t memory_leaks;          // Potential memory leaks (allocations - deallocations)
    
    // Object type breakdown
    size_t allocations_by_type[CLJ_TYPE_COUNT];  // Allocations per CljType
    size_t deallocations_by_type[CLJ_TYPE_COUNT]; // Deallocations per CljType
    size_t retains_by_type[CLJ_TYPE_COUNT];    // Retains per CljType
    size_t releases_by_type[CLJ_TYPE_COUNT];   // Releases per CljType
    size_t autoreleases_by_type[CLJ_TYPE_COUNT]; // Autoreleases per CljType
} MemoryStats;

// Global memory statistics
extern MemoryStats g_memory_stats;
extern bool g_memory_verbose_mode;

/**
 * @brief Initialize the memory profiler system
 * 
 * Resets all statistics and prepares the profiler for tracking.
 * Should be called at the start of memory profiling sessions.
 */
// Memory hooks functions
void memory_hooks_init(void);
void memory_hooks_cleanup(void);
void memory_hooks_register(MemoryHookFunc hook);
void memory_hooks_unregister(void);
void memory_hook_trigger(MemoryHookType type, void *ptr, size_t size);

// Memory profiling integration
void memory_profiling_init_with_hooks(void);
void memory_profiling_cleanup_with_hooks(void);

void memory_profiler_init(void);

/**
 * @brief Reset memory statistics to zero
 * 
 * Clears all tracked statistics without disabling profiling.
 * Useful for starting fresh profiling sessions.
 */
void memory_profiler_reset(void);

/**
 * @brief Cleanup memory profiler and report final statistics
 * 
 * Prints final memory statistics and detects potential leaks.
 * Should be called at the end of profiling sessions.
 */
void memory_profiler_cleanup(void);

/**
 * @brief Get current memory statistics
 * @return Current MemoryStats structure
 */
MemoryStats memory_profiler_get_stats(void);

/**
 * @brief Print detailed memory statistics for a test
 * @param test_name Name of the test for context
 */
void memory_profiler_print_stats(const char *test_name);

/**
 * @brief Enable or disable memory profiling
 * @param enabled true to enable, false to disable
 */
void enable_memory_profiling(bool enabled);

/**
 * @brief Check if memory profiling is currently enabled
 * @return true if enabled, false otherwise
 */
bool is_memory_profiling_enabled(void);

/**
 * @brief Set memory verbose mode for test output
 * @param verbose true to show detailed stats for all tests, false to show only errors/leaks
 */
void set_memory_verbose_mode(bool verbose);

/**
 * @brief Track a memory allocation
 * @param size Size of allocated memory in bytes
 */

/**
 * @brief Track a memory deallocation
 * @param size Size of deallocated memory in bytes
 */
void memory_profiler_track_deallocation(size_t size);

/**
 * @brief Track CljObject creation
 * @param obj Pointer to created CljObject
 */
void memory_profiler_track_object_creation(CljObject *obj);

/**
 * @brief Track CljObject destruction
 * @param obj Pointer to destroyed CljObject
 */
void memory_profiler_track_object_destruction(CljObject *obj);

/**
 * @brief Track retain() operation
 * @param obj Pointer to retained CljObject
 */
void memory_profiler_track_retain(CljObject *obj);

/**
 * @brief Track release() operation
 * @param obj Pointer to released CljObject
 */
void memory_profiler_track_release(CljObject *obj);

/**
 * @brief Track autorelease() operation
 * @param obj Pointer to autoreleased CljObject
 */
void memory_profiler_track_autorelease(CljObject *obj);

/**
 * @brief Check for memory leaks at a specific location
 * @param location Description of where the check is performed
 */
void memory_profiler_check_leaks(const char *location);

/**
 * @brief Check if there are any detected memory leaks
 * @return true if leaks detected, false otherwise
 */
bool memory_profiler_has_leaks(void);

/**
 * @brief Calculate difference between two memory statistics
 * @param after Statistics after an operation
 * @param before Statistics before an operation
 * @return Difference between the two statistics
 */
MemoryStats memory_profiler_diff_stats(const MemoryStats *after, const MemoryStats *before);

/**
 * @brief Print difference between memory statistics
 * @param diff Calculated difference
 * @param test_name Name of the test for context
 */
void memory_profiler_print_diff(MemoryStats diff, const char *test_name);

/**
 * @brief Start memory profiling for a test
 * @param test_name Name of the test being profiled
 */
void memory_test_start(const char *test_name);

/**
 * @brief End memory profiling for a test
 * @param test_name Name of the test being profiled
 */
void memory_test_end(const char *test_name);

#ifdef ENABLE_MEMORY_PROFILING
// Enable memory profiling when ENABLE_MEMORY_PROFILING is defined

// Memory usage macros
#define MEMORY_PROFILER_INIT() memory_profiler_init()
#define MEMORY_PROFILER_RESET() memory_profiler_reset()
#define MEMORY_PROFILER_CLEANUP() memory_profiler_cleanup()
#define MEMORY_PROFILER_PRINT_STATS(test_name) memory_profiler_print_stats(test_name)
#define MEMORY_PROFILER_CHECK_LEAKS(location) memory_profiler_check_leaks(location)

// Object tracking macros
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

// No-op object tracking macros for release builds
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
/**
 * @brief Test memory profiling macros for automatic test integration
 * 
 * These macros provide convenient integration with test frameworks
 * and automatic memory profiling for test cases.
 */

/**
 * @brief Start memory profiling for a test
 * @param test_name Name of the test being profiled
 * 
 * Initializes memory profiling hooks and prints start message.
 * Should be paired with MEMORY_TEST_END().
 */
#define MEMORY_TEST_START(test_name) memory_test_start(test_name)

/**
 * @brief End memory profiling for a test
 * @param test_name Name of the test being profiled
 * 
 * Prints final statistics, checks for leaks, and cleans up profiling hooks.
 * Should be paired with MEMORY_TEST_START().
 */
#define MEMORY_TEST_END(test_name) memory_test_end(test_name)

/**
 * @brief Start memory benchmarking for a test
 * @param test_name Name of the test being benchmarked
 * 
 * Captures initial memory state for comparison.
 * Should be paired with MEMORY_TEST_BENCHMARK_END().
 */
#define MEMORY_TEST_BENCHMARK_START(test_name) do { \
    MemoryStats before = memory_profiler_get_stats(); \
    printf("🔍 Memory Benchmark: %s\n", test_name); \
} while(0)

/**
 * @brief End memory benchmarking for a test
 * @param test_name Name of the test being benchmarked
 * 
 * Captures final memory state and prints comparison statistics.
 * Should be paired with MEMORY_TEST_BENCHMARK_START().
 */
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
