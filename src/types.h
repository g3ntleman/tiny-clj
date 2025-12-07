#ifndef TINY_CLJ_TYPES_H
#define TINY_CLJ_TYPES_H

#include <stdint.h>

#ifdef DEBUG
#ifndef ZOMBIE_RC
/** @brief Zombie RC marker value (for NSZombieEnabled debugging).
 *  When an object's rc is set to ZOMBIE_RC, it indicates the object is a zombie
 *  (freed but not deallocated). The object's type remains unchanged for printing.
 *  Defined here to avoid circular dependencies between memory.h and object.h.
 *  Note: Using -1 as zombie marker, which is safe since normal rc values are >= 0.
 */
#define ZOMBIE_RC -1
#endif
#endif

/** @brief Singleton RC marker value.
 *  When an object's rc is set to SINGLETON_RC, it indicates the object is a singleton
 *  (statically allocated, never freed). Singletons should not be retained or released.
 *  Defined here to avoid circular dependencies between memory.h and object.h.
 *  Note: Using -2 as singleton marker, which is safe since normal rc values are >= 0
 *  and ZOMBIE_RC = -1.
 */
#define SINGLETON_RC -2

typedef enum {
    // Immediate types - use their tag values for consistency
    CLJ_INT = 1,      // TAG_FIXNUM
    CLJ_CHAR = 3,     // TAG_CHAR
    CLJ_BOOL = 5,     // TAG_BOOL
    CLJ_FLOAT = 7,    // TAG_FIXED
    
    // Heap object types - use non-conflicting even values
    CLJ_NIL = 0,      // Type for nil (nil is represented as NULL)
    CLJ_SYMBOL = 2,   // Interned symbols
    CLJ_STRING = 4,   // Strings
    CLJ_VECTOR = 6,   // Vectors
    CLJ_VECTOR_TRANSIENT_WEAK = 8,
    CLJ_MAP = 10,
    CLJ_LIST = 12,
    CLJ_AST_NODE = 14,
    CLJ_CALLSITE_CACHE = 16,
    CLJ_SEQ = 18,         // Sequence iterator
    CLJ_FUNC = 20,        // Native C functions
    CLJ_CLOSURE = 22,     // Interpreted Clojure functions
    CLJ_EXCEPTION = 24,
    CLJ_BYTE_ARRAY = 26,  // Mutable byte array
    CLJ_ATOM = 28,         // Mutable atom container
    CLJ_VECTOR_TRANSIENT = 30,
    CLJ_MAP_TRANSIENT = 32,
    CLJ_NAMESPACE = 34,    // Namespace objects
    CLJ_RAW_MEMORY = 36   // Raw memory allocations (ID arrays, etc.)
} CljType;

#define CLJ_TYPE_COUNT (CLJ_RAW_MEMORY + 1)

const char* clj_type_name(CljType type);

#endif
