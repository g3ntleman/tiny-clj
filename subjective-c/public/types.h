#ifndef SUBJECTIVE_C_TYPES_H
#define SUBJECTIVE_C_TYPES_H

#include <stdint.h>

#ifdef DEBUG
#ifndef ZOMBIE_RC
#define ZOMBIE_RC -1
#endif
#endif

#ifndef SINGLETON_RC
#define SINGLETON_RC -2
#endif

typedef enum {
    CLJ_INT = 1,
    CLJ_CHAR = 3,
    CLJ_BOOL = 5,
    CLJ_FLOAT = 7,

    CLJ_NIL = 0,
    CLJ_SYMBOL = 2,
    CLJ_STRING = 4,
    CLJ_VECTOR = 6,
    CLJ_VECTOR_TRANSIENT_WEAK = 8,
    CLJ_MAP = 10,
    CLJ_LIST = 12,
    CLJ_AST_NODE = 14,
    CLJ_CALLSITE_CACHE = 16,
    CLJ_SEQ = 18,
    CLJ_FUNC = 20,
    CLJ_CLOSURE = 22,
    CLJ_EXCEPTION = 24,
    CLJ_BYTE_ARRAY = 26,
    CLJ_ATOM = 28,
    CLJ_VECTOR_TRANSIENT = 30,
    CLJ_MAP_TRANSIENT = 32,
    CLJ_NAMESPACE = 34,
    CLJ_RAW_MEMORY = 36,
    CLJ_SYMBOL_TOKEN = 38
} CljType;

#define CLJ_TYPE_COUNT (CLJ_SYMBOL_TOKEN + 1)

const char* clj_type_name(CljType type);

#endif // SUBJECTIVE_C_TYPES_H
