/*
 * Memory Profiler Implementation for Tiny-CLJ
 * 
 * Tracks object allocation and deallocation for heap analysis.
 * Only compiled in DEBUG builds.
 */

#include "memory_profiler.h"
#include "CljObject.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// GLOBAL MEMORY STATISTICS
// ============================================================================

MemoryStats g_memory_stats = {0};

#ifdef DEBUG

// ============================================================================
// MEMORY PROFILING FUNCTIONS
// ============================================================================

void memory_profiler_init(void) {
    memset(&g_memory_stats, 0, sizeof(MemoryStats));
    // Debug prints removed for production
}

void memory_profiler_reset(void) {
    memset(&g_memory_stats, 0, sizeof(MemoryStats));
}

void memory_profiler_cleanup(void) {
    if (g_memory_stats.memory_leaks > 0) {
        printf("⚠️  Memory Profiler: %zu potential memory leaks detected!\n", 
               g_memory_stats.memory_leaks);
    }
    // Debug prints removed for production
}

MemoryStats memory_profiler_get_stats(void) {
    return g_memory_stats;
}

// Helper function to print memory statistics table (shared between print_stats and print_diff)
static void print_memory_table(const MemoryStats *stats, const char *test_name, bool is_delta) {
    const char *title = is_delta ? "Memory Delta" : "Memory Statistics";
    const char *operations_title = is_delta ? "Memory Operations (Delta)" : "Memory Operations";
    const char *clj_title = is_delta ? "CljObject Operations (Delta)" : "CljObject Operations";
    
    printf("\n📊 %s for %s:\n", title, test_name);
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
        printf("  │ Object Creations: %+10ld                                │\n", (long)stats->object_creations);
        printf("  │ Object Destructions: %+8ld                              │\n", (long)stats->object_destructions);
        printf("  │ retain() calls:   %+10ld                                │\n", (long)stats->retain_calls);
        printf("  │ release() calls:  %+10ld                                │\n", (long)stats->release_calls);
        printf("  │ autorelease() calls: %+7ld                              │\n", (long)stats->autorelease_calls);
    } else {
        printf("  │ Object Creations: %10zu                                │\n", stats->object_creations);
        printf("  │ Object Destructions: %8zu                              │\n", stats->object_destructions);
        printf("  │ retain() calls:   %10zu                                │\n", stats->retain_calls);
        printf("  │ release() calls:  %10zu                                │\n", stats->release_calls);
        printf("  │ autorelease() calls: %7zu                              │\n", stats->autorelease_calls);
    }
    
    printf("  └─────────────────────────────────────────────────────────┘\n");
    
    // Calculate efficiency metrics
    if (stats->object_creations > 0) {
        double retention_ratio = (double)stats->retain_calls / stats->object_creations;
        const char *ratio_label = is_delta ? "Delta Retention Ratio" : "Retention Ratio";
        const char *ratio_suffix = is_delta ? "" : " (retain calls per object)";
        printf("  📈 %s: %.2f%s\n", ratio_label, retention_ratio, ratio_suffix);
    }
    
    if (stats->total_allocations > 0) {
        double deallocation_ratio = (double)stats->total_deallocations / stats->total_allocations;
        const char *ratio_label = is_delta ? "Delta Deallocation Ratio" : "Deallocation Ratio";
        const char *ratio_suffix = is_delta ? "" : " (deallocations per allocation)";
        printf("  📈 %s: %.2f%s\n", ratio_label, deallocation_ratio, ratio_suffix);
    }
    
    // Memory efficiency assessment
    if (stats->memory_leaks > 0) {
        printf("  ⚠️  Memory Leak Detected: %zu allocations not freed\n", stats->memory_leaks);
    } else if (stats->total_allocations == stats->total_deallocations && stats->total_allocations > 0) {
        printf("  ✅ Perfect Memory Management: All allocations freed\n");
    }
}

void memory_profiler_print_stats(const char *test_name) {
    printf("\n🔍 MEMORY_PROFILER_PRINT_STATS called for: %s\n", test_name);
    print_memory_table(&g_memory_stats, test_name, false);
}

// ============================================================================
// MEMORY TRACKING FUNCTIONS
// ============================================================================

// Helper function to update memory leak statistics and detect double-frees
static void update_memory_leak_stats(void) {
    // Calculate memory leaks safely (avoid integer overflow)
    if (g_memory_stats.total_allocations >= g_memory_stats.total_deallocations) {
        g_memory_stats.memory_leaks = g_memory_stats.total_allocations - g_memory_stats.total_deallocations;
    } else {
        g_memory_stats.memory_leaks = 0; // No leaks if deallocations exceed allocations
        // This is not necessarily a double-free - could be normal cleanup
        // Only warn if the difference is significant
        if (g_memory_stats.total_deallocations > g_memory_stats.total_allocations + 2) {
            static bool double_free_warning_shown = false;
            if (!double_free_warning_shown) {
                printf("⚠️  WARNING: Potential double-free detected! Deallocations (%zu) significantly exceed allocations (%zu).\n", 
                       g_memory_stats.total_deallocations, g_memory_stats.total_allocations);
                double_free_warning_shown = true;
            }
        }
    }
}

void memory_profiler_track_allocation(size_t size) {
    g_memory_stats.total_allocations++;
    g_memory_stats.current_memory_usage += size;
    if (g_memory_stats.current_memory_usage > g_memory_stats.peak_memory_usage) {
        g_memory_stats.peak_memory_usage = g_memory_stats.current_memory_usage;
    }
    update_memory_leak_stats();
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
        g_memory_stats.object_creations++;
        // Track the allocation size (approximate)
        memory_profiler_track_allocation(sizeof(CljObject));
    }
}

void memory_profiler_track_object_destruction(CljObject *obj) {
    if (obj) {
        g_memory_stats.object_destructions++;
        // Track the deallocation size (approximate)
        memory_profiler_track_deallocation(sizeof(CljObject));
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
        printf("⚠️  Memory Leak Warning at %s: %zu allocations not freed\n", 
               location, g_memory_stats.memory_leaks);
    } else {
        printf("✅ Memory Clean at %s: All allocations freed\n", location);
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
    diff.object_creations = after->object_creations - before->object_creations;
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

void memory_profiler_init(void) { /* no-op */ }
void memory_profiler_reset(void) { /* no-op */ }
void memory_profiler_cleanup(void) { /* no-op */ }
MemoryStats memory_profiler_get_stats(void) { MemoryStats empty = {0}; return empty; }
void memory_profiler_print_stats(const char *test_name) { /* no-op */ }

void memory_profiler_track_allocation(size_t size) { /* no-op */ }
void memory_profiler_track_deallocation(size_t size) { /* no-op */ }
void memory_profiler_track_object_creation(CljObject *obj) { /* no-op */ }
void memory_profiler_track_object_destruction(CljObject *obj) { /* no-op */ }
void memory_profiler_track_retain(CljObject *obj) { /* no-op */ }
void memory_profiler_track_release(CljObject *obj) { /* no-op */ }
void memory_profiler_track_autorelease(CljObject *obj) { /* no-op */ }

void memory_profiler_check_leaks(const char *location) { /* no-op */ }
bool memory_profiler_has_leaks(void) { return false; }

MemoryStats memory_profiler_diff_stats(const MemoryStats *after, const MemoryStats *before) { 
    MemoryStats empty = {0}; return empty; 
}
void memory_profiler_print_diff(MemoryStats diff, const char *test_name) { /* no-op */ }

#endif // DEBUG
