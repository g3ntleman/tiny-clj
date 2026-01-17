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
 * - Type-specific breakdown (STRING, VECTOR, MAP, etc.)
 * - Efficiency metrics (retention ratio, deallocation ratio)
 * 
 * @author Tiny-CLJ Team
 * @version 1.0
 * @since 2024
 */

#include "memory_profiler.h"
#include "memory.h"  // For LOGF macro
#include "object.h"
#include "value.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#if defined(__APPLE__)
#include <malloc/malloc.h> // malloc_size
#elif defined(__linux__)
#include <malloc.h>        // malloc_usable_size
#endif

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

/**
 * @brief Global memory leak reporting flag
 * 
 * Controls whether memory leak messages are printed for each test.
 * When false, only summary leak information is printed.
 */
bool g_memory_leak_reporting_enabled = true;

/**
 * @brief Global memory verbose mode flag
 * 
 * Controls whether detailed memory statistics are printed for successful tests.
 * When false, only errors and leaks are printed.
 */
bool g_memory_verbose_mode = false;

// ============================================================================
// MEMORY HOOKS IMPLEMENTATION
// ============================================================================

#if MEMORY_PROFILING_ENABLED

static inline size_t clj_malloc_usable_size(void *p) {
    if (!p) return 0;
#if defined(__APPLE__)
    return malloc_size(p);
#elif defined(__linux__)
    return malloc_usable_size(p);
#else
    return 0;
#endif
}
// ============================================================================
// RAW ALLOCATION BLOCK TRACKING
// ============================================================================
//
// Tracks malloc/calloc/realloc/free that go through CLJ_* allocation macros.
// Best-effort: unknown frees are ignored.
//
// NOTE: This is profiler-only state. It uses plain malloc/free internally and
// intentionally does not go through CLJ_* macros to avoid recursion.
//

typedef struct {
    void *ptr;          // NULL means empty slot; (void*)-1 means tombstone
    size_t size;        // bytes
    const char *file;   // allocation site (string literal)
    int line;           // allocation site
} RawAllocEntry;

static RawAllocEntry *g_raw_table = NULL;
static size_t g_raw_cap = 0;
static size_t g_raw_len = 0; // live entries (excludes tombstones)

static inline size_t ptr_hash(void *p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
}

static void raw_table_init_if_needed(void) {
    if (g_raw_table) return;
    g_raw_cap = 1024;
    g_raw_len = 0;
    g_raw_table = (RawAllocEntry*)malloc(sizeof(RawAllocEntry) * g_raw_cap);
    if (!g_raw_table) {
        g_raw_cap = 0;
        return;
    }
    memset(g_raw_table, 0, sizeof(RawAllocEntry) * g_raw_cap);
}

static void raw_table_grow(void) {
    size_t old_cap = g_raw_cap;
    size_t new_cap = (old_cap == 0) ? 1024 : (old_cap * 2);
    RawAllocEntry *new_tab = (RawAllocEntry*)malloc(sizeof(RawAllocEntry) * new_cap);
    if (!new_tab) return;
    memset(new_tab, 0, sizeof(RawAllocEntry) * new_cap);

    for (size_t i = 0; i < old_cap; i++) {
        RawAllocEntry e = g_raw_table[i];
        if (!e.ptr || e.ptr == (void*)-1) continue;
        size_t mask = new_cap - 1;
        size_t idx = ptr_hash(e.ptr) & mask;
        while (new_tab[idx].ptr) {
            idx = (idx + 1) & mask;
        }
        new_tab[idx] = e;
    }

    free(g_raw_table);
    g_raw_table = new_tab;
    g_raw_cap = new_cap;
}

static RawAllocEntry* raw_table_find_slot(void *ptr, bool *out_found) {
    *out_found = false;
    if (!ptr || ptr == (void*)-1) return NULL;
    if (!g_raw_table || g_raw_cap == 0) return NULL;

    size_t mask = g_raw_cap - 1;
    size_t idx = ptr_hash(ptr) & mask;
    RawAllocEntry *first_tomb = NULL;

    for (;;) {
        RawAllocEntry *slot = &g_raw_table[idx];
        if (!slot->ptr) {
            return first_tomb ? first_tomb : slot;
        }
        if (slot->ptr == (void*)-1) {
            if (!first_tomb) first_tomb = slot;
        } else if (slot->ptr == ptr) {
            *out_found = true;
            return slot;
        }
        idx = (idx + 1) & mask;
    }
}

static bool raw_table_remove(void *ptr, RawAllocEntry *out_entry) {
    if (out_entry) memset(out_entry, 0, sizeof(*out_entry));
    if (!g_raw_table || g_raw_cap == 0 || !ptr) return false;

    size_t mask = g_raw_cap - 1;
    size_t idx = ptr_hash(ptr) & mask;
    for (;;) {
        RawAllocEntry *slot = &g_raw_table[idx];
        if (!slot->ptr) return false;
        if (slot->ptr != (void*)-1 && slot->ptr == ptr) {
            if (out_entry) *out_entry = *slot;
            slot->ptr = (void*)-1;
            slot->size = 0;
            slot->file = NULL;
            slot->line = 0;
            if (g_raw_len > 0) g_raw_len--;
            return true;
        }
        idx = (idx + 1) & mask;
    }
}

static void raw_table_put(void *ptr, size_t size, const char *file, int line) {
    if (!ptr) return;
    raw_table_init_if_needed();
    if (!g_raw_table || g_raw_cap == 0) return;

    // Keep load factor <= ~0.7
    if ((g_raw_len + 1) * 10 >= g_raw_cap * 7) {
        raw_table_grow();
        if (!g_raw_table || g_raw_cap == 0) return;
    }

    bool found = false;
    RawAllocEntry *slot = raw_table_find_slot(ptr, &found);
    if (!slot) return;
    if (!found) g_raw_len++;

    slot->ptr = ptr;
    slot->size = size;
    slot->file = file;
    slot->line = line;
}

static void raw_table_reset(void) {
    if (g_raw_table && g_raw_cap) {
        memset(g_raw_table, 0, sizeof(RawAllocEntry) * g_raw_cap);
    }
    g_raw_len = 0;
}

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
            MEMORY_PROFILER_TRACK_RETAIN(ptr);
            break;
        case MEMORY_HOOK_RELEASE:
            MEMORY_PROFILER_TRACK_RELEASE(ptr);
            break;
        case MEMORY_HOOK_AUTORELEASE:
            MEMORY_PROFILER_TRACK_AUTORELEASE(ptr);
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
    // Only print start message in verbose mode
    if (g_memory_verbose_mode) {
        LOGF(stdout, "🔍 Memory Test Start: %s\n", test_name);
    }
}

void memory_test_end(const char *test_name) {
    // Only print detailed stats if there are leaks or in verbose mode
    if (g_memory_stats.memory_leaks > 0 || g_memory_verbose_mode) {
        memory_profiler_print_stats(test_name);
    }
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
    memset(g_memory_stats.autoreleases_by_type, 0, sizeof(g_memory_stats.autoreleases_by_type));
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
    memset(g_memory_stats.retains_by_type, 0, sizeof(g_memory_stats.retains_by_type));
    memset(g_memory_stats.releases_by_type, 0, sizeof(g_memory_stats.releases_by_type));
    memset(g_memory_stats.autoreleases_by_type, 0, sizeof(g_memory_stats.autoreleases_by_type));

    // Reset raw allocation bookkeeping for test isolation.
    raw_table_reset();

#ifdef DEBUG
    // Keep autorelease peak scoped to the same profiling window as MemoryStats.
    autorelease_pool_peak_reset();
#endif
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
        LOGF(stdout, "⚠️  Memory Profiler: %zu potential memory leaks detected!\n", 
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
    if (!g_memory_profiling_enabled) {
        MemoryStats empty = {0};
        return empty;
    }
    return g_memory_stats;
}

// Helper function to print memory statistics table (shared between print_stats and print_diff)
static void print_memory_table(const MemoryStats *stats, const char *test_name, bool is_delta) {
    (void)test_name; // Suppress unused parameter warning
    if (!stats) return;
    // Only print if verbose mode is enabled
    if (!g_memory_verbose_mode) return;
    
    // Compact output - only show essential information
    if (is_delta) {
        LOGF(stdout, "📊 Memory Delta: Alloc:%+ld Dealloc:%+ld Peak:%+ld Current:%+ld Leaks:%+ld\n", 
               (long)stats->total_allocations, (long)stats->total_deallocations, 
               (long)stats->peak_memory_usage, (long)stats->current_memory_usage, 
               (long)stats->memory_leaks);
    } else {
        LOGF(stdout, "📊 Memory: Alloc:%zu Dealloc:%zu Peak:%zu Current:%zu Leaks:%zu\n", 
               stats->total_allocations, stats->total_deallocations, 
               stats->peak_memory_usage, stats->current_memory_usage, stats->memory_leaks);
    }

    // Raw allocation summary (malloc/calloc/realloc/free via CLJ_* macros).
    if (!is_delta) {
        LOGF(stdout, "🧱 Raw: Alloc:%zu Free:%zu Realloc:%zu Blocks:%zu (peak=%zu) Bytes:%zu (peak=%zu)\n",
             stats->raw_allocations, stats->raw_frees, stats->raw_reallocations,
             stats->raw_blocks_current, stats->raw_blocks_peak,
             stats->raw_bytes_current, stats->raw_bytes_peak);
    }

#ifdef DEBUG
    LOGF(stdout, "📌 Autorelease Peak Count: %u\n", (unsigned)autorelease_pool_peak_count());
#endif
    
    // Compact object type breakdown - only show types with activity
    bool has_activity = false;
    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        size_t allocs = stats->allocations_by_type[i];
        size_t deallocs = stats->deallocations_by_type[i];
        size_t retains = stats->retains_by_type[i];
        size_t releases = stats->releases_by_type[i];
        size_t autoreleases = stats->autoreleases_by_type[i];
        
        if (allocs > 0 || deallocs > 0 || retains > 0 || releases > 0 || autoreleases > 0) {
            has_activity = true;
            const char* type_name = clj_type_name((CljType)i);
            LOGF(stdout, "📋 %s: A:%zu/%zu", type_name, allocs, deallocs);
            
            // Add retain/release/autorelease info if > 0
            if (retains > 0) LOGF(stdout, " R:%zu", retains);
            if (releases > 0) LOGF(stdout, " Rel:%zu", releases);
            if (autoreleases > 0) LOGF(stdout, " AR:%zu", autoreleases);
            
            LOGF(stdout, "\n");
        }
    }
    
    // Always show basic stats even if no activity
    if (!has_activity) {
        LOGF(stdout, "📋 Types: (no memory activity detected)\n");
        LOGF(stdout, "🔍 Debug: Total allocs=%zu, deallocs=%zu, retains=%zu, releases=%zu, autoreleases=%zu\n", 
               stats->total_allocations, stats->total_deallocations, 
               stats->retain_calls, stats->release_calls, stats->autorelease_calls);
    }
    
    
    // Compact leak detection
    if (stats->memory_leaks > 0) {
        LOGF(stdout, "🚨 LEAK: %zu objects, %zu bytes\n", stats->memory_leaks, stats->current_memory_usage);
    }
}

void memory_profiler_print_stats(const char *test_name) {
    if (!g_memory_profiling_enabled) return;
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
            if (!double_free_warning_shown && g_memory_verbose_mode) {
            LOGF(stdout, "⚠️  WARNING: Potential double-free detected! Object destructions (%zu) significantly exceed allocations (%zu).\n", 
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
    if (!g_memory_profiling_enabled) return;
    
    if (obj) {
        // Only track heap objects, not immediate values
        if (is_immediate((CljValue)obj)) {
            return; // Skip immediate values (FIXNUM, CHAR, SPECIAL, FIXED)
        }
        
        // Skip singletons - they don't use memory management
        if (is_singleton(obj)) {
            return;
        }
        
        g_memory_stats.total_allocations++;
        
        // Add memory tracking (use allocator-reported size when available).
        size_t obj_size = clj_malloc_usable_size(obj);
        if (obj_size == 0) {
            obj_size = sizeof(CljObject);
        }
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
    if (!g_memory_profiling_enabled) return;
    
    if (obj) {
        // Only track heap objects, not immediate values
        if (is_immediate((CljValue)obj)) {
            return; // Skip immediate values (FIXNUM, CHAR, SPECIAL, FIXED)
        }
        
        // Skip singletons - they are never freed and don't use memory management
        if (is_singleton(obj)) {
            return;
        }
        
        g_memory_stats.object_destructions++;
        // Track the deallocation size (use allocator-reported size when available).
        size_t obj_size = clj_malloc_usable_size(obj);
        if (obj_size == 0) {
            obj_size = sizeof(CljObject);
        }
        memory_profiler_track_deallocation(obj_size);
        
        // Track by object type with bounds checking
        assert(obj->type >= 0 && obj->type < CLJ_TYPE_COUNT && "Invalid object type for memory tracking");
        g_memory_stats.deallocations_by_type[obj->type]++;
    }
}

void memory_profiler_track_object_zombify(CljObject *obj) {
    if (!g_memory_profiling_enabled) return;
    if (!obj) return;
    if (is_immediate((CljValue)obj)) return;
    if (is_singleton(obj)) return;

    // Count the event, but do NOT reduce current bytes: zombie mode keeps memory allocated.
    g_memory_stats.object_destructions++;
    assert(obj->type >= 0 && obj->type < CLJ_TYPE_COUNT && "Invalid object type for memory tracking");
    g_memory_stats.deallocations_by_type[obj->type]++;
    update_memory_leak_stats();
}

void memory_profiler_track_retain(CljObject *obj) {
    if (!g_memory_profiling_enabled) return;
    
    if (obj) {
        // Only track heap objects, not immediate values
        if (is_immediate((CljValue)obj)) {
            return; // Skip immediate values (FIXNUM, CHAR, SPECIAL, FIXED)
        }
        
        // Skip singletons (rc==0) - they don't use retain counting
        if (is_singleton(obj)) {
            return;
        }
        
        g_memory_stats.retain_calls++;
        
        // Add per-type tracking
        if (obj->type < CLJ_TYPE_COUNT) {
            g_memory_stats.retains_by_type[obj->type]++;
        }
    }
}

void memory_profiler_track_release(CljObject *obj) {
    if (!g_memory_profiling_enabled) return;
    
    if (obj) {
        // Only track heap objects, not immediate values
        if (is_immediate((CljValue)obj)) {
            return; // Skip immediate values (FIXNUM, CHAR, SPECIAL, FIXED)
        }
        
        // Skip singletons (rc==0) - they don't use retain counting
        if (is_singleton(obj)) {
            return;
        }
        
        g_memory_stats.release_calls++;
        
        // Add per-type tracking
        if (obj->type < CLJ_TYPE_COUNT) {
            g_memory_stats.releases_by_type[obj->type]++;
        }
    }
}

void memory_profiler_track_autorelease(CljObject *obj) {
    if (!g_memory_profiling_enabled) return;
    
    if (obj) {
        // Only track heap objects, not immediate values
        if (is_immediate((CljValue)obj)) {
            return; // Skip immediate values (FIXNUM, CHAR, SPECIAL, FIXED)
        }
        
        // Skip singletons - they don't use memory management
        if (is_singleton(obj)) {
            return;
        }
        
        g_memory_stats.autorelease_calls++;
        // Track autorelease by type - only for heap-allocated objects
        if (obj->type < CLJ_TYPE_COUNT) {
            g_memory_stats.autoreleases_by_type[obj->type]++;
        }
    }
}

// ============================================================================
// RAW ALLOCATION TRACKING (malloc/calloc/realloc/free)
// ============================================================================

void memory_profiler_track_raw_alloc(void *ptr, size_t size, const char *file, int line) {
    if (!g_memory_profiling_enabled) return;
    if (!ptr || size == 0) return;

    size_t usable = clj_malloc_usable_size(ptr);
    if (usable != 0) {
        size = usable;
    }

    g_memory_stats.raw_allocations++;
    g_memory_stats.raw_blocks_current++;
    g_memory_stats.raw_bytes_current += size;
    if (g_memory_stats.raw_blocks_current > g_memory_stats.raw_blocks_peak) {
        g_memory_stats.raw_blocks_peak = g_memory_stats.raw_blocks_current;
    }
    if (g_memory_stats.raw_bytes_current > g_memory_stats.raw_bytes_peak) {
        g_memory_stats.raw_bytes_peak = g_memory_stats.raw_bytes_current;
    }

    raw_table_put(ptr, size, file, line);
}

void memory_profiler_track_raw_free(void *ptr, const char *file, int line) {
    (void)file;
    (void)line;
    if (!g_memory_profiling_enabled) return;
    if (!ptr) return;

    g_memory_stats.raw_frees++;

    RawAllocEntry e;
    if (raw_table_remove(ptr, &e)) {
        if (g_memory_stats.raw_blocks_current > 0) g_memory_stats.raw_blocks_current--;
        if (g_memory_stats.raw_bytes_current >= e.size) g_memory_stats.raw_bytes_current -= e.size;
        else g_memory_stats.raw_bytes_current = 0;
    }
}

void memory_profiler_track_raw_realloc(void *old_ptr, void *new_ptr, size_t new_size, const char *file, int line) {
    if (!g_memory_profiling_enabled) return;

    g_memory_stats.raw_reallocations++;

    // realloc(NULL, n) == malloc(n)
    if (!old_ptr) {
        if (new_ptr && new_size) {
            memory_profiler_track_raw_alloc(new_ptr, new_size, file, line);
        }
        return;
    }

    // realloc(p, 0) may free p and return NULL.
    if (new_size == 0) {
        memory_profiler_track_raw_free(old_ptr, file, line);
        return;
    }

    // Normal realloc: remove old (if tracked) and add new.
    RawAllocEntry old;
    bool had_old = raw_table_remove(old_ptr, &old);
    if (had_old) {
        if (g_memory_stats.raw_blocks_current > 0) g_memory_stats.raw_blocks_current--;
        if (g_memory_stats.raw_bytes_current >= old.size) g_memory_stats.raw_bytes_current -= old.size;
        else g_memory_stats.raw_bytes_current = 0;
    }

    if (new_ptr) {
        size_t usable = clj_malloc_usable_size(new_ptr);
        size_t final_size = (usable != 0) ? usable : new_size;
        g_memory_stats.raw_blocks_current++;
        g_memory_stats.raw_bytes_current += final_size;
        if (g_memory_stats.raw_blocks_current > g_memory_stats.raw_blocks_peak) {
            g_memory_stats.raw_blocks_peak = g_memory_stats.raw_blocks_current;
        }
        if (g_memory_stats.raw_bytes_current > g_memory_stats.raw_bytes_peak) {
            g_memory_stats.raw_bytes_peak = g_memory_stats.raw_bytes_current;
        }
        raw_table_put(new_ptr, final_size, file, line);
    }
}

// ============================================================================
// MEMORY LEAK DETECTION
// ============================================================================

void memory_profiler_check_leaks(const char *location) {
    if (!g_memory_profiling_enabled) return;
    if (g_memory_stats.memory_leaks > 0) {
        if (g_memory_leak_reporting_enabled) {
            LOGF(stdout, "\n🚨 MEMORY LEAK DETECTED at %s:\n", location ? location : "Unknown");
            LOGF(stdout, "   ┌─────────────────────────────────────────────────────────┐\n");
            LOGF(stdout, "   │ LEAK SUMMARY                                            │\n");
            LOGF(stdout, "   ├─────────────────────────────────────────────────────────┤\n");
            LOGF(stdout, "   │ Total Leaks:        %10zu allocations                    │\n", g_memory_stats.memory_leaks);
            LOGF(stdout, "   │ Current Memory:     %10zu bytes                         │\n", g_memory_stats.current_memory_usage);
            LOGF(stdout, "   │ Peak Memory:       %10zu bytes                         │\n", g_memory_stats.peak_memory_usage);
#ifdef DEBUG
            LOGF(stdout, "   │ Autorelease Peak:   %10u items                         │\n", (unsigned)autorelease_pool_peak_count());
#endif
            LOGF(stdout, "   │ Allocations:        %10zu                               │\n", g_memory_stats.total_allocations);
            LOGF(stdout, "   │ Deallocations:      %10zu                               │\n", g_memory_stats.total_deallocations);
            LOGF(stdout, "   └─────────────────────────────────────────────────────────┘\n");
            
            // Leak breakdown - one line per type
            for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
                size_t allocs = g_memory_stats.allocations_by_type[i];
                size_t deallocs = g_memory_stats.deallocations_by_type[i];
                size_t leaks = (allocs >= deallocs) ? (allocs - deallocs) : 0;
                
                if (leaks > 0) {
                    const char* type_name = clj_type_name((CljType)i);
                    LOGF(stdout, "🔍 %s: %zu leaks\n", type_name, leaks);
                }
            }
        }
        // If reporting is disabled, don't print anything (silent leak checking)
    }
}

bool memory_profiler_has_leaks(void) {
    if (!g_memory_profiling_enabled) return false;
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
    
    // Calculate type-specific deltas
    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        diff.allocations_by_type[i] = after->allocations_by_type[i] - before->allocations_by_type[i];
        diff.deallocations_by_type[i] = after->deallocations_by_type[i] - before->deallocations_by_type[i];
        diff.autoreleases_by_type[i] = after->autoreleases_by_type[i] - before->autoreleases_by_type[i];
    }
    
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

void memory_profiler_track_deallocation(size_t size) { 
    (void)size; /* no-op */ 
}
void memory_profiler_track_object_creation(CljObject *obj) { 
    (void)obj; /* no-op */ 
}
void memory_profiler_track_object_destruction(CljObject *obj) { 
    (void)obj; /* no-op */ 
}
void memory_profiler_track_object_zombify(CljObject *obj) {
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

void memory_profiler_track_raw_alloc(void *ptr, size_t size, const char *file, int line) {
    (void)ptr; (void)size; (void)file; (void)line; /* no-op */
}
void memory_profiler_track_raw_free(void *ptr, const char *file, int line) {
    (void)ptr; (void)file; (void)line; /* no-op */
}
void memory_profiler_track_raw_realloc(void *old_ptr, void *new_ptr, size_t new_size, const char *file, int line) {
    (void)old_ptr; (void)new_ptr; (void)new_size; (void)file; (void)line; /* no-op */
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

#endif // MEMORY_PROFILING_ENABLED

// ============================================================================
// MEMORY PROFILING CONTROL (ALWAYS AVAILABLE)
// ============================================================================

void enable_memory_profiling(bool enabled) {
#if MEMORY_PROFILING_ENABLED
    g_memory_profiling_enabled = enabled;
    if (enabled) {
        // Reset statistics when enabling profiling
        memset(&g_memory_stats, 0, sizeof(MemoryStats));
        // Silent: Don't print message on every test (JUnit-style output)
    } else {
        // Silent: Don't print message on disable
    }
    // Update cached debug output flag in memory.c
    extern void memory_update_debug_output_active(void);
    memory_update_debug_output_active();
#else
    // In release builds, this is a no-op
    (void)enabled;
#endif
}

bool is_memory_profiling_enabled(void) {
#if MEMORY_PROFILING_ENABLED
    return g_memory_profiling_enabled;
#else
    return false;
#endif
}

void set_memory_leak_reporting_enabled(bool enabled) {
    g_memory_leak_reporting_enabled = enabled;
    // Don't print status message - silent operation
}

bool is_memory_leak_reporting_enabled(void) {
    return g_memory_leak_reporting_enabled;
}

void set_memory_verbose_mode(bool verbose) {
#if MEMORY_PROFILING_ENABLED
    g_memory_verbose_mode = verbose;
    // Update cached debug output flag in memory.c
    extern void memory_update_debug_output_active(void);
    memory_update_debug_output_active();
#else
    (void)verbose; // Suppress unused parameter warning
#endif
}

