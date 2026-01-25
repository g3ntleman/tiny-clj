#ifndef SUBJECTIVE_C_MEMORY_PROFILER_H
#define SUBJECTIVE_C_MEMORY_PROFILER_H

#include "object.h"
#include <stddef.h>
#include <stdbool.h>

// Feature switches (positive naming)
#ifndef MEMORY_PROFILER_ENABLED
#define MEMORY_PROFILER_ENABLED 1
#endif
#ifndef MEMORY_PROFILING_ENABLED
#define MEMORY_PROFILING_ENABLED 0
#endif

void memory_profiler_track_object_zombify(CljObject *obj);

typedef enum {
    MEMORY_HOOK_DEALLOCATION,
    MEMORY_HOOK_RETAIN,
    MEMORY_HOOK_RELEASE,
    MEMORY_HOOK_AUTORELEASE
} MemoryHookType;

typedef void (*MemoryHookFunc)(MemoryHookType type, void *ptr, size_t size);

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t total_allocations;
    size_t total_deallocations;
    size_t peak_memory_usage;
    size_t current_memory_usage;
    size_t object_destructions;
    size_t retain_calls;
    size_t release_calls;
    size_t autorelease_calls;
    size_t memory_leaks;
    size_t raw_bytes_current;
    size_t raw_bytes_peak;
    size_t raw_blocks_current;
    size_t raw_blocks_peak;
    size_t raw_allocations;
    size_t raw_frees;
    size_t raw_reallocations;
    size_t allocations_by_type[CLJ_TYPE_COUNT];
    size_t deallocations_by_type[CLJ_TYPE_COUNT];
    size_t retains_by_type[CLJ_TYPE_COUNT];
    size_t releases_by_type[CLJ_TYPE_COUNT];
    size_t autoreleases_by_type[CLJ_TYPE_COUNT];
    size_t bytes_current_by_type[CLJ_TYPE_COUNT];
    size_t bytes_peak_by_type[CLJ_TYPE_COUNT];
} MemoryStats;

extern MemoryStats g_memory_stats;
extern bool g_memory_verbose_mode;
extern bool g_memory_profiling_enabled;

void memory_hooks_init(void);
void memory_hooks_cleanup(void);
void memory_hooks_register(MemoryHookFunc hook);
void memory_hooks_unregister(void);
void memory_hook_trigger(MemoryHookType type, void *ptr, size_t size);
void memory_profiling_init_with_hooks(void);
void memory_profiling_cleanup_with_hooks(void);
void memory_profiler_init(void);
void memory_profiler_reset(void);
void memory_profiler_cleanup(void);
MemoryStats memory_profiler_get_stats(void);
void memory_profiler_print_stats(const char *test_name);
void enable_memory_profiling(bool enabled);
bool is_memory_profiling_enabled(void);
void set_memory_leak_reporting_enabled(bool enabled);
bool is_memory_leak_reporting_enabled(void);
void set_memory_verbose_mode(bool verbose);
void memory_profiler_track_deallocation(size_t size);
void memory_profiler_track_object_creation(CljObject *obj);
void memory_profiler_track_object_creation_sized(CljObject *obj, size_t size);
void memory_profiler_track_object_destruction(CljObject *obj);
void memory_profiler_track_retain(CljObject *obj);
void memory_profiler_track_release(CljObject *obj);
void memory_profiler_track_autorelease(CljObject *obj);

#if MEMORY_PROFILING_ENABLED
void memory_profiler_track_raw_alloc(void *ptr, size_t size, const char *file, int line);
void memory_profiler_track_raw_free(void *ptr, const char *file, int line);
void memory_profiler_track_raw_realloc(void *old_ptr, void *new_ptr, size_t new_size, const char *file, int line);
#endif

void memory_profiler_check_leaks(const char *location);
bool memory_profiler_has_leaks(void);
MemoryStats memory_profiler_diff_stats(const MemoryStats *after, const MemoryStats *before);
void memory_profiler_print_diff(MemoryStats diff, const char *test_name);
void memory_test_start(const char *test_name);
void memory_test_end(const char *test_name);

#if MEMORY_PROFILING_ENABLED
#define MEMORY_PROFILER_INIT() memory_profiler_init()
#define MEMORY_PROFILER_RESET() memory_profiler_reset()
#define MEMORY_PROFILER_CLEANUP() memory_profiler_cleanup()
#define MEMORY_PROFILER_PRINT_STATS(n) memory_profiler_print_stats(n)
#define MEMORY_PROFILER_CHECK_LEAKS(loc) memory_profiler_check_leaks(loc)
#define MEMORY_PROFILER_TRACK_DEALLOCATION(sz) memory_profiler_track_deallocation(sz)
#define MEMORY_PROFILER_TRACK_OBJECT_CREATION(o) memory_profiler_track_object_creation(o)
#define MEMORY_PROFILER_TRACK_OBJECT_DESTRUCTION(o) memory_profiler_track_object_destruction(o)
#define MEMORY_PROFILER_TRACK_RETAIN(o) memory_profiler_track_retain(o)
#define MEMORY_PROFILER_TRACK_RELEASE(o) memory_profiler_track_release(o)
#define MEMORY_PROFILER_TRACK_AUTORELEASE(o) memory_profiler_track_autorelease(o)
#define MEMORY_PROFILER_TRACK_OBJECT_ZOMBIFY(o) memory_profiler_track_object_zombify(o)
#define MEMORY_PROFILER_COMPARE_STATS(before, after, name) do { \
    MemoryStats _d = memory_profiler_diff_stats(&(after), &(before)); \
    memory_profiler_print_diff(_d, name); \
} while(0)
#else
#define MEMORY_PROFILER_INIT() ((void)0)
#define MEMORY_PROFILER_RESET() ((void)0)
#define MEMORY_PROFILER_CLEANUP() ((void)0)
#define MEMORY_PROFILER_PRINT_STATS(n) ((void)0)
#define MEMORY_PROFILER_CHECK_LEAKS(loc) ((void)0)
#define MEMORY_PROFILER_TRACK_DEALLOCATION(sz) ((void)0)
#define MEMORY_PROFILER_TRACK_OBJECT_CREATION(o) ((void)0)
#define MEMORY_PROFILER_TRACK_OBJECT_DESTRUCTION(o) ((void)0)
#define MEMORY_PROFILER_TRACK_RETAIN(o) ((void)0)
#define MEMORY_PROFILER_TRACK_RELEASE(o) ((void)0)
#define MEMORY_PROFILER_TRACK_AUTORELEASE(o) ((void)0)
#define MEMORY_PROFILER_TRACK_OBJECT_ZOMBIFY(o) ((void)0)
#define MEMORY_PROFILER_COMPARE_STATS(before, after, name) ((void)0)
#endif

#ifdef DEBUG
#define MEMORY_TEST_START(n) memory_test_start(n)
#define MEMORY_TEST_END(n) memory_test_end(n)
#define MEMORY_TEST_BENCHMARK_START(n) do { \
    MemoryStats before = memory_profiler_get_stats(); \
    printf("🔍 Memory Benchmark: %s\n", n); \
} while(0)
#define MEMORY_TEST_BENCHMARK_END(n) do { \
    MemoryStats after = memory_profiler_get_stats(); \
    MEMORY_PROFILER_COMPARE_STATS(before, after, n); \
} while(0)
#else
#define MEMORY_TEST_START(n) ((void)0)
#define MEMORY_TEST_END(n) ((void)0)
#define MEMORY_TEST_BENCHMARK_START(n) ((void)0)
#define MEMORY_TEST_BENCHMARK_END(n) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif
