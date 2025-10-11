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
    CLJ_ARRAY,       // Strong array (retains on push)
    CLJ_WEAK_ARRAY,  // Weak array (no retain on push; single release on clear)
    CLJ_FUNC,
    CLJ_EXCEPTION,
    CLJ_UNKNOWN  // Unknown/invalid type sentinel (should not occur at runtime)
} CljType;

const char* clj_type_name(CljType type);

#endif
