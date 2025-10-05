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
    printf("🔍 Memory Profiler initialized (DEBUG build)\n");
    printf("🔍 DEBUG macro is defined: %d\n", DEBUG);
}

void memory_profiler_reset(void) {
    memset(&g_memory_stats, 0, sizeof(MemoryStats));
}

void memory_profiler_cleanup(void) {
    if (g_memory_stats.memory_leaks > 0) {
        printf("⚠️  Memory Profiler: %zu potential memory leaks detected!\n", 
               g_memory_stats.memory_leaks);
    }
    printf("🔍 Memory Profiler cleanup completed\n");
}

MemoryStats memory_profiler_get_stats(void) {
    return g_memory_stats;
}

void memory_profiler_print_stats(const char *test_name) {
    printf("\n🔍 MEMORY_PROFILER_PRINT_STATS called for: %s\n", test_name);
    printf("📊 Memory Statistics for %s:\n", test_name);
    printf("  ┌─────────────────────────────────────────────────────────┐\n");
    printf("  │ Memory Operations                                       │\n");
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    printf("  │ Allocations:      %10zu                                │\n", g_memory_stats.total_allocations);
    printf("  │ Deallocations:    %10zu                                │\n", g_memory_stats.total_deallocations);
    printf("  │ Peak Memory:      %10zu bytes                          │\n", g_memory_stats.peak_memory_usage);
    printf("  │ Current Memory:   %10zu bytes                          │\n", g_memory_stats.current_memory_usage);
    printf("  │ Memory Leaks:     %10zu                                │\n", g_memory_stats.memory_leaks);
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    printf("  │ CljObject Operations                                   │\n");
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    printf("  │ Object Creations: %10zu                                │\n", g_memory_stats.object_creations);
    printf("  │ Object Destructions: %8zu                              │\n", g_memory_stats.object_destructions);
    printf("  │ retain() calls:   %10zu                                │\n", g_memory_stats.retain_calls);
    printf("  │ release() calls:  %10zu                                │\n", g_memory_stats.release_calls);
    printf("  │ autorelease() calls: %7zu                              │\n", g_memory_stats.autorelease_calls);
    printf("  └─────────────────────────────────────────────────────────┘\n");
    
    // Calculate efficiency metrics
    if (g_memory_stats.object_creations > 0) {
        double retention_ratio = (double)g_memory_stats.retain_calls / g_memory_stats.object_creations;
        printf("  📈 Retention Ratio: %.2f (retain calls per object)\n", retention_ratio);
    }
    
    if (g_memory_stats.total_allocations > 0) {
        double deallocation_ratio = (double)g_memory_stats.total_deallocations / g_memory_stats.total_allocations;
        printf("  📈 Deallocation Ratio: %.2f (deallocations per allocation)\n", deallocation_ratio);
    }
}

// ============================================================================
// MEMORY TRACKING FUNCTIONS
// ============================================================================

void memory_profiler_track_allocation(size_t size) {
    g_memory_stats.total_allocations++;
    g_memory_stats.current_memory_usage += size;
    if (g_memory_stats.current_memory_usage > g_memory_stats.peak_memory_usage) {
        g_memory_stats.peak_memory_usage = g_memory_stats.current_memory_usage;
    }
    g_memory_stats.memory_leaks = g_memory_stats.total_allocations - g_memory_stats.total_deallocations;
}

void memory_profiler_track_deallocation(size_t size) {
    g_memory_stats.total_deallocations++;
    if (g_memory_stats.current_memory_usage >= size) {
        g_memory_stats.current_memory_usage -= size;
    } else {
        g_memory_stats.current_memory_usage = 0;
    }
    g_memory_stats.memory_leaks = g_memory_stats.total_allocations - g_memory_stats.total_deallocations;
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
    printf("\n📊 Memory Delta for %s:\n", test_name);
    printf("  ┌─────────────────────────────────────────────────────────┐\n");
    printf("  │ Memory Operations (Delta)                               │\n");
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    printf("  │ Allocations:      %+10ld                                │\n", (long)diff.total_allocations);
    printf("  │ Deallocations:    %+10ld                                │\n", (long)diff.total_deallocations);
    printf("  │ Peak Memory:      %+10ld bytes                          │\n", (long)diff.peak_memory_usage);
    printf("  │ Current Memory:   %+10ld bytes                          │\n", (long)diff.current_memory_usage);
    printf("  │ Memory Leaks:     %+10ld                                │\n", (long)diff.memory_leaks);
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    printf("  │ CljObject Operations (Delta)                            │\n");
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    printf("  │ Object Creations: %+10ld                                │\n", (long)diff.object_creations);
    printf("  │ Object Destructions: %+8ld                              │\n", (long)diff.object_destructions);
    printf("  │ retain() calls:   %+10ld                                │\n", (long)diff.retain_calls);
    printf("  │ release() calls:  %+10ld                                │\n", (long)diff.release_calls);
    printf("  │ autorelease() calls: %+7ld                              │\n", (long)diff.autorelease_calls);
    printf("  └─────────────────────────────────────────────────────────┘\n");
    
    // Efficiency analysis
    if (diff.object_creations > 0) {
        double retention_ratio = (double)diff.retain_calls / diff.object_creations;
        printf("  📈 Delta Retention Ratio: %.2f\n", retention_ratio);
    }
    
    if (diff.total_allocations > 0) {
        double deallocation_ratio = (double)diff.total_deallocations / diff.total_allocations;
        printf("  📈 Delta Deallocation Ratio: %.2f\n", deallocation_ratio);
    }
    
    // Memory efficiency assessment
    if (diff.memory_leaks > 0) {
        printf("  ⚠️  Memory Leak Detected: %ld allocations not freed\n", (long)diff.memory_leaks);
    } else if (diff.total_allocations == diff.total_deallocations && diff.total_allocations > 0) {
        printf("  ✅ Perfect Memory Management: All allocations freed\n");
    }
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
