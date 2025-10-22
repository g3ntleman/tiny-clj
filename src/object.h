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

// value.h included later to avoid circular dependency

#ifndef TINY_CLJ_OBJECT_H
#define TINY_CLJ_OBJECT_H

// Forward declaration for ID type to avoid circular dependency
typedef void* ID;

#include "common.h"
#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>


// Forward declaration to avoid circular dependency
struct CljNamespace;

// Type optimization constants
#define LAST_SINGLETON_TYPE CLJ_SYMBOL  // Last singleton type (0-2)

// Type checking macros for performance
#define IS_SINGLETON_TYPE(type) ((type) <= LAST_SINGLETON_TYPE)

// Check if object type tracks retains (should be retain counted)
// Returns false only for singletons (which don't use retain counting)
#define TRACKS_RETAINS(obj) ((obj) && !is_singleton(obj))

// Legacy alias for backward compatibility
#define IS_SINGLETON(obj) is_singleton(obj)

// Automatic type mapping for ALLOC macros
#define TYPE_OF_CljObject CLJ_UNKNOWN
#define TYPE_OF_CljList CLJ_LIST
#define TYPE_OF_CljSymbol CLJ_SYMBOL
#define TYPE_OF_CljFunction CLJ_CLOSURE
#define TYPE_OF_CljFunc CLJ_FUNC
#define TYPE_OF_CljPersistentVector CLJ_VECTOR
#define TYPE_OF_CljPersistentMap CLJ_MAP
#define TYPE_OF_CljMap CLJ_MAP
#define TYPE_OF_CLJException CLJ_EXCEPTION
#define TYPE_OF_CljSeqIterator CLJ_SEQ
// Für primitive Typen die nicht als Struct existieren
#define TYPE_OF_int CLJ_INT
#define TYPE_OF_double CLJ_FLOAT
#define TYPE_OF_char CLJ_STRING
// Für interne Strukturen ohne CLJ_TYPE
#define TYPE_OF_SymbolEntry CLJ_UNKNOWN
#define TYPE_OF_CljNamespace CLJ_UNKNOWN
#define TYPE_OF_EvalState CLJ_UNKNOWN
#define TYPE_OF_CljObjectPool CLJ_UNKNOWN

// Makro zur Typableitung
#define TYPE_OF(struct_type) TYPE_OF_##struct_type

typedef struct CljObject CljObject;
// Macro: safe type extraction (returns CLJ_UNKNOWN for NULL objects)
#define TYPE(object) ((object) ? (object)->type : CLJ_UNKNOWN)


// Optimized CljObject structure (no union - primitives are immediates)
// 4-byte header for 32-bit architectures: 2 bytes type + 2 bytes rc
struct CljObject {
    uint16_t type;  // Typ-Tag für Heap-Objekte (reduced from CljType)
    uint16_t rc;    // Reference Count (reduced from int)
    // Keine Union! Daten in Substrukturen (CljString, CljVector, etc.)
};

// Check if an object is a singleton (should not be reference counted)
static inline bool is_singleton(CljObject *obj) {
    if (!obj) return false;
    
    // Safety check: ensure the pointer is valid and points to a valid object
    // Check if the pointer is in a reasonable memory range (not in zero page)
    if ((uintptr_t)obj < 0x1000) return false;
    
    // Standard singleton types
    if (IS_SINGLETON_TYPE(obj->type)) return true;
    
    // Special case: empty map singleton (rc == 0)
    if (obj->type == CLJ_MAP && obj->rc == 0) return true;
    
    return false;
}

// Subtypes via struct embedding (composition/inheritance-like)
// Symbol name buffer size - fixed allocation for performance and memory
#define SYMBOL_NAME_MAX_LEN 32

typedef struct {
    CljObject base;         // Embedded base object
    struct CljNamespace *ns; // Direct reference to CLJNamespace (with reference counting)
    const char *name;       // Pointer to string literal in .rodata segment
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

// CljList represents a Clojure-style linked list with flexible rest handling:
// - first: The first element of the list (can be any CljObject)
// - rest: The rest of the list, which can be:
//   * Another CljList (proper list)
//   * A CljSeq (sequence iterator)
//   * Any other CljObject (improper list, e.g., for dotted pairs)
//   * NULL (empty rest)
typedef struct CljList {
    CljObject base;         // Embedded base object
    CljObject *first;      // First element of the list
    CljObject *rest;       // Rest of the list (can be a CljList, CljSeq, or any CljObject for improper lists)
} CljList;

// Safe accessor macros for CljList (Clojure-style)
#define LIST_FIRST(list) ((list) ? (list)->first : NULL)
#define LIST_REST(list) ((list) ? (list)->rest : NULL)  // Returns CljObject* (can be CljList, CljSeq, or other)

// ID typedef is defined in value.h

typedef struct {
    CljObject base;         // Embedded base object
    CljObject* (*fn)(CljObject **args, int argc);  // Keep old signature for now
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
// Exception structure as CljObject subtype (embedded pattern)
//
// Ownership & RC rules:
// - Uses standard CljObject reference counting via base.rc
// - Creator starts with base.rc=1.
// - throw_exception transfers ownership to EvalState->last_error and longjmps.
// - The catch handler must take ownership from last_error,
//   set last_error=NULL and call release((CljObject*)exc) exactly once.
// - Any embedded data (e.g., 'data' map) is retained when stored and
//   released in the exception finalizer.
// ============================================================================
typedef struct {
    CljObject base;         // Embedded base object
    char type[64];           // Exception type (e.g., "DoubleFreeError") - embedded string
    char message[256];       // Error message - embedded string
    char file[128];          // Source file - embedded string
    int line;               // Source line
    int col;                // Source column
} CLJException;

// make_int() and make_float() removed - use fixnum() and make_fixed() instead
/** Create a string object (rc=1), copies input with strdup. */
// Function wrappers moved to value.h to avoid circular dependency
// moved to vector.h
// make_symbol_old declaration removed - use make_symbol from value.h instead
/** Convenience: create generic error exception object. */
CljObject* make_error(const char *message, const char *file, int line, int col);
/** Create a CLJException object (rc=1) with optional data. */
CLJException* make_exception(const char *type, const char *message, const char *file, int line, int col);
/** Create a CljObject wrapper for CLJException (rc=1) with optional data. */
CljObject* make_exception_wrapper(const char *type, const char *message, const char *file, int line, int col);

// Exception throwing
/** Throw exception via longjmp; transfers ownership to runtime. */
void throw_exception(const char *type, const char *message, const char *file, int line, int col);
/** Throw exception with printf-style formatting; transfers ownership to runtime. */
void throw_exception_formatted(const char *type, const char *file, int line, int col, const char *format, ...);
/** Create interpreted function with params/body/closure; rc=1. */
CljObject* make_function(CljObject **params, int param_count, CljObject *body, CljObject *closure_env, const char *name);
/** Create empty list node (rc=1). */
CljObject* make_list(CljObject *first, CljObject *rest);

// Singleton access functions
// clj_nil(), clj_true(), clj_false() are now macros defined above


// Memory management functions moved to memory.h

// String representation of CljObject
/** Return newly allocated C-string representation (caller frees). */
char* pr_str(CljObject *v);
char* to_string(CljObject *v);

// Type checking helper
static inline bool is_type(CljObject *obj, CljType expected_type) {
    if (!obj) return false;
    // Check if it's an immediate value (CljValue) being passed as CljObject*
    // Immediate values have odd addresses (tagged pointers)
    if ((uintptr_t)obj & 0x1) return false;
    return TYPE(obj) == expected_type;
}

// Equality comparison
/** Structural equality for collections; pointer equality fast path. */
bool clj_equal(CljObject *a, CljObject *b);
bool clj_equal_id(ID a, ID b);
static inline bool clj_is_truthy(CljObject *v) {
    // Ultra-schneller Bit-Trick: nil(0) und false(5) haben Byte < 8
    return ((uintptr_t)v & 0xFF) >= 8;
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
SymbolEntry* symbol_table_add(const char *ns, const char *name, CljObject *symbol);
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

// Autorelease-pool API moved to memory.h

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
// Note: set_global_eval_state() removed - Exception handling now independent of EvalState

// Exception management with reference counting (analogous to CljVector)
/** Allocate CLJException with type/message/location and optional data. */


// Polymorphic helpers for subtyping
/** Allocate CljObject shell with given type (no data). */
CljObject* create_object(CljType type);
// Old memory management functions removed - use RETAIN/RELEASE macros instead

// Debug macros removed - no debug output in any builds

// STM32-optimized: Remove test code in STM32 builds
#ifdef STM32_BUILD
    #define STM32_PRINTF(fmt, ...) ((void)0)
    #define STM32_FPRINTF(stream, fmt, ...) ((void)0)
#else
    #define STM32_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
    #define STM32_FPRINTF(stream, fmt, ...) fprintf(stream, fmt, ##__VA_ARGS__)
#endif

// Type-safe casting with exception throwing (DRY principle)
static inline void* assert_type(CljObject *obj, CljType expected_type) {
    if (!is_type(obj, expected_type)) {
#ifdef DEBUG
        // Direct error output with expected and actual types
        const char *actual_type = obj ? "Object" : "NULL";
        const char *expected_type_name = "Expected";
        fprintf(stderr, "Assertion failed: Expected %s, got %s at %s:%d\n", 
                expected_type_name, actual_type, __FILE__, __LINE__);
#endif
        abort();
    }
    return obj;
}

// Type-safe casting (static inline for performance)
static inline CljSymbol* as_symbol(CljObject *obj) {
    return (CljSymbol*)assert_type(obj, CLJ_SYMBOL);
}
static inline CljPersistentVector* as_vector(CljObject *obj) {
    if (!is_type(obj, CLJ_VECTOR) && !is_type(obj, CLJ_WEAK_VECTOR) && !is_type(obj, CLJ_TRANSIENT_VECTOR)) {
#ifdef DEBUG
        const char *actual_type = obj ? "Vector" : "NULL";
        fprintf(stderr, "Assertion failed: Expected Vector, got %s at %s:%d\n", 
                actual_type, __FILE__, __LINE__);
#endif
        abort();
    }
    return (CljPersistentVector*)obj;
}
static inline CljMap* as_map(CljObject *obj) {
    if (!is_type(obj, CLJ_MAP) && !is_type(obj, CLJ_TRANSIENT_MAP)) {
#ifdef DEBUG
        const char *actual_type = obj ? "Vector" : "NULL";
        fprintf(stderr, "Assertion failed: Expected Map, got %s at %s:%d\n", 
                actual_type, __FILE__, __LINE__);
#endif
        abort();
    }
    return (CljMap*)obj;
}
static inline CljList* as_list(CljObject *obj) {
    if (!is_type(obj, CLJ_LIST)) {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected List, got %s", 
                "List");
        throw_exception("TypeError", error_msg, __FILE__, __LINE__, 0);
    }
    return (CljList*)obj;
}
static inline CljFunction* as_function(CljObject *obj) {
    return (CljFunction*)assert_type(obj, CLJ_CLOSURE);
}
static inline CLJException* as_exception(CljObject *obj) {
    return (CLJException*)assert_type(obj, CLJ_EXCEPTION);
}

// Helper: check if a function object is native (CljFunc) or interpreted (CljFunction)
static inline int is_native_fn(CljObject *fn) {
    // Native builtins are represented as CljFunc; interpreted functions as CljFunction
    if (TYPE(fn) != CLJ_FUNC) return 0;
    
    // Additional check: native functions have a function pointer
    CljFunc *native_func = (CljFunc*)fn;
    return native_func->fn != NULL;
}

// is_autorelease_pool_active() function moved to memory.h

// value.h included separately to avoid circular dependency

#endif
