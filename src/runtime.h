/*
 * Runtime Header
 * 
 * Memory allocation macros and runtime constants for Tiny-Clj:
 * - STACK_ALLOC: Stack allocation using alloca() for temporary data
 * - ALLOC: Heap allocation using malloc() for persistent data
 * - Function call limits for STM32 compatibility
 * - Builtin function registration system
 */

#ifndef TINY_CLJ_RUNTIME_H
#define TINY_CLJ_RUNTIME_H

#include "object.h"
#include "memory.h"
#include "vector.h"
#include "map.h"
#include <alloca.h>
#include <stdlib.h>
#include <stdbool.h>

// Forward declarations to avoid circular dependencies
// Note: These are only needed when runtime.h is included before namespace.h/symbol.h
// C11 allows identical typedef redefinitions, but compiler warns - suppress warning
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtypedef-redefinition"
#ifndef TINY_CLJ_NAMESPACE_H
typedef struct CljNamespace CljNamespace;
#else
// CljNamespace already defined in namespace.h
#endif
#ifndef TINY_CLJ_SYMBOLS_H
typedef struct SymbolEntry SymbolEntry;
#else
// SymbolEntry already defined in symbol.h
#endif
#pragma GCC diagnostic pop

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
#define MAX_CALL_STACK_DEPTH 1000

// Maximum autorelease pool depth
#define MAX_POOL_DEPTH 24

typedef ID (*BuiltinFn)(ID *args, unsigned int argc);

// Runtime state management
typedef struct TinyClJRuntime {
    // Namespaces
    CljMap *ns_registry;      // transient Map: Symbol (namespace name) → CljNamespace*
    CljNamespace *clojure_core_cache;
    
    // Symbol Table
    SymbolEntry *symbol_table;
    
    // Meta Registry
    CljMap *meta_registry;
    
    // Autorelease Pool Stack (transient vector)
    CljVector *pool_stack;
    
    // Builtins
    bool builtins_registered;
    
    // Event Loop
    CljVector *task_queue;    // transient vector for normal tasks
    CljVector *timer_queue;  // transient vector for timer tasks
    int timer_id_counter;          // counter for unique timer IDs
} TinyClJRuntime;

// Statisch alloziertes globales Runtime-Struct
extern TinyClJRuntime g_runtime;

void runtime_init(TinyClJRuntime *runtime);
void runtime_free(TinyClJRuntime *runtime);

// Legacy builtin functions removed - all builtins now use namespace registration

#endif
