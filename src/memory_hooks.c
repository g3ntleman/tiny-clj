/*
 * Memory Hooks Implementation
 * 
 * Clean implementation of memory hooks for profiling
 */

#include "memory_hooks.h"
#include "memory_profiler.h"
#include <stdio.h>

// Global hook function (only one hook supported for simplicity)
static MemoryHookFunc g_hook_func = NULL;

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
        case MEMORY_HOOK_ALLOCATION:
            memory_profiler_track_allocation(size);
            break;
        case MEMORY_HOOK_DEALLOCATION:
            memory_profiler_track_deallocation(size);
            break;
        case MEMORY_HOOK_OBJECT_CREATION:
            memory_profiler_track_object_creation((CljObject*)ptr);
            break;
        case MEMORY_HOOK_OBJECT_DESTRUCTION:
            memory_profiler_track_object_destruction((CljObject*)ptr);
            break;
        case MEMORY_HOOK_RETAIN:
            memory_profiler_track_retain((CljObject*)ptr);
            break;
        case MEMORY_HOOK_RELEASE:
            memory_profiler_track_release((CljObject*)ptr);
            break;
        case MEMORY_HOOK_AUTORELEASE:
            memory_profiler_track_autorelease((CljObject*)ptr);
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

// Test helpers for backward compatibility
void memory_test_start(const char *test_name) {
    memory_profiler_reset();
    printf("🔍 Memory Profiling: %s\n", test_name);
}

void memory_test_end(const char *test_name) {
    memory_profiler_print_stats(test_name);
    memory_profiler_check_leaks(test_name);
}
