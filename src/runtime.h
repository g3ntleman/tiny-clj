/*
 * Runtime Header
 * 
 * Memory allocation macros and runtime constants for Tiny-Clj:
 * - STACK_ALLOC: Stack allocation using alloca() for temporary data
 * - ALLOC: Heap allocation using malloc() for persistent data
 * - ALLOC_ZERO: Zero-initialized heap allocation using calloc()
 * - Function call limits for STM32 compatibility
 * - Builtin function registration system
 */

#ifndef TINY_CLJ_RUNTIME_H
#define TINY_CLJ_RUNTIME_H

#include "object.h"
#include "memory.h"
#include "vector.h"
#include "namespace.h"
#include <alloca.h>
#include <stdlib.h>
#include <stdbool.h>

// Memory allocation macros
// Allocate `count` objects of type `type` on the stack
#define STACK_ALLOC(type, count) ((type*) alloca(sizeof(type) * (count)))

// Maximum number of function parameters (STM32-safe)
#define MAX_FUNCTION_PARAMS 32

// Maximum stack depth for function calls
// Increased to handle nested function calls like (update {:a 1} :a inc)
// Note: This is a safety limit - actual stack depth depends on function nesting
// Very high value to handle deeply nested Clojure function calls
// Note: This is a safety limit to prevent infinite recursion, not a hard limit
#define MAX_CALL_STACK_DEPTH 30

// Maximum autorelease pool depth
#define MAX_POOL_DEPTH 24

typedef ID (*BuiltinFn)(ID *args, unsigned int argc);

// Runtime state management
typedef struct TinyClJRuntime {
    // Namespaces
    CljMap *ns_registry;            // transient Map: Symbol → CljNamespace*
    CljMap *resolve_cache;          // Symbol resolution cache
    
    // Symbol Table
    CljVector *symbol_table;        // Vector of CljSymbol*
    
    // Meta Registry
    void *meta_registry;            // CljObject*
    
    // Autorelease Pool Stack
    CljVector *pool_stack;  // transient vector for autorelease pools
    int pool_stack_top;
    
    // Builtins
    bool builtins_registered;
    
    // Event Loop
    CljVector *task_queue;    // transient vector for normal tasks
    CljVector *timer_queue;  // transient vector for timer tasks
    int timer_id_counter;
} TinyClJRuntime;

// Statisch alloziertes globales Runtime-Struct
extern TinyClJRuntime g_runtime;

void runtime_init(TinyClJRuntime *runtime);
void runtime_reset(TinyClJRuntime *runtime);

// Legacy builtin functions removed - all builtins now use namespace registration

#endif
