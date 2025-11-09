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
    CLJ_WEAK_VECTOR = 8,
    CLJ_MAP = 10,
    CLJ_LIST = 12,
    CLJ_SEQ = 14,         // Sequence iterator
    CLJ_FUNC = 16,        // Native C functions
    CLJ_CLOSURE = 18,     // Interpreted Clojure functions
    CLJ_EXCEPTION = 20,
    CLJ_BYTE_ARRAY = 22,  // Mutable byte array
    CLJ_ATOM = 24,         // Mutable atom container
    CLJ_TRANSIENT_VECTOR = 26,
    CLJ_TRANSIENT_MAP = 28,
    CLJ_RAW_MEMORY = 32   // Raw memory allocations (ID arrays, etc.)
} CljType;

#define CLJ_TYPE_COUNT (CLJ_RAW_MEMORY + 1)

const char* clj_type_name(CljType type);

#endif
