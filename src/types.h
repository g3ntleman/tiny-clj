#ifndef TINY_CLJ_TYPES_H
#define TINY_CLJ_TYPES_H

typedef enum {
    // Singletons (0-2) - no reference counting needed, can use simple range check
    CLJ_NIL,
    CLJ_BOOL,
    CLJ_SYMBOL,  // Interned symbols - no reference counting needed
    // Other primitive types (3-4) - have reference counting
    CLJ_INT,
    CLJ_FLOAT,
    // Complex types (5+) - require individual checks, have reference counting
    CLJ_STRING,
    CLJ_VECTOR,
    CLJ_WEAK_VECTOR,
    CLJ_MAP,
    CLJ_LIST,
    CLJ_SEQ,         // Sequence iterator (embedded CljSeqIterator)
    CLJ_FUNC,
    CLJ_EXCEPTION,
    // Transient types (Clojure-kompatibel: nur Vector und Map)
    CLJ_TRANSIENT_VECTOR,
    CLJ_TRANSIENT_MAP,
    CLJ_UNKNOWN  // Unknown/invalid type sentinel (should not occur at runtime)
} CljType;

#define CLJ_TYPE_COUNT (CLJ_UNKNOWN + 1)

const char* clj_type_name(CljType type);

#endif
