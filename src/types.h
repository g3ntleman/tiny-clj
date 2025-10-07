#ifndef TINY_CLJ_TYPES_H
#define TINY_CLJ_TYPES_H

typedef enum {
    // Primitive types (0-4) - can use simple range check, no reference counting
    CLJ_NIL,
    CLJ_INT,
    CLJ_FLOAT,
    CLJ_BOOL,
    CLJ_SYMBOL,  // Interned symbols - no reference counting needed
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
