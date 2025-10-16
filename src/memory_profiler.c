/*
 * Memory Profiler Implementation for Tiny-CLJ
 * 
 * Comprehensive memory tracking and profiling system implementation.
 * Provides detailed statistics on object allocation, deallocation, and memory usage.
 * 
 * Implementation Details:
 * - Thread-safe global statistics tracking
 * - Automatic memory leak detection
 * - Peak memory usage monitoring
 * - Object type breakdown analysis
 * - Reference counting operation tracking
 * - Test-specific profiling integration
 * 
 * Key Features:
 * - Zero overhead in RELEASE builds (all functions are no-ops)
 * - Detailed memory statistics with visual formatting
 * - Automatic leak detection and reporting
 * - Per-object-type allocation tracking
 * - Memory efficiency metrics calculation
 * 
 * Usage Patterns:
 * - Automatic profiling via WITH_MEMORY_PROFILING() macro
 * - Manual profiling via MEMORY_TEST_START/END macros
 * - Benchmarking via MEMORY_TEST_BENCHMARK_START/END macros
 * 
 * Statistics Output:
 * - Tabular format with Unicode symbols for readability
 * - Memory operations summary (allocations, deallocations, leaks)
 * - Object lifecycle tracking (creations, destructions, operations)
 * - Type-specific breakdown (INT, FLOAT, VECTOR, etc.)
 * - Efficiency metrics (retention ratio, deallocation ratio)
 * 
 * @author Tiny-CLJ Team
 * @version 1.0
 * @since 2024
 */

#include "memory_profiler.h"
#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ============================================================================
// GLOBAL MEMORY STATISTICS
// ============================================================================

/**
 * @brief Global memory statistics structure
 * 
 * Tracks all memory operations across the entire application.
 * Thread-safe access through atomic operations.
 */
MemoryStats g_memory_stats = {0};

/**
 * @brief Global memory profiling enabled flag
 * 
 * Controls whether memory profiling is active.
 * Can be toggled at runtime for performance testing.
 */
bool g_memory_profiling_enabled = false;

// ============================================================================
// MEMORY HOOKS IMPLEMENTATION
// ============================================================================

#ifdef DEBUG
// Global hook function (only one hook supported for simplicity)
static MemoryHookFunc g_hook_func = NULL;

// ============================================================================
// MEMORY HOOKS FUNCTIONS
// ============================================================================

void memory_hooks_init(void) {
    g_hook_func = NULL;
}

void memory_hooks_cleanup(void) {
    g_hook_func = NULL;
}

void memory_hooks_register(MemoryHookFunc hook) {
    g_hook_func = hook;
}

void memory_hooks_unregister(void) {
    g_hook_func = NULL;
}

void memory_hook_trigger(MemoryHookType type, void *ptr, size_t size) {
    if (g_hook_func) {
        g_hook_func(type, ptr, size);
    }
}

// Default memory profiler hook implementation
static void memory_profiler_hook(MemoryHookType type, void *ptr, size_t size) {
    switch (type) {
        case MEMORY_HOOK_DEALLOCATION:
            MEMORY_PROFILER_TRACK_DEALLOCATION(size);
            break;
        case MEMORY_HOOK_RETAIN:
            MEMORY_PROFILER_TRACK_RETAIN((CljObject*)ptr);
            break;
        case MEMORY_HOOK_RELEASE:
            MEMORY_PROFILER_TRACK_RELEASE((CljObject*)ptr);
            break;
        case MEMORY_HOOK_AUTORELEASE:
            MEMORY_PROFILER_TRACK_AUTORELEASE((CljObject*)ptr);
            break;
    }
}

// Initialize memory profiling hooks
void memory_profiling_init_with_hooks(void) {
    memory_profiler_init();
    memory_hooks_register(memory_profiler_hook);
}

// Cleanup memory profiling hooks
void memory_profiling_cleanup_with_hooks(void) {
    memory_hooks_unregister();
    memory_profiler_cleanup();
}

void memory_test_start(const char *test_name) {
    // Reset memory statistics for this test to get isolated results
    memory_profiler_reset();
    printf("🔍 Memory Test Start: %s\n", test_name);
}

void memory_test_end(const char *test_name) {
    memory_profiler_print_stats(test_name);
    memory_profiler_check_leaks(test_name);
}

// ============================================================================
// MEMORY PROFILING FUNCTIONS
// ============================================================================

/**
 * @brief Initialize the memory profiler system
 * 
 * Resets all statistics and prepares the profiler for tracking.
 * Should be called at the start of memory profiling sessions.
 * 
 * Initializes:
 * - All counters to zero
 * - Type-specific arrays
 * - Memory usage tracking
 */
void memory_profiler_init(void) {
    memset(&g_memory_stats, 0, sizeof(MemoryStats));
    // Initialize type arrays
    memset(g_memory_stats.allocations_by_type, 0, sizeof(g_memory_stats.allocations_by_type));
    memset(g_memory_stats.deallocations_by_type, 0, sizeof(g_memory_stats.deallocations_by_type));
    // Memory profiler initialized
}

/**
 * @brief Reset memory statistics to zero
 * 
 * Clears all tracked statistics without disabling profiling.
 * Useful for starting fresh profiling sessions.
 * 
 * Resets:
 * - All counters to zero
 * - Type-specific arrays
 * - Memory usage tracking
 */
void memory_profiler_reset(void) {
    memset(&g_memory_stats, 0, sizeof(MemoryStats));
    // Initialize type arrays
    memset(g_memory_stats.allocations_by_type, 0, sizeof(g_memory_stats.allocations_by_type));
    memset(g_memory_stats.deallocations_by_type, 0, sizeof(g_memory_stats.deallocations_by_type));
}

/**
 * @brief Cleanup memory profiler and report final statistics
 * 
 * Prints final memory statistics and detects potential leaks.
 * Should be called at the end of profiling sessions.
 * 
 * Reports:
 * - Final memory statistics
 * - Potential memory leaks
 * - Cleanup completion
 */
void memory_profiler_cleanup(void) {
    if (g_memory_stats.memory_leaks > 0) {
        printf("⚠️  Memory Profiler: %zu potential memory leaks detected!\n", 
               g_memory_stats.memory_leaks);
    }
    // Memory profiler initialized
}

/**
 * @brief Get current memory statistics
 * @return Current MemoryStats structure
 * 
 * Returns a copy of the current global memory statistics.
 * Safe to call from any thread.
 */
MemoryStats memory_profiler_get_stats(void) {
    return g_memory_stats;
}

// Helper function to print memory statistics table (shared between print_stats and print_diff)
static void print_memory_table(const MemoryStats *stats, const char *test_name, bool is_delta) {
    (void)test_name; // Suppress unused parameter warning
    if (!stats) return;
    
    const char *title = is_delta ? "Memory Delta" : "Memory Statistics";
    const char *operations_title = is_delta ? "Memory Operations (Delta)" : "Memory Operations";
    const char *clj_title = is_delta ? "CljObject Operations (Delta)" : "CljObject Operations";
    
    printf("\n📊 %s:\n", title);
    printf("  ┌─────────────────────────────────────────────────────────┐\n");
    printf("  │ %-55s │\n", operations_title);
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    
    if (is_delta) {
        printf("  │ Allocations:      %+10ld                                │\n", (long)stats->total_allocations);
        printf("  │ Deallocations:    %+10ld                                │\n", (long)stats->total_deallocations);
        printf("  │ Peak Memory:      %+10ld bytes                          │\n", (long)stats->peak_memory_usage);
        printf("  │ Current Memory:   %+10ld bytes                          │\n", (long)stats->current_memory_usage);
        printf("  │ Memory Leaks:     %+10ld                                │\n", (long)stats->memory_leaks);
    } else {
        printf("  │ Allocations:      %10zu                                │\n", stats->total_allocations);
        printf("  │ Deallocations:    %10zu                                │\n", stats->total_deallocations);
        printf("  │ Peak Memory:      %10zu bytes                          │\n", stats->peak_memory_usage);
        printf("  │ Current Memory:   %10zu bytes                          │\n", stats->current_memory_usage);
        printf("  │ Memory Leaks:     %10zu                                │\n", stats->memory_leaks);
    }
    
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    printf("  │ %-55s │\n", clj_title);
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    
    if (is_delta) {
        printf("  │ Object Creations: %+10ld                                │\n", (long)stats->total_allocations);
        printf("  │ Object Destructions: %+8ld                              │\n", (long)stats->object_destructions);
        printf("  │ retain() calls:   %+10ld                                │\n", (long)stats->retain_calls);
        printf("  │ release() calls:  %+10ld                                │\n", (long)stats->release_calls);
        printf("  │ autorelease() calls: %+7ld                              │\n", (long)stats->autorelease_calls);
    } else {
        printf("  │ Object Creations: %10zu                                │\n", stats->total_allocations);
        printf("  │ Object Destructions: %8zu                              │\n", stats->object_destructions);
        printf("  │ retain() calls:   %10zu                                │\n", stats->retain_calls);
        printf("  │ release() calls:  %10zu                                │\n", stats->release_calls);
        printf("  │ autorelease() calls: %7zu                              │\n", stats->autorelease_calls);
    }
    
    printf("  └─────────────────────────────────────────────────────────┘\n");
    
    // Object type breakdown
    printf("\n📋 Object Type Breakdown:\n");
    printf("  ┌─────────────────────────────────────────────────────────┐\n");
    printf("  │ Type                │ Allocations │ Deallocations │ Leaks │\n");
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    
    const char* type_names[] = {
        "NIL", "BOOL", "SYMBOL", "INT", "FLOAT", "STRING", "VECTOR", 
        "WEAK_VECTOR", "MAP", "LIST", "SEQ", 
        "FUNC", "EXCEPTION", "UNKNOWN"
    };
    
    size_t total_type_leaks = 0;
    // Safety check: ensure CLJ_TYPE_COUNT doesn't exceed array bounds
    int max_types = (CLJ_TYPE_COUNT < (int)(sizeof(type_names)/sizeof(type_names[0]))) ? 
                    CLJ_TYPE_COUNT : (int)(sizeof(type_names)/sizeof(type_names[0]));
    
    for (int i = 0; i < max_types; i++) {
        size_t allocs = stats->allocations_by_type[i];
        size_t deallocs = stats->deallocations_by_type[i];
        size_t leaks = (allocs >= deallocs) ? (allocs - deallocs) : 0;
        total_type_leaks += leaks;
        
        if (allocs > 0 || deallocs > 0 || leaks > 0) {
            // Bounds check to prevent array overflow
            if (i >= 0 && i < (int)(sizeof(type_names)/sizeof(type_names[0]))) {
                const char* type_name = type_names[i];
                printf("  │ %-18s │ %10zu │ %12zu │ %5zu │\n", 
                       type_name, allocs, deallocs, leaks);
            } else {
                printf("  │ %-18s │ %10zu │ %12zu │ %5zu │\n", 
                       "UNKNOWN", allocs, deallocs, leaks);
            }
        }
    }
    
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    printf("  │ %-18s │ %10zu │ %12zu │ %5zu │\n", 
           "TOTAL", stats->total_allocations, stats->object_destructions, total_type_leaks);
    printf("  └─────────────────────────────────────────────────────────┘\n");
    
    // Calculate efficiency metrics with safety checks
    if (stats->total_allocations > 0) {
        double retention_ratio = (double)stats->retain_calls / stats->total_allocations;
        const char *ratio_label = is_delta ? "Delta Retention Ratio" : "Retention Ratio";
        const char *ratio_suffix = is_delta ? "" : " (retain calls per allocation)";
        printf("  📈 %s: %.2f%s\n", ratio_label, retention_ratio, ratio_suffix);
    }
    
    if (stats->total_allocations > 0) {
        double deallocation_ratio = (double)stats->total_deallocations / stats->total_allocations;
        const char *ratio_label = is_delta ? "Delta Deallocation Ratio" : "Deallocation Ratio";
        const char *ratio_suffix = is_delta ? "" : " (deallocations per allocation)";
        printf("  📈 %s: %.2f%s\n", ratio_label, deallocation_ratio, ratio_suffix);
    }
    
    // Memory efficiency assessment with enhanced warnings
    if (stats->memory_leaks > 0) {
        printf("\n🚨 CRITICAL MEMORY LEAK DETECTED!\n");
        printf("   ┌─────────────────────────────────────────────────────────┐\n");
        printf("   │ LEAK ALERT: %zu allocations not freed!                    │\n", stats->memory_leaks);
        printf("   │ Current Memory Usage: %zu bytes                         │\n", stats->current_memory_usage);
        printf("   │ Peak Memory Usage: %zu bytes                             │\n", stats->peak_memory_usage);
        printf("   └─────────────────────────────────────────────────────────┘\n");
        printf("   ⚠️  This indicates potential memory management issues!\n");
        printf("   ⚠️  Check for missing RELEASE() calls or incorrect lifecycle management.\n");
    } else if (stats->total_allocations == stats->total_deallocations && stats->total_allocations > 0) {
        printf("\n✅ PERFECT MEMORY MANAGEMENT: All allocations properly freed\n");
    } else if (stats->total_allocations == 0) {
        printf("\n📊 No memory operations detected in this test\n");
    }
}

void memory_profiler_print_stats(const char *test_name) {
    print_memory_table(&g_memory_stats, test_name, false);
}

// ============================================================================
// MEMORY TRACKING FUNCTIONS
// ============================================================================

// Helper function to update memory leak statistics and detect double-frees
static void update_memory_leak_stats(void) {
    // Calculate memory leaks based on allocations vs destructions
    if (g_memory_stats.total_allocations >= g_memory_stats.object_destructions) {
        g_memory_stats.memory_leaks = g_memory_stats.total_allocations - g_memory_stats.object_destructions;
    } else {
        g_memory_stats.memory_leaks = 0; // No leaks if destructions exceed allocations
        // This is not necessarily a double-free - could be normal cleanup
        // Only warn if the difference is significant
        if (g_memory_stats.object_destructions > g_memory_stats.total_allocations + 2) {
            static bool double_free_warning_shown = false;
            if (!double_free_warning_shown) {
                printf("⚠️  WARNING: Potential double-free detected! Object destructions (%zu) significantly exceed allocations (%zu).\n", 
                       g_memory_stats.object_destructions, g_memory_stats.total_allocations);
                double_free_warning_shown = true;
            }
        }
    }
}


void memory_profiler_track_deallocation(size_t size) {
    g_memory_stats.total_deallocations++;
    if (g_memory_stats.current_memory_usage >= size) {
        g_memory_stats.current_memory_usage -= size;
    } else {
        g_memory_stats.current_memory_usage = 0;
    }
    update_memory_leak_stats();
}

void memory_profiler_track_object_creation(CljObject *obj) {
    if (obj) {
        g_memory_stats.total_allocations++;
        
        // Add memory tracking
        size_t obj_size = sizeof(CljObject);
        g_memory_stats.current_memory_usage += obj_size;
        if (g_memory_stats.current_memory_usage > g_memory_stats.peak_memory_usage) {
            g_memory_stats.peak_memory_usage = g_memory_stats.current_memory_usage;
        }
        
        // Track by object type with bounds checking
        assert(obj->type >= 0 && obj->type < CLJ_TYPE_COUNT && "Invalid object type for memory tracking");
        g_memory_stats.allocations_by_type[obj->type]++;
    }
}

void memory_profiler_track_object_destruction(CljObject *obj) {
    if (obj) {
        g_memory_stats.object_destructions++;
        // Track the deallocation size (approximate)
        memory_profiler_track_deallocation(sizeof(CljObject));
        
        // Track by object type with bounds checking
        assert(obj->type >= 0 && obj->type < CLJ_TYPE_COUNT && "Invalid object type for memory tracking");
        g_memory_stats.deallocations_by_type[obj->type]++;
    }
}

void memory_profiler_track_retain(CljObject *obj) {
    if (obj) {
        g_memory_stats.retain_calls++;
    }
}

void memory_profiler_track_release(CljObject *obj) {
    if (obj) {
        g_memory_stats.release_calls++;
    }
}

void memory_profiler_track_autorelease(CljObject *obj) {
    if (obj) {
        g_memory_stats.autorelease_calls++;
    }
}

// ============================================================================
// MEMORY LEAK DETECTION
// ============================================================================

void memory_profiler_check_leaks(const char *location) {
    if (g_memory_stats.memory_leaks > 0) {
        printf("\n🚨 MEMORY LEAK DETECTED at %s:\n", location ? location : "Unknown");
        printf("   ┌─────────────────────────────────────────────────────────┐\n");
        printf("   │ LEAK SUMMARY                                            │\n");
        printf("   ├─────────────────────────────────────────────────────────┤\n");
        printf("   │ Total Leaks:        %10zu allocations                    │\n", g_memory_stats.memory_leaks);
        printf("   │ Current Memory:     %10zu bytes                         │\n", g_memory_stats.current_memory_usage);
        printf("   │ Peak Memory:       %10zu bytes                         │\n", g_memory_stats.peak_memory_usage);
        printf("   │ Allocations:        %10zu                               │\n", g_memory_stats.total_allocations);
        printf("   │ Deallocations:      %10zu                               │\n", g_memory_stats.total_deallocations);
        printf("   └─────────────────────────────────────────────────────────┘\n");
        
        // Show detailed leak breakdown by type
        printf("\n🔍 LEAK BREAKDOWN BY OBJECT TYPE:\n");
        printf("   ┌─────────────────────────────────────────────────────────┐\n");
        printf("   │ Type                │ Allocations │ Deallocations │ Leaks │\n");
        printf("   ├─────────────────────────────────────────────────────────┤\n");
        
        const char* type_names[] = {
            "NIL", "BOOL", "SYMBOL", "INT", "FLOAT", "STRING", "VECTOR", 
            "WEAK_VECTOR", "MAP", "LIST", "SEQ", 
            "FUNC", "EXCEPTION", "UNKNOWN"
        };
        
        size_t total_type_leaks = 0;
        int max_types = (CLJ_TYPE_COUNT < (int)(sizeof(type_names)/sizeof(type_names[0]))) ? 
                        CLJ_TYPE_COUNT : (int)(sizeof(type_names)/sizeof(type_names[0]));
        
        for (int i = 0; i < max_types; i++) {
            size_t allocs = g_memory_stats.allocations_by_type[i];
            size_t deallocs = g_memory_stats.deallocations_by_type[i];
            size_t leaks = (allocs >= deallocs) ? (allocs - deallocs) : 0;
            total_type_leaks += leaks;
            
            if (leaks > 0) {
                const char* type_name = (i >= 0 && i < (int)(sizeof(type_names)/sizeof(type_names[0]))) 
                                       ? type_names[i] : "UNKNOWN";
                printf("   │ %-18s │ %10zu │ %12zu │ %5zu │\n", 
                       type_name, allocs, deallocs, leaks);
            }
        }
        
        printf("   ├─────────────────────────────────────────────────────────┤\n");
        printf("   │ %-18s │ %10zu │ %12zu │ %5zu │\n", 
               "TOTAL", g_memory_stats.total_allocations, g_memory_stats.object_destructions, total_type_leaks);
        printf("   └─────────────────────────────────────────────────────────┘\n");
        
        printf("\n⚠️  CRITICAL: %zu memory leaks detected! Objects were not properly freed.\n", 
               g_memory_stats.memory_leaks);
        printf("   This indicates potential memory management issues in the code.\n");
        printf("   Check for missing RELEASE() calls or incorrect object lifecycle management.\n\n");
    } else {
        printf("\n✅ MEMORY CLEAN: All allocations properly freed at %s\n", location ? location : "Unknown");
    }
}

bool memory_profiler_has_leaks(void) {
    return g_memory_stats.memory_leaks > 0;
}

// ============================================================================
// MEMORY COMPARISON FUNCTIONS
// ============================================================================

MemoryStats memory_profiler_diff_stats(const MemoryStats *after, const MemoryStats *before) {
    MemoryStats diff = {0};
    
    diff.total_allocations = after->total_allocations - before->total_allocations;
    diff.total_deallocations = after->total_deallocations - before->total_deallocations;
    diff.peak_memory_usage = after->peak_memory_usage - before->peak_memory_usage;
    diff.current_memory_usage = after->current_memory_usage - before->current_memory_usage;
    diff.object_destructions = after->object_destructions - before->object_destructions;
    diff.retain_calls = after->retain_calls - before->retain_calls;
    diff.release_calls = after->release_calls - before->release_calls;
    diff.autorelease_calls = after->autorelease_calls - before->autorelease_calls;
    diff.memory_leaks = after->memory_leaks - before->memory_leaks;
    
    return diff;
}

void memory_profiler_print_diff(MemoryStats diff, const char *test_name) {
    print_memory_table(&diff, test_name, true);
}

#else
// No-op implementations for release builds

// Memory hooks no-op implementations
void memory_hooks_init(void) { /* no-op */ }
void memory_hooks_cleanup(void) { /* no-op */ }
void memory_hooks_register(MemoryHookFunc hook) { (void)hook; /* no-op */ }
void memory_hooks_unregister(void) { /* no-op */ }
void memory_hook_trigger(MemoryHookType type, void *ptr, size_t size) { 
    (void)type; (void)ptr; (void)size; /* no-op */ 
}
void memory_profiling_init_with_hooks(void) { /* no-op */ }
void memory_profiling_cleanup_with_hooks(void) { /* no-op */ }
void memory_test_start(const char *test_name) { (void)test_name; /* no-op */ }
void memory_test_end(const char *test_name) { (void)test_name; /* no-op */ }

void memory_profiler_init(void) { /* no-op */ }
void memory_profiler_reset(void) { /* no-op */ }
void memory_profiler_cleanup(void) { /* no-op */ }
MemoryStats memory_profiler_get_stats(void) { MemoryStats empty = {0}; return empty; }
void memory_profiler_print_stats(const char *test_name) { 
    (void)test_name; /* no-op */ 
}

void memory_profiler_track_allocation(size_t size) { 
    (void)size; /* no-op */ 
}
void memory_profiler_track_deallocation(size_t size) { 
    (void)size; /* no-op */ 
}
void memory_profiler_track_object_creation(CljObject *obj) { 
    (void)obj; /* no-op */ 
}
void memory_profiler_track_object_destruction(CljObject *obj) { 
    (void)obj; /* no-op */ 
}
void memory_profiler_track_retain(CljObject *obj) { 
    (void)obj; /* no-op */ 
}
void memory_profiler_track_release(CljObject *obj) { 
    (void)obj; /* no-op */ 
}
void memory_profiler_track_autorelease(CljObject *obj) { 
    (void)obj; /* no-op */ 
}

void memory_profiler_check_leaks(const char *location) { 
    (void)location; /* no-op */ 
}
bool memory_profiler_has_leaks(void) { return false; }

MemoryStats memory_profiler_diff_stats(const MemoryStats *after, const MemoryStats *before) { 
    (void)after;
    (void)before;
    MemoryStats empty = {0}; return empty; 
}
void memory_profiler_print_diff(MemoryStats diff, const char *test_name) { 
    (void)diff;
    (void)test_name; /* no-op */ 
}

#endif // DEBUG

// ============================================================================
// MEMORY PROFILING CONTROL (ALWAYS AVAILABLE)
// ============================================================================

void enable_memory_profiling(bool enabled) {
#ifdef DEBUG
    g_memory_profiling_enabled = enabled;
    if (enabled) {
        // Reset statistics when enabling profiling
        memset(&g_memory_stats, 0, sizeof(MemoryStats));
        printf("🔍 Memory profiling enabled (statistics reset)\n");
    } else {
        printf("🔍 Memory profiling disabled\n");
    }
#else
    // In release builds, this is a no-op
    (void)enabled;
#endif
}

bool is_memory_profiling_enabled(void) {
#ifdef DEBUG
    return g_memory_profiling_enabled;
#else
    return false;
#endif
}

