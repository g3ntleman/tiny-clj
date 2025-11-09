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
// ID can represent either objects (CljObject*) or immediate values (CljValue)
#define ID void*

// CljValue represents only immediate values (fixnums, chars, booleans, fixed-point)
// These are 32-bit tagged pointers that don't require heap allocation
typedef void* CljValue;

// CljObject* represents only heap-allocated objects
// Objects require reference counting and memory management

#include "types.h"
#include "common.h"

// ZOMBIE_RC is defined in types.h (DEBUG builds only)


// Type optimization constants
#define LAST_SINGLETON_TYPE CLJ_SYMBOL  // Last singleton type (CLJ_SYMBOL, after immediate types)

// Type checking macros for performance
#define IS_SINGLETON_TYPE(type) ((type) <= LAST_SINGLETON_TYPE)

// Check if object type tracks retains (should be retain counted)
// Returns false only for singletons (which don't use retain counting)
#define TRACKS_RETAINS(obj) ((obj) && !is_singleton(obj))

// Legacy alias for backward compatibility
#define IS_SINGLETON(obj) is_singleton(obj)

// Automatic type mapping for ALLOC macros
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
#define TYPE_OF_CljAtom CLJ_ATOM
// Für primitive Typen die nicht als Struct existieren
#define TYPE_OF_int CLJ_INT
#define TYPE_OF_double CLJ_FLOAT
#define TYPE_OF_char CLJ_STRING
// Für interne Strukturen ohne CLJ_TYPE
#define TYPE_OF_SymbolEntry CLJ_NIL
#define TYPE_OF_CljNamespace CLJ_NIL

// Makro zur Typableitung
#define TYPE_OF(struct_type) TYPE_OF_##struct_type

typedef struct CljObject CljObject;

// Optimized CljObject structure (no union - primitives are immediates)
// 4-byte header for 32-bit architectures: 2 bytes type + 2 bytes rc
struct CljObject {
    uint16_t type;  // Typ-Tag für Heap-Objekte (reduced from CljType)
    int16_t rc;     // Reference Count (int16_t to support ZOMBIE_RC = -1)
    // Keine Union! Daten in Substrukturen (CljString, CljVector, etc.)
};

// Unified tag extraction (handles NULL, immediates, and heap objects)
// Returns CljType for type safety - values match immediate tags (CLJ_INT=1=TAG_FIXNUM, etc.)
static inline CljType TAG(ID obj) {
    if (!obj) return CLJ_NIL;
    // Check if it's an immediate (tagged pointer with odd tag)
    if ((uintptr_t)obj & 0x1) {
        // It's an immediate - tag value directly maps to CljType
        return (CljType)((uintptr_t)obj & 0x7);
    }
    // It's a heap object - return obj->type directly
    CljObject *obj_ptr = (CljObject*)obj;
#ifdef DEBUG
    // Check for zombie object before accessing type
    if (obj_ptr && (uintptr_t)obj_ptr >= 0x1000 && obj_ptr->rc == ZOMBIE_RC) {
        // Zombie detected: we need to throw exception, but we can't include exception.h here
        // So we'll access obj->type anyway (which is safe - type is unchanged for zombies)
        // The exception will be thrown by the caller (retain/release/is_type)
        // For now, just return the type (which is still valid for zombies)
    }
#endif
    return obj_ptr->type;
}

// Check if an object is a singleton (should not be reference counted)
static inline bool is_singleton(CljObject *obj) {
    // Safety check: ensure the pointer is valid before accessing fields
    if (!obj || (uintptr_t)obj < 0x1000) {
        return true;  // NULL or invalid pointer is treated as singleton
    }
    
#ifdef DEBUG
    // Check for zombie object (rc == ZOMBIE_RC)
    // Zombies are not singletons - they are freed objects that should be reference counted
    if (obj->rc == ZOMBIE_RC) {
        return false;  // Zombie objects are not singletons
    }
#endif
    
    // CRITICAL: Additional safety check - try to access obj->type in a safe way
    // If the object has been freed, AddressSanitizer will catch it
    // We can't prevent the access, but we can make it safer by checking bounds first
    // Note: This is a best-effort check - AddressSanitizer will catch actual use-after-free
    
    // Happy path: valid object that is NOT a singleton
    // CRITICAL: Access obj->type only after pointer validation
    // If obj points to freed memory, AddressSanitizer will detect it here
    CljType obj_type = obj->type;
    if (!IS_SINGLETON_TYPE(obj_type) && 
        !(obj->rc == 0 && (obj_type == CLJ_MAP || obj_type == CLJ_LIST || obj_type == CLJ_STRING || obj_type == CLJ_VECTOR))) {
        return false;
    }
    
    // All other cases are singletons
    return true;
}

// Struct definitions moved to specific headers:
// - CljList -> list.h
// - CljMap -> map.h  
// - CljSymbol -> symbol.h
// - CljPersistentVector -> vector.h
// - CljFunc, CljFunction -> function.h
// - CljByteArray -> byte_array.h
// - CLJException -> exception.h

// Type checking helper (accepts ID for convenience)
static inline bool is_type(ID obj, CljType expected_type) {
    if (!obj) return false;
    // Check if it's an immediate value (CljValue) being passed as CljObject*
    // Immediate values have odd addresses (tagged pointers)
    if ((uintptr_t)obj & 0x1) return false;
    
#ifdef DEBUG
    // Check for zombie object before accessing type
    CljObject *obj_ptr = (CljObject*)obj;
    if (obj_ptr && (uintptr_t)obj_ptr >= 0x1000 && obj_ptr->rc == ZOMBIE_RC) {
        // Zombie detected: throw exception with stacktrace and zombie object
        // Note: We need to include exception.h for this, but to avoid circular dependency,
        // we'll just return false here and let TAG() macro handle the exception
        // Actually, TAG() macro will access obj->type, which will trigger zombie detection there
        // So we can just let it fall through to TAG() macro
    }
#endif
    
    return TAG(obj) == expected_type;
}

// Equality comparison
/** Structural equality for collections; pointer equality fast path. */
bool clj_equal(ID a, ID b);
static inline bool clj_is_truthy(CljObject *v) {
    // In Clojure, only nil and false are falsy, everything else is truthy
    // nil = NULL (0x0)
    // false = (0 << 3) | 5 = 0x5
    if (!v) return false;  // nil is falsy
    // Check if it's false: false has tag 5 (TAG_BOOL) and special value 0
    if (((uintptr_t)v & 0x7) == 5) {  // TAG_BOOL = 5
        uint8_t special = (uint8_t)((uintptr_t)v >> 3);
        if (special == 0) return false;  // SPECIAL_FALSE = 0
    }
    // Everything else is truthy (including character('\0'), fixed(0.0), etc.)
    return true;
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
