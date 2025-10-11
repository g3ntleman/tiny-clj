/*
 * Memory Hooks for Clean Profiling
 * 
 * This header provides a clean way to hook into memory operations
 * without polluting the main code with profiling calls.
 */

#ifndef TINY_CLJ_MEMORY_HOOKS_H
#define TINY_CLJ_MEMORY_HOOKS_H

#include <stddef.h>
#include <stdbool.h>

// Forward declaration for CljObject (avoid redefinition)
#ifndef CLJ_OBJECT_DEFINED
struct CljObject;
// CljObject is already defined in CljObject.h
#endif

// Memory operation types
typedef enum {
    MEMORY_HOOK_ALLOCATION,
    MEMORY_HOOK_DEALLOCATION,
    MEMORY_HOOK_OBJECT_CREATION,
    MEMORY_HOOK_OBJECT_DESTRUCTION,
    MEMORY_HOOK_RETAIN,
    MEMORY_HOOK_RELEASE,
    MEMORY_HOOK_AUTORELEASE
} MemoryHookType;

// Hook function type
typedef void (*MemoryHookFunc)(MemoryHookType type, void *ptr, size_t size);

// Hook management
void memory_hooks_init(void);
void memory_hooks_cleanup(void);
void memory_hooks_register(MemoryHookFunc hook);
void memory_hooks_unregister(void);

// Hook triggers (to be called from memory operations)
void memory_hook_trigger(MemoryHookType type, void *ptr, size_t size);

// Memory profiling integration
void memory_profiling_init_with_hooks(void);
void memory_profiling_cleanup_with_hooks(void);

// Test helpers (for backward compatibility)
void memory_test_start(const char *test_name);
void memory_test_end(const char *test_name);

// Memory management macros are now defined in memory.h
// This file only provides the hook infrastructure

#endif // TINY_CLJ_MEMORY_HOOKS_H
