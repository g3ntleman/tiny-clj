/*
 * CljObject Header
 * 
 * Core data structure definitions for Tiny-Clj:
 * - CljObject union representing all Clojure data types
 * - Function definitions with stack-allocated parameters
 * - Environment management for symbol bindings
 * - Meta-data registry for storing metadata
 * - Memory management with reference counting
 */

#ifndef TINY_CLJ_OBJECT_H
#define TINY_CLJ_OBJECT_H

#include "common.h"
#include "types.h"
#include <string.h>
#include <stdbool.h>
#include <errno.h>


// Forward declaration to avoid circular dependency
struct CljNamespace;

// Type optimization constants
#define LAST_SINGLETON_TYPE CLJ_SYMBOL  // Last singleton type (0-2)

// Type checking macros for performance
#define IS_SINGLETON_TYPE(type) ((type) <= LAST_SINGLETON_TYPE)

// Check if object type tracks references (should be reference counted)
// Returns false only for singletons (which don't use reference counting)
#define TRACKS_REFERENCES(obj) ((obj) && !IS_SINGLETON_TYPE((obj)->type))

// Legacy alias for backward compatibility
#define IS_SINGLETON(obj) ((obj) && IS_SINGLETON_TYPE((obj)->type))

typedef struct CljObject CljObject;
// Macro: safe type extraction (returns CLJ_UNKNOWN for NULL objects)
#define TYPE(object) ((object) ? (object)->type : CLJ_UNKNOWN)


// Optimized CljObject structure (tight union-based layout)
struct CljObject {
    CljType type;
    int rc;
    union {
        // Primitive values stored directly
        int i;
        double f;
        bool b;  // boolean
        
        // Complex objects referenced via pointer
        void* data;
    } as;
};

// Subtypes via struct embedding (composition/inheritance-like)
// Symbol name buffer size - fixed allocation for performance and memory
#define SYMBOL_NAME_MAX_LEN 32

typedef struct {
    CljObject base;         // Embedded base object
    struct CljNamespace *ns; // Direct reference to CLJNamespace (with reference counting)
    char name[SYMBOL_NAME_MAX_LEN]; // Fixed buffer for name
} CljSymbol;

typedef struct {
    CljObject base;         // Embedded base object
    int count;
    int capacity;
    int mutable_flag;
    CljObject **data;
} CljPersistentVector;

typedef struct {
    CljObject base;         // Embedded base object
    int count;
    int capacity;
    CljObject **data;  // [key1, val1, key2, val2, ...]
} CljMap;

typedef struct CljList {
    CljObject base;         // Embedded base object
    CljObject *head;
    struct CljList *tail;
} CljList;

// Safe accessor macros for CljList (Clojure-style)
#define LIST_FIRST(list) ((list) ? (list)->head : NULL)
#define LIST_REST(list) ((list) ? (list)->tail : NULL)

typedef struct {
    CljObject base;         // Embedded base object
    CljObject* (*fn)(CljObject **args, int argc);
    void *env;
    const char *name;       // Optional function name (for debugging/printing)
} CljFunc;

typedef struct {
    CljObject base;         // Embedded base object
    CljObject **params;     // Parameter symbols array
    int param_count;        // Number of parameters
    CljObject *body;        // Function body (AST)
    CljObject *closure_env; // Captured environment
    const char *name;       // Optional function name
} CljFunction;

// ============================================================================
// Exception structure with explicit reference counting
//
// Ownership & RC rules (pre-ARC-like model):
// - Exceptions MUST NOT use autorelease.
// - Creator starts with rc=1.
// - throw_exception transfers ownership to EvalState->last_error and longjmps.
// - The catch handler must take ownership from last_error,
//   set last_error=NULL and call release_exception(exc) exactly once.
// - Any embedded data (e.g., 'data' map) is retained when stored and
//   released in the exception finalizer.
// ============================================================================
typedef struct {
    int rc;                 // Reference Count
    const char *type;       // Exception type (e.g., "DoubleFreeError")
    const char *message;    // Error message
    const char *file;       // Source file
    int line;               // Source line
    int col;                // Source column
    CljObject *data;        // Additional data (map)
} CLJException;

/** Create an int object (rc=1). */
CljObject* make_int(int x);
/** Create a float object (rc=1). */
CljObject* make_float(double x);
/** Create a string object (rc=1), copies input with strdup. */
// make_nil() and make_bool() removed - use clj_nil(), clj_true(), clj_false() instead
// moved to vector.h
/** Create/intern a symbol with optional namespace; rc=1. */
CljObject* make_symbol(const char *name, const char *ns);
/** Convenience: create generic error exception object. */
CljObject* make_error(const char *message, const char *file, int line, int col);
/** Create a CLJException object (rc=1) with optional data. */
CljObject* make_exception(const char *type, const char *message, const char *file, int line, int col, CljObject *data);

// Exception throwing
/** Throw exception via longjmp; transfers ownership to runtime. */
void throw_exception(const char *type, const char *message, const char *file, int line, int col);
/** Throw exception with printf-style formatting; transfers ownership to runtime. */
void throw_exception_formatted(const char *type, const char *file, int line, int col, const char *format, ...);
/** Create interpreted function with params/body/closure; rc=1. */
CljObject* make_function(CljObject **params, int param_count, CljObject *body, CljObject *closure_env, const char *name);
/** Create empty list node (rc=1). */
CljList* make_list(CljObject *first, CljList *rest);

// Singleton access functions
/** Return global nil singleton (rc=0). */
CljObject* clj_nil();
/** Return global true singleton (rc=0). */
CljObject* clj_true();
/** Return global false singleton (rc=0). */
CljObject* clj_false();


/** Increase rc if applicable; ignored for singletons/primitives. */
void retain(CljObject *v);
/** Decrease rc and free at rc==0; ignored for singletons/primitives. */
void release(CljObject *v);

// String representation of CljObject
/** Return newly allocated C-string representation (caller frees). */
char* pr_str(CljObject *v);

// Equality comparison
/** Structural equality for collections; pointer equality fast path. */
bool clj_equal(CljObject *a, CljObject *b);
static inline bool clj_is_truthy(CljObject *v) {
    if (!v || v == clj_nil()) return false;
    if (v->type == CLJ_BOOL) return v->as.b;
    return true;
}

// Map operations (optimized with pointer fast paths)
/** Get value for key or NULL (structural key equality). */
CljObject* map_get(CljObject *map, CljObject *key);
/** Associate key->value (replaces existing; retains value). */
void map_assoc(CljObject *map, CljObject *key, CljObject *value);
/** Vector of keys (retained elements). */
CljObject* map_keys(CljObject *map);
/** Vector of values (retained elements). */
CljObject* map_vals(CljObject *map);
/** Number of entries. */
int map_count(CljObject *map);

// Optimized map operations with pointer comparisons
/** Append key/value without duplicate check (retains both). */
void map_put(CljObject *map, CljObject *key, CljObject *value);
/** Iterate pairs and call func(key,value) for each. */
void map_foreach(CljObject *map, void (*func)(CljObject*, CljObject*));
/** Returns 1 if key exists (may use pointer fast-path). */
int map_contains(CljObject *map, CljObject *key);
/** Remove key if present; releases removed items. */
void map_remove(CljObject *map, CljObject *key);

// Symbol interning with a real symbol table
typedef struct SymbolEntry {
    char *ns;
    char *name;
    CljObject *symbol;
    struct SymbolEntry *next;
} SymbolEntry;

extern SymbolEntry *symbol_table;

CljObject* intern_symbol(const char *ns, const char *name);
CljObject* intern_symbol_global(const char *name);  // Without namespace
void symbol_table_cleanup();
int symbol_count();

// Meta registry for metadata (only when ENABLE_META is defined)
#ifdef ENABLE_META
extern CljObject *meta_registry;

// Meta access functions
void meta_set(CljObject *v, CljObject *meta);
CljObject* meta_get(CljObject *v);
void meta_clear(CljObject *v);
void meta_registry_init();
void meta_registry_cleanup();
#else
// Stubs when meta is disabled
#define meta_set(v, meta) ((void)0)
#define meta_get(v) (NULL)
#define meta_clear(v) ((void)0)
#define meta_registry_init() ((void)0)
#define meta_registry_cleanup() ((void)0)
#endif

// Autorelease-pool API similar to obj.c
typedef struct CljObjectPool CljObjectPool;
/** Add object to current autorelease pool and return it. */
CljObject *autorelease(CljObject *v);
/** Push a new autorelease pool; returns pool handle. */
CljObjectPool *autorelease_pool_push();
/** Pop and drain current autorelease pool (most common usage). */
void autorelease_pool_pop(void);
/** Pop and drain specific autorelease pool (advanced usage). */
void autorelease_pool_pop_specific(CljObjectPool *pool);
/** Legacy API: Pop and drain given autorelease pool (backward compatibility). */
void autorelease_pool_pop_legacy(CljObjectPool *pool);
/** Drain all autorelease pools (global cleanup). */
void autorelease_pool_cleanup_all();

#define AUTORELEASE_POOL_SCOPE(name) for (CljObjectPool *(name) = autorelease_pool_push(); (name) != NULL; autorelease_pool_pop_specific(name), (name) = NULL)

// Function call helpers
/** Call function with argv; returns result or error object. */
CljObject* clj_call_function(CljObject *fn, int argc, CljObject **argv);
/** Apply function to array args in given env; returns result. */
CljObject* clj_apply_function(CljObject *fn, CljObject **args, int argc, CljObject *env);

// Environment helpers for function calls
/** Create child env extended with param/value bindings (stack impl.). */
CljObject* env_extend_stack(CljObject *parent_env, CljObject **params, CljObject **values, int count);
/** Lookup key in env and return value or NULL. */
CljObject* env_get_stack(CljObject *env, CljObject *key);
/** Set key->value in env (mutating env). */
void env_set_stack(CljObject *env, CljObject *key, CljObject *value);

// Exception handling
/** Set thread/global eval state for exception handling. */
void set_global_eval_state(void *state);

// Exception management with reference counting (analogous to CljVector)
/** Allocate CLJException with type/message/location and optional data. */
CLJException* create_exception(const char *type, const char *message, const char *file, int line, int col, CljObject *data);
/** Increment exception rc (not autoreleased). */
void retain_exception(CLJException *exception);
/** Decrement exception rc and free at rc==0. */
void release_exception(CLJException *exception);

// Polymorphic helpers for subtyping
/** Allocate CljObject shell with given type (no data). */
CljObject* create_object(CljType type);
/** Retain object (alias of retain). */
void retain_object(CljObject *obj);
/** Release object (alias of release). */
void release_object(CljObject *obj);
/** Free object memory immediately (no rc checks). */
void free_object(CljObject *obj);

// Type checking helper
static inline bool is_type(CljObject *obj, CljType expected_type) {
    return TYPE(obj) == expected_type;
}

// Debug macros - only include debug code in debug builds
#ifdef DEBUG
    #define DEBUG_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
    #define DEBUG_FPRINTF(stream, fmt, ...) fprintf(stream, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_PRINTF(fmt, ...) ((void)0)
    #define DEBUG_FPRINTF(stream, fmt, ...) ((void)0)
#endif

// STM32-optimized: Remove test code in STM32 builds
#ifdef STM32_BUILD
    #define STM32_PRINTF(fmt, ...) ((void)0)
    #define STM32_FPRINTF(stream, fmt, ...) ((void)0)
#else
    #define STM32_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
    #define STM32_FPRINTF(stream, fmt, ...) fprintf(stream, fmt, ##__VA_ARGS__)
#endif

// Type-safe casting (static inline for performance)
static inline CljSymbol* as_symbol(CljObject *obj) {
    return (TYPE(obj) == CLJ_SYMBOL) ? (CljSymbol*)obj : NULL;
}
static inline CljPersistentVector* as_vector(CljObject *obj) {
    return (TYPE(obj) == CLJ_VECTOR || TYPE(obj) == CLJ_WEAK_VECTOR) ? (CljPersistentVector*)obj : NULL;
}
static inline CljMap* as_map(CljObject *obj) {
    return (TYPE(obj) == CLJ_MAP) ? (CljMap*)obj->as.data : NULL;
}
static inline CljList* as_list(CljObject *obj) {
    return (TYPE(obj) == CLJ_LIST) ? (CljList*)obj : NULL;
}
static inline CljFunction* as_function(CljObject *obj) {
    return (TYPE(obj) == CLJ_FUNC) ? (CljFunction*)obj : NULL;
}

// Helper: check if a function object is native (CljFunc) or interpreted (CljFunction)
static inline int is_native_fn(CljObject *fn) {
    // Native builtins are represented as CljFunc; interpreted functions as CljFunction
    return TYPE(fn) == CLJ_FUNC && ((CljFunction*)fn)->params == NULL && ((CljFunction*)fn)->body == NULL && ((CljFunction*)fn)->closure_env == NULL;
}

// Helper function to check if autorelease pool is active
bool is_autorelease_pool_active(void);

#endif
