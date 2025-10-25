/*
 * CljObject Header
 * 
 * Core data structure definitions for Tiny-Clj:
 * - CljObject base structure for all Clojure data types
 * - Type checking and casting utilities
 * - Memory management with reference counting
 */

#ifndef TINY_CLJ_OBJECT_H
#define TINY_CLJ_OBJECT_H

// Forward declaration for ID type to avoid circular dependency
typedef void* ID;

// Forward declaration for CljValue to avoid circular dependency
typedef struct CljObject* CljValue;

#include "types.h"
#include "common.h"


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
#define TYPE_OF_CljByteArray CLJ_BYTE_ARRAY
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

// Struct definitions moved to specific headers:
// - CljList -> list.h
// - CljMap -> map.h  
// - CljSymbol -> symbol.h
// - CljPersistentVector -> vector.h
// - CljFunc, CljFunction -> function.h
// - CljByteArray -> byte_array.h
// - CLJException -> exception.h

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
bool clj_equal(CljValue a, CljValue b);
bool clj_equal_id(ID a, ID b);
static inline bool clj_is_truthy(CljObject *v) {
    // Ultra-schneller Bit-Trick: nil(0) und false(5) haben Byte < 8
    return ((uintptr_t)v & 0xFF) >= 8;
}

// Specific function implementations moved to their respective headers:
// - Map operations -> map.h
// - Symbol functions -> symbol.h
// - Meta functions -> meta.h
// - Autorelease-pool API -> memory.h
// - Function call helpers -> function.h
// - Environment functions -> environment.h

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

// Type-safe casting functions moved to specific headers:
// - as_symbol() -> symbol.h
// - as_vector() -> vector.h  
// - as_map() -> map.h
// - as_list() -> list.h
// - as_function() -> function.h
// - as_exception() -> exception.h
// - as_byte_array() -> byte_array.h
// - is_native_fn() -> function.h
// - is_autorelease_pool_active() -> memory.h

#endif
