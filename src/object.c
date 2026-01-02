/*
 * CljObject Implementation
 * 
 * Core data structure for Tiny-Clj representing all Clojure values:
 * - Basic types: symbols, keywords, numbers, strings, booleans
 * - Data structures: lists, vectors, maps, sets
 * - Functions: user-defined and built-in functions
 * - Meta-data support with global registry
 * - Reference counting for memory management
 * - Stack-allocated function call system
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>
#include <subjective-c/object.h>
#include "memory.h"
#include "runtime.h"
#include "seq.h"
#include <subjective-c/map.h>
#include "atom.h"
#include "kv_macros.h"
#include "namespace.h"
#include "exception.h"  // For ExceptionHandler definition
#include <subjective-c/strings.h>
#include "function.h"
#include "list.h"
#include "symbol.h"
#include <subjective-c/vector.h>
#include "byte_array.h"
#include "value.h"  // For string_empty_singleton

// release_object_deep() function moved to memory.c

// Memory management functions moved to memory.c

// Helper functions for optimized structure
// Note: is_primitive_type function replaced by IS_PRIMITIVE_TYPE macro in header

// Note: set_global_eval_state() removed - Exception handling now independent of EvalState

// Exception creation moved to exception.c





// Function creation moved to function.c
// List creation moved to list.c

// String-Formatierungsfunktionen nach strings.c ausgelagert
// Equality-Funktion nach equality.c ausgelagert

// Map functions are implemented in map.c

// Symbol table functions are implemented in symbol.c

// Metadata functions moved to meta.c

// Static singletons - these live forever and are never freed
// Note: nil is now represented as NULL, true/false as immediate values
// empty_map, empty_vector, and empty_string singletons are now statically initialized
// in their respective source files (map.c, vector.c, strings.c)

// Singleton access functions
// Function wrappers moved to object.h as macros

// clj_false() removed - use make_special(SPECIAL_FALSE) instead

// clj_empty_vector moved to vector.c

// clj_empty_map() no longer part of public API; make_map_old(0) returns singleton

// Environment functions moved to environment.c
// Function call functions moved to function_call.c


// Old memory management functions removed - use RETAIN/RELEASE macros instead

// Type-safe Casting now provided as static inline in header

/**
 * @brief Get the reference count of an object
 * @param obj The object to check (can be NULL)
 * @return The reference count, or -1 if obj is NULL, SINGLETON_RC if it's a singleton, or ZOMBIE_RC if it's a zombie
 */
int reference_count(CljObject *obj) {
    if (!obj) {
        return -1;  // NULL has no reference count
    }
    
    // Check if it's an immediate value (tagged pointer)
    if ((uintptr_t)obj & 0x1) {
        return -1;  // Immediate values don't have reference counts
    }
    
    return obj->rc;
}
