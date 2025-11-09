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
    // Immediate types (for tagged pointers, not used in heap objects)
    CLJ_INT,     // Immediate integer (fixnum)
    CLJ_CHAR,    // Immediate character
    CLJ_BOOL,    // Immediate boolean (true/false)
    CLJ_FLOAT,   // Immediate float (fixed-point)
    // Singletons (0-1) - no reference counting needed, can use simple range check
    CLJ_SYMBOL,  // Interned symbols - no reference counting needed
    // Complex types (2+) - require individual checks, have reference counting
    CLJ_STRING,
    CLJ_VECTOR,
    CLJ_WEAK_VECTOR,
    CLJ_MAP,
    CLJ_LIST,
    CLJ_SEQ,         // Sequence iterator (embedded CljSeqIterator)
    CLJ_FUNC,        // Native C functions (CljFunc)
    CLJ_CLOSURE,     // Interpreted Clojure functions (CljFunction)
    CLJ_EXCEPTION,
    CLJ_BYTE_ARRAY,  // Mutable byte array (Clojure-compatible)
    CLJ_ATOM,         // Mutable atom container (Clojure-compatible)
    // Transient types (Clojure-kompatibel: nur Vector und Map)
    CLJ_TRANSIENT_VECTOR,
    CLJ_TRANSIENT_MAP,
    CLJ_UNKNOWN  // Unknown/invalid type sentinel (should not occur at runtime)
} CljType;

#define CLJ_TYPE_COUNT (CLJ_UNKNOWN + 1)

const char* clj_type_name(CljType type);

#endif
