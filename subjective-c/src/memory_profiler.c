/*
 * Memory Profiler – comprehensive memory tracking (object/raw heap, leaks, peaks).
 * Lives in subjective-c; used by tiny-clj and subjective-c.
 */

#include "memory_profiler.h"
#include "memory.h"
#include "object.h"
#include "platform_allocated_size.h"
#include "value.h"
#include "types.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

MemoryStats g_memory_stats = {0};
bool g_memory_profiling_enabled = false;
bool g_memory_leak_reporting_enabled = true;
bool g_memory_verbose_mode = false;

// Lightweight heap tracking (always enabled in DEBUG, even without full profiling)
#ifdef DEBUG
static inline void memory_track_heap_simple(void *obj, size_t size, bool is_alloc) {
    if (!obj || size == 0) return;
    if (is_alloc) {
        g_memory_stats.current_memory_usage += size;
        if (g_memory_stats.current_memory_usage > g_memory_stats.peak_memory_usage)
            g_memory_stats.peak_memory_usage = g_memory_stats.current_memory_usage;
    } else {
        if (g_memory_stats.current_memory_usage >= size)
            g_memory_stats.current_memory_usage -= size;
        else
            g_memory_stats.current_memory_usage = 0;
    }
}
#endif

#if defined(DEBUG) || MEMORY_PROFILING_ENABLED
typedef struct { void *ptr; size_t size; } RawBlock;
static RawBlock *g_raw_blocks = NULL;
static size_t g_raw_blocks_count = 0, g_raw_blocks_capacity = 0;

static bool raw_blocks_ensure_capacity(size_t needed) {
    if (needed <= g_raw_blocks_capacity) return true;
    size_t nc = (g_raw_blocks_capacity == 0) ? 256 : g_raw_blocks_capacity * 2;
    while (nc < needed) nc *= 2;
    RawBlock *nb = (RawBlock*)realloc(g_raw_blocks, nc * sizeof(RawBlock));
    if (!nb) return false;
    g_raw_blocks = nb;
    g_raw_blocks_capacity = nc;
    return true;
}

static long raw_blocks_find(void *ptr) {
    if (!ptr) return -1;
    for (size_t i = 0; i < g_raw_blocks_count; i++)
        if (g_raw_blocks[i].ptr == ptr) return (long)i;
    return -1;
}

static void raw_blocks_update_peaks(void) {
    if (g_memory_stats.raw_bytes_current > g_memory_stats.raw_bytes_peak)
        g_memory_stats.raw_bytes_peak = g_memory_stats.raw_bytes_current;
    if (g_memory_stats.raw_blocks_current > g_memory_stats.raw_blocks_peak)
        g_memory_stats.raw_blocks_peak = g_memory_stats.raw_blocks_current;
}

static void raw_blocks_reset(void) {
    g_raw_blocks_count = 0;
    g_memory_stats.raw_blocks_current = 0;
    g_memory_stats.raw_bytes_current = 0;
    g_memory_stats.raw_blocks_peak = 0;
    g_memory_stats.raw_bytes_peak = 0;
}
#endif

#if MEMORY_PROFILING_ENABLED

#if defined(memory_profiler_track_raw_alloc)
#undef memory_profiler_track_raw_alloc
#endif
#if defined(memory_profiler_track_raw_free)
#undef memory_profiler_track_raw_free
#endif
#if defined(memory_profiler_track_raw_realloc)
#undef memory_profiler_track_raw_realloc
#endif
static MemoryHookFunc g_hook_func = NULL;

void memory_hooks_init(void) { g_hook_func = NULL; }
void memory_hooks_cleanup(void) { g_hook_func = NULL; }
void memory_hooks_register(MemoryHookFunc hook) { g_hook_func = hook; }
void memory_hooks_unregister(void) { g_hook_func = NULL; }
void memory_hook_trigger(MemoryHookType type, void *ptr, size_t size) {
    if (g_hook_func) g_hook_func(type, ptr, size);
}

static void memory_profiler_hook(MemoryHookType type, void *ptr, size_t size) {
    switch (type) {
        case MEMORY_HOOK_DEALLOCATION: MEMORY_PROFILER_TRACK_DEALLOCATION(size); break;
        case MEMORY_HOOK_RETAIN:       MEMORY_PROFILER_TRACK_RETAIN(ptr); break;
        case MEMORY_HOOK_RELEASE:      MEMORY_PROFILER_TRACK_RELEASE(ptr); break;
        case MEMORY_HOOK_AUTORELEASE:  MEMORY_PROFILER_TRACK_AUTORELEASE(ptr); break;
    }
}

void memory_profiling_init_with_hooks(void) {
    memory_profiler_init();
    memory_hooks_register(memory_profiler_hook);
}

void memory_profiling_cleanup_with_hooks(void) {
    memory_hooks_unregister();
    memory_profiler_cleanup();
}

void memory_test_start(const char *test_name) {
    memory_profiler_reset();
    if (g_memory_verbose_mode) printf("🔍 Memory Test Start: %s\n", test_name);
}

void memory_test_end(const char *test_name) {
    if (g_memory_stats.memory_leaks > 0 || g_memory_verbose_mode)
        memory_profiler_print_stats(test_name);
    memory_profiler_check_leaks(test_name);
}

void memory_profiler_init(void) {
    memset(&g_memory_stats, 0, sizeof(MemoryStats));
    memset(g_memory_stats.allocations_by_type, 0, sizeof(g_memory_stats.allocations_by_type));
    memset(g_memory_stats.deallocations_by_type, 0, sizeof(g_memory_stats.deallocations_by_type));
    memset(g_memory_stats.autoreleases_by_type, 0, sizeof(g_memory_stats.autoreleases_by_type));
}

void memory_profiler_reset(void) {
    memset(&g_memory_stats, 0, sizeof(MemoryStats));
    memset(g_memory_stats.allocations_by_type, 0, sizeof(g_memory_stats.allocations_by_type));
    memset(g_memory_stats.deallocations_by_type, 0, sizeof(g_memory_stats.deallocations_by_type));
    memset(g_memory_stats.retains_by_type, 0, sizeof(g_memory_stats.retains_by_type));
    memset(g_memory_stats.releases_by_type, 0, sizeof(g_memory_stats.releases_by_type));
    memset(g_memory_stats.autoreleases_by_type, 0, sizeof(g_memory_stats.autoreleases_by_type));
    memset(g_memory_stats.bytes_current_by_type, 0, sizeof(g_memory_stats.bytes_current_by_type));
    memset(g_memory_stats.bytes_peak_by_type, 0, sizeof(g_memory_stats.bytes_peak_by_type));
#ifdef DEBUG
    autorelease_pool_peak_reset();
#endif
}

void memory_profiler_cleanup(void) {
    if (g_memory_stats.memory_leaks > 0)
        LOGF(stdout, "⚠️  Memory Profiler: %zu potential memory leaks detected!\n", g_memory_stats.memory_leaks);
}

MemoryStats memory_profiler_get_stats(void) {
#ifdef DEBUG
    // In DEBUG, always return current/peak heap usage (even without full profiling)
    return g_memory_stats;
#else
    if (!g_memory_profiling_enabled) { MemoryStats e = {0}; return e; }
    return g_memory_stats;
#endif
}

static void update_memory_leak_stats(void) {
    if (g_memory_stats.total_allocations >= g_memory_stats.object_destructions)
        g_memory_stats.memory_leaks = g_memory_stats.total_allocations - g_memory_stats.object_destructions;
    else {
        g_memory_stats.memory_leaks = 0;
        if (g_memory_stats.object_destructions > g_memory_stats.total_allocations + 2) {
            static bool warn_once = false;
            if (!warn_once && g_memory_verbose_mode) {
                LOGF(stdout, "⚠️  WARNING: Potential double-free (dests %zu > allocs %zu).\n",
                     g_memory_stats.object_destructions, g_memory_stats.total_allocations);
                warn_once = true;
            }
        }
    }
}

// Hash-table for O(1) object tracking (replaces O(n) linear array)
typedef struct { void *ptr; size_t size; uint8_t type; } ObjBlock;
static ObjBlock *g_obj_ht = NULL;        // Hash table (open addressing)
static size_t g_obj_ht_capacity = 0;     // Always power of 2
static size_t g_obj_ht_count = 0;        // Number of entries

#define OBJ_HT_EMPTY   ((void*)0)
#define OBJ_HT_DELETED ((void*)(uintptr_t)-1)

static inline size_t obj_ht_hash(void *ptr) {
    uintptr_t x = (uintptr_t)ptr;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    return (size_t)((x >> 16) ^ x);
}

static bool obj_ht_grow(void) {
    size_t new_cap = (g_obj_ht_capacity == 0) ? 512 : g_obj_ht_capacity * 2;
    ObjBlock *new_ht = (ObjBlock*)calloc(new_cap, sizeof(ObjBlock));
    if (!new_ht) return false;
    // Rehash existing entries
    for (size_t i = 0; i < g_obj_ht_capacity; i++) {
        void *p = g_obj_ht[i].ptr;
        if (p && p != OBJ_HT_DELETED) {
            size_t idx = obj_ht_hash(p) & (new_cap - 1);
            while (new_ht[idx].ptr) idx = (idx + 1) & (new_cap - 1);
            new_ht[idx] = g_obj_ht[i];
        }
    }
    free(g_obj_ht);
    g_obj_ht = new_ht;
    g_obj_ht_capacity = new_cap;
    return true;
}

static void obj_blocks_track(void *ptr, size_t size, uint8_t type) {
    if (!ptr) return;
    if (g_obj_ht_count * 2 >= g_obj_ht_capacity && !obj_ht_grow()) return;
    size_t idx = obj_ht_hash(ptr) & (g_obj_ht_capacity - 1);
    while (g_obj_ht[idx].ptr && g_obj_ht[idx].ptr != OBJ_HT_DELETED && g_obj_ht[idx].ptr != ptr)
        idx = (idx + 1) & (g_obj_ht_capacity - 1);
    if (!g_obj_ht[idx].ptr || g_obj_ht[idx].ptr == OBJ_HT_DELETED) g_obj_ht_count++;
    g_obj_ht[idx] = (ObjBlock){ .ptr = ptr, .size = size, .type = type };
}

static bool obj_blocks_untrack(void *ptr, size_t *size_out, uint8_t *type_out) {
    if (!ptr || !g_obj_ht || g_obj_ht_capacity == 0) return false;
    size_t idx = obj_ht_hash(ptr) & (g_obj_ht_capacity - 1);
    for (size_t probes = 0; probes < g_obj_ht_capacity; probes++) {
        void *p = g_obj_ht[idx].ptr;
        if (!p) return false;  // Empty slot = not found
        if (p == ptr) {
            if (size_out) *size_out = g_obj_ht[idx].size;
            if (type_out) *type_out = g_obj_ht[idx].type;
            g_obj_ht[idx].ptr = OBJ_HT_DELETED;
            g_obj_ht_count--;
            return true;
        }
        idx = (idx + 1) & (g_obj_ht_capacity - 1);
    }
    return false;
}

void memory_profiler_track_deallocation(size_t size) {
    g_memory_stats.total_deallocations++;
    g_memory_stats.current_memory_usage = (g_memory_stats.current_memory_usage >= size)
        ? g_memory_stats.current_memory_usage - size : 0;
    update_memory_leak_stats();
}

void memory_profiler_track_object_creation(CljObject *obj) {
    memory_profiler_track_object_creation_sized(obj, sizeof(CljObject));
}

void memory_profiler_track_object_creation_sized(CljObject *obj, size_t size) {
    if (!obj) return;
    if (is_immediate((CljValue)obj) || is_singleton(obj)) return;
    
    size_t obj_size = (size > 0) ? size : sizeof(CljObject);
    { size_t r = platform_allocated_size(obj); if (r > 0) obj_size = r; }
    
#ifdef DEBUG
    // Lightweight heap tracking (always in DEBUG, even without full profiling)
    memory_track_heap_simple(obj, obj_size, true);
#endif
    
    // Full profiling (only when enabled)
    if (!g_memory_profiling_enabled) return;
    
    if (obj->type < 0 || obj->type >= CLJ_TYPE_COUNT) {
        fprintf(stderr, "memory_profiler: invalid type %d for object %p\n",
                (int)obj->type, (void*)obj);
        exception_print_native_backtrace();
        abort();
    }
    g_memory_stats.total_allocations++;
    if (obj->type < CLJ_TYPE_COUNT) {
        g_memory_stats.bytes_current_by_type[obj->type] += obj_size;
        if (g_memory_stats.bytes_current_by_type[obj->type] > g_memory_stats.bytes_peak_by_type[obj->type])
            g_memory_stats.bytes_peak_by_type[obj->type] = g_memory_stats.bytes_current_by_type[obj->type];
    }
    obj_blocks_track(obj, obj_size, obj->type);
    assert(obj->type >= 0 && obj->type < CLJ_TYPE_COUNT);
    g_memory_stats.allocations_by_type[obj->type]++;
}

void memory_profiler_track_object_destruction(CljObject *obj) {
    if (!obj) return;
    if (is_immediate((CljValue)obj) || is_singleton(obj)) return;
    
    size_t obj_size = sizeof(CljObject);
    { size_t r = platform_allocated_size(obj); if (r > 0) obj_size = r; }
    
    // Full profiling (only when enabled)
    if (!g_memory_profiling_enabled) {
#ifdef DEBUG
        // Lightweight heap tracking (always in DEBUG, even without full profiling)
        memory_track_heap_simple(obj, obj_size, false);
#endif
        return;
    }
    
    if (obj->type < 0 || obj->type >= CLJ_TYPE_COUNT) {
        fprintf(stderr, "memory_profiler: invalid type %d for object %p in destruction\n",
                (int)obj->type, (void*)obj);
        exception_print_native_backtrace();
        abort();
    }
    g_memory_stats.object_destructions++;
    uint8_t tt = obj->type;
    // Use stored size from hash table for consistency with creation
    (void)obj_blocks_untrack(obj, &obj_size, &tt);
#ifdef DEBUG
    memory_track_heap_simple(obj, obj_size, false);
#endif
    memory_profiler_track_deallocation(obj_size);
    if (tt < CLJ_TYPE_COUNT) {
        if (g_memory_stats.bytes_current_by_type[tt] >= obj_size)
            g_memory_stats.bytes_current_by_type[tt] -= obj_size;
        else
            g_memory_stats.bytes_current_by_type[tt] = 0;
    }
    assert(obj->type >= 0 && obj->type < CLJ_TYPE_COUNT);
    g_memory_stats.deallocations_by_type[obj->type]++;
}

void memory_profiler_track_object_zombify(CljObject *obj) {
    if (!obj) return;
    if (is_immediate((CljValue)obj) || is_singleton(obj)) return;
    
    size_t obj_size = sizeof(CljObject);
    { size_t r = platform_allocated_size(obj); if (r > 0) obj_size = r; }
    
    // Full profiling (only when enabled)
    if (!g_memory_profiling_enabled) {
#ifdef DEBUG
        // Lightweight heap tracking (always in DEBUG, even without full profiling)
        memory_track_heap_simple(obj, obj_size, false);
#endif
        return;
    }
    
    if (obj->type < 0 || obj->type >= CLJ_TYPE_COUNT) {
        fprintf(stderr, "memory_profiler: invalid type %d for object %p in zombify\n",
                (int)obj->type, (void*)obj);
        exception_print_native_backtrace();
        abort();
    }
    g_memory_stats.object_destructions++;
    uint8_t tt = obj->type;
    // Use stored size from hash table for consistency with creation
    (void)obj_blocks_untrack(obj, &obj_size, &tt);
#ifdef DEBUG
    memory_track_heap_simple(obj, obj_size, false);
#endif
    memory_profiler_track_deallocation(obj_size);
    if (tt < CLJ_TYPE_COUNT) {
        if (g_memory_stats.bytes_current_by_type[tt] >= obj_size)
            g_memory_stats.bytes_current_by_type[tt] -= obj_size;
        else
            g_memory_stats.bytes_current_by_type[tt] = 0;
    }
    assert(obj->type >= 0 && obj->type < CLJ_TYPE_COUNT);
    g_memory_stats.deallocations_by_type[obj->type]++;
}

void memory_profiler_track_retain(CljObject *obj) {
    if (!g_memory_profiling_enabled || !obj || is_immediate((CljValue)obj) || is_singleton(obj)) return;
    g_memory_stats.retain_calls++;
    if (obj->type < CLJ_TYPE_COUNT) g_memory_stats.retains_by_type[obj->type]++;
}

void memory_profiler_track_release(CljObject *obj) {
    if (!g_memory_profiling_enabled || !obj || is_immediate((CljValue)obj) || is_singleton(obj)) return;
    g_memory_stats.release_calls++;
    if (obj->type < CLJ_TYPE_COUNT) g_memory_stats.releases_by_type[obj->type]++;
}

void memory_profiler_track_autorelease(CljObject *obj) {
    if (!g_memory_profiling_enabled || !obj || is_immediate((CljValue)obj) || is_singleton(obj)) return;
    g_memory_stats.autorelease_calls++;
    if (obj->type < CLJ_TYPE_COUNT) g_memory_stats.autoreleases_by_type[obj->type]++;
}

void memory_profiler_track_raw_alloc(void *ptr, size_t size, const char *file, int line) {
    (void)file;(void)line;
    if (!g_memory_profiling_enabled || !ptr) return;
    g_memory_stats.raw_allocations++;
    size_t actual = size;
    { size_t r = platform_allocated_size(ptr); if (r > 0) actual = r; }
    long idx = raw_blocks_find(ptr);
    if (idx >= 0) {
        size_t old = g_raw_blocks[(size_t)idx].size;
        g_raw_blocks[(size_t)idx].size = actual;
        g_memory_stats.raw_bytes_current = (g_memory_stats.raw_bytes_current >= old) ? g_memory_stats.raw_bytes_current - old : 0;
        g_memory_stats.raw_bytes_current += actual;
        raw_blocks_update_peaks();
        g_memory_stats.current_memory_usage = (g_memory_stats.current_memory_usage >= old) ? g_memory_stats.current_memory_usage - old : 0;
        g_memory_stats.current_memory_usage += actual;
        if (g_memory_stats.current_memory_usage > g_memory_stats.peak_memory_usage)
            g_memory_stats.peak_memory_usage = g_memory_stats.current_memory_usage;
        return;
    }
    if (!raw_blocks_ensure_capacity(g_raw_blocks_count + 1)) return;
    g_raw_blocks[g_raw_blocks_count++] = (RawBlock){ .ptr = ptr, .size = actual };
    g_memory_stats.raw_blocks_current++;
    g_memory_stats.raw_bytes_current += actual;
    raw_blocks_update_peaks();
    g_memory_stats.current_memory_usage += actual;
    if (g_memory_stats.current_memory_usage > g_memory_stats.peak_memory_usage)
        g_memory_stats.peak_memory_usage = g_memory_stats.current_memory_usage;
}

void memory_profiler_track_raw_free(void *ptr, const char *file, int line) {
    (void)file;(void)line;
    if (!g_memory_profiling_enabled || !ptr) return;
    g_memory_stats.raw_frees++;
    long idx = raw_blocks_find(ptr);
    if (idx < 0) return;
    size_t old = g_raw_blocks[(size_t)idx].size;
    g_raw_blocks[(size_t)idx] = g_raw_blocks[--g_raw_blocks_count];
    if (g_memory_stats.raw_blocks_current > 0) g_memory_stats.raw_blocks_current--;
    g_memory_stats.raw_bytes_current = (g_memory_stats.raw_bytes_current >= old) ? g_memory_stats.raw_bytes_current - old : 0;
    g_memory_stats.current_memory_usage = (g_memory_stats.current_memory_usage >= old) ? g_memory_stats.current_memory_usage - old : 0;
}

void memory_profiler_track_raw_realloc(void *old_ptr, void *new_ptr, size_t new_size, const char *file, int line) {
    (void)file;(void)line;
    if (!g_memory_profiling_enabled) return;
    g_memory_stats.raw_reallocations++;
    if (!old_ptr) { memory_profiler_track_raw_alloc(new_ptr, new_size, file, line); return; }
    if (new_size == 0) { memory_profiler_track_raw_free(old_ptr, file, line); return; }
    if (old_ptr == new_ptr) {
        long idx = raw_blocks_find(old_ptr);
        if (idx >= 0) {
            size_t old = g_raw_blocks[(size_t)idx].size;
            g_raw_blocks[(size_t)idx].size = new_size;
            g_memory_stats.raw_bytes_current = (g_memory_stats.raw_bytes_current >= old) ? g_memory_stats.raw_bytes_current - old : 0;
            g_memory_stats.raw_bytes_current += new_size;
            raw_blocks_update_peaks();
            g_memory_stats.current_memory_usage = (g_memory_stats.current_memory_usage >= old) ? g_memory_stats.current_memory_usage - old : 0;
            g_memory_stats.current_memory_usage += new_size;
            if (g_memory_stats.current_memory_usage > g_memory_stats.peak_memory_usage)
                g_memory_stats.peak_memory_usage = g_memory_stats.current_memory_usage;
        } else
            memory_profiler_track_raw_alloc(new_ptr, new_size, file, line);
        return;
    }
    memory_profiler_track_raw_free(old_ptr, file, line);
    memory_profiler_track_raw_alloc(new_ptr, new_size, file, line);
}

static void print_memory_table(const MemoryStats *stats, const char *test_name, bool is_delta) {
    (void)test_name;
    if (!stats || !g_memory_verbose_mode) return;
    if (is_delta)
        LOGF(stdout, "📊 Memory Delta: Alloc:%+ld Dealloc:%+ld Peak:%+ld Current:%+ld Leaks:%+ld\n",
             (long)stats->total_allocations, (long)stats->total_deallocations,
             (long)stats->peak_memory_usage, (long)stats->current_memory_usage, (long)stats->memory_leaks);
    else
        LOGF(stdout, "📊 Memory: Alloc:%zu Dealloc:%zu Peak:%zu Current:%zu Leaks:%zu\n",
             stats->total_allocations, stats->total_deallocations,
             stats->peak_memory_usage, stats->current_memory_usage, stats->memory_leaks);
#ifdef DEBUG
    LOGF(stdout, "📌 Autorelease Peak Count: %u\n", (unsigned)autorelease_pool_peak_count());
#endif
    bool any = false;
    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        size_t a = stats->allocations_by_type[i], d = stats->deallocations_by_type[i];
        size_t r = stats->retains_by_type[i], rl = stats->releases_by_type[i], ar = stats->autoreleases_by_type[i];
        if (a || d || r || rl || ar) {
            any = true;
            LOGF(stdout, "📋 %s: Alloc:%zu Dealloc:%zu", clj_type_name((CljType)i), a, d);
            if (r) LOGF(stdout, " Retain:%zu", r);
            if (rl) LOGF(stdout, " Release:%zu", rl);
            if (ar) LOGF(stdout, " Autorelease:%zu", ar);
            LOGF(stdout, "\n");
        }
    }
    if (!any) {
        LOGF(stdout, "📋 Types: (no memory activity)\n");
        LOGF(stdout, "🔍 allocs=%zu deallocs=%zu retains=%zu releases=%zu autoreleases=%zu\n",
             stats->total_allocations, stats->total_deallocations, stats->retain_calls, stats->release_calls, stats->autorelease_calls);
    }
    if (stats->memory_leaks > 0)
        LOGF(stdout, "🚨 LEAK: %zu objects, %zu bytes\n", stats->memory_leaks, stats->current_memory_usage);
}

void memory_profiler_print_stats(const char *test_name) {
    if (!g_memory_profiling_enabled) return;
    print_memory_table(&g_memory_stats, test_name, false);
}

void memory_profiler_check_leaks(const char *location) {
    if (!g_memory_profiling_enabled) return;
    if (g_memory_stats.memory_leaks > 0 && g_memory_leak_reporting_enabled) {
        printf("\n🚨 MEMORY LEAK DETECTED at %s:\n", location ? location : "Unknown");
        printf("   │ Total Leaks: %10zu  Current: %10zu  Peak: %10zu\n", g_memory_stats.memory_leaks, g_memory_stats.current_memory_usage, g_memory_stats.peak_memory_usage);
#ifdef DEBUG
        printf("   │ Autorelease Peak: %10u\n", (unsigned)autorelease_pool_peak_count());
#endif
        printf("   │ Allocations: %10zu  Deallocations: %10zu\n", g_memory_stats.total_allocations, g_memory_stats.total_deallocations);
        for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
            size_t a = g_memory_stats.allocations_by_type[i], d = g_memory_stats.deallocations_by_type[i];
            size_t leaks = (a >= d) ? (a - d) : 0;
            if (leaks > 0) printf("🔍 %s: %zu leaks\n", clj_type_name((CljType)i), leaks);
        }
    }
}

bool memory_profiler_has_leaks(void) {
    return g_memory_profiling_enabled && g_memory_stats.memory_leaks > 0;
}

MemoryStats memory_profiler_diff_stats(const MemoryStats *after, const MemoryStats *before) {
    MemoryStats d = {0};
    d.total_allocations = after->total_allocations - before->total_allocations;
    d.total_deallocations = after->total_deallocations - before->total_deallocations;
    d.peak_memory_usage = after->peak_memory_usage - before->peak_memory_usage;
    d.current_memory_usage = after->current_memory_usage - before->current_memory_usage;
    d.object_destructions = after->object_destructions - before->object_destructions;
    d.retain_calls = after->retain_calls - before->retain_calls;
    d.release_calls = after->release_calls - before->release_calls;
    d.autorelease_calls = after->autorelease_calls - before->autorelease_calls;
    d.memory_leaks = after->memory_leaks - before->memory_leaks;
    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        d.allocations_by_type[i] = after->allocations_by_type[i] - before->allocations_by_type[i];
        d.deallocations_by_type[i] = after->deallocations_by_type[i] - before->deallocations_by_type[i];
        d.autoreleases_by_type[i] = after->autoreleases_by_type[i] - before->autoreleases_by_type[i];
    }
    return d;
}

void memory_profiler_print_diff(MemoryStats diff, const char *test_name) {
    print_memory_table(&diff, test_name, true);
}

#else
/* No-op when MEMORY_PROFILING_ENABLED=0 */
void memory_hooks_init(void) {}
void memory_hooks_cleanup(void) {}
void memory_hooks_register(MemoryHookFunc h) { (void)h; }
void memory_hooks_unregister(void) {}
void memory_hook_trigger(MemoryHookType t, void *p, size_t s) { (void)t;(void)p;(void)s; }
void memory_profiling_init_with_hooks(void) {}
void memory_profiling_cleanup_with_hooks(void) {}
void memory_test_start(const char *n) { (void)n; }
void memory_test_end(const char *n) { (void)n; }
void memory_profiler_init(void) {}
void memory_profiler_reset(void) {
#ifdef DEBUG
    // Reset lightweight heap tracking (always in DEBUG)
    g_memory_stats.current_memory_usage = 0;
    g_memory_stats.peak_memory_usage = 0;
    raw_blocks_reset();
#endif
}
void memory_profiler_cleanup(void) {}
MemoryStats memory_profiler_get_stats(void) {
#ifdef DEBUG
    // In DEBUG, always return current/peak heap usage (even without full profiling)
    return g_memory_stats;
#else
    MemoryStats e = {0}; return e;
#endif
}
void memory_profiler_print_stats(const char *n) { (void)n; }
void memory_profiler_track_deallocation(size_t s) { (void)s; }
void memory_profiler_track_object_creation(CljObject *o) { (void)o; }
void memory_profiler_track_object_creation_sized(CljObject *o, size_t s) {
#ifdef DEBUG
    // Lightweight heap tracking (always in DEBUG, even without full profiling)
    if (o && !is_immediate((CljValue)o) && !is_singleton(o)) {
        size_t obj_size = (s > 0) ? s : sizeof(CljObject);
        { size_t r = platform_allocated_size(o); if (r > 0) obj_size = r; }
        memory_track_heap_simple(o, obj_size, true);
    }
#else
    (void)o;(void)s;
#endif
}

void memory_profiler_track_object_destruction(CljObject *o) {
#ifdef DEBUG
    // Lightweight heap tracking (always in DEBUG, even without full profiling)
    if (o && !is_immediate((CljValue)o) && !is_singleton(o)) {
        size_t obj_size = sizeof(CljObject);
        { size_t r = platform_allocated_size(o); if (r > 0) obj_size = r; }
        memory_track_heap_simple(o, obj_size, false);
    }
#else
    (void)o;
#endif
}

void memory_profiler_track_object_zombify(CljObject *o) {
#ifdef DEBUG
    // Lightweight heap tracking: treat zombify as deallocation for heap stats
    if (o && !is_immediate((CljValue)o) && !is_singleton(o)) {
        size_t obj_size = sizeof(CljObject);
        { size_t r = platform_allocated_size(o); if (r > 0) obj_size = r; }
        memory_track_heap_simple(o, obj_size, false);
    }
#else
    (void)o;
#endif
}
void memory_profiler_track_retain(CljObject *o) { (void)o; }
void memory_profiler_track_release(CljObject *o) { (void)o; }
void memory_profiler_track_autorelease(CljObject *o) { (void)o; }
void memory_profiler_check_leaks(const char *l) { (void)l; }
bool memory_profiler_has_leaks(void) { return false; }
MemoryStats memory_profiler_diff_stats(const MemoryStats *a, const MemoryStats *b) { (void)a;(void)b; MemoryStats e = {0}; return e; }
void memory_profiler_print_diff(MemoryStats d, const char *n) { (void)d;(void)n; }

#endif

void enable_memory_profiling(bool enabled) {
#if MEMORY_PROFILING_ENABLED
    g_memory_profiling_enabled = enabled;
    if (enabled) memset(&g_memory_stats, 0, sizeof(MemoryStats));
    memory_set_debug_output_enabled(memory_get_debug_output_enabled());
#else
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

void set_memory_leak_reporting_enabled(bool enabled) { g_memory_leak_reporting_enabled = enabled; }
bool is_memory_leak_reporting_enabled(void) { return g_memory_leak_reporting_enabled; }

void set_memory_verbose_mode(bool verbose) {
#if MEMORY_PROFILING_ENABLED
    g_memory_verbose_mode = verbose;
    memory_set_debug_output_enabled(memory_get_debug_output_enabled());
#else
    (void)verbose;
#endif
}
