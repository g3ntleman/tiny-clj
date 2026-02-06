#ifndef SUBJECTIVE_C_TYPES_H
#define SUBJECTIVE_C_TYPES_H

#include <stdint.h>

#ifdef DEBUG
#ifndef ZOMBIE_RC
#define ZOMBIE_RC 0  // Zombie objects have rc=0 (already freed but not DEALLOCed)
#endif
#endif

#ifndef SINGLETON_RC
#define SINGLETON_RC -2
#endif

typedef enum {
    CLJ_NIL = 0,
    CLJ_FIXNUM = 1,
    CLJ_SYMBOL = 2,
    CLJ_CHAR = 3,
    CLJ_STRING = 4,
    CLJ_BOOL = 5,
    CLJ_VECTOR_PERSISTENT = 6,
    CLJ_FLOAT = 7,

    CLJ_VECTOR_TRANSIENT = 8,
    CLJ_MAP_PERSISTENT = 9,
    CLJ_MAP_TRANSIENT = 10,
    CLJ_LIST = 11,
    CLJ_AST_NODE = 12,
    CLJ_AST_CALL = 13,
    CLJ_CALLSITE_CACHE = 14,
    CLJ_RESERVED_AST_BODY = 15,
    CLJ_SEQ = 16,
    CLJ_FUNC = 17,
    CLJ_CLOSURE = 18,
    CLJ_EXCEPTION = 19,
    CLJ_BYTE_ARRAY = 20,
    CLJ_ATOM = 21,
    CLJ_NAMESPACE = 22,
    CLJ_RAW_MEMORY = 23,
    CLJ_SYMBOL_TOKEN = 24,
    CLJ_HASHMAP = 25,
    CLJ_REGEX = 26,
    CLJ_LAZY_SEQ = 27,
    CLJ_INSTANT = 28,
    CLJ_UUID = 29,
    // Lexical addressing: (depth, slot) references for locals
    CLJ_SLOT_REF = 30
} CljType;

#define CLJ_TYPE_COUNT (CLJ_SLOT_REF + 1)

#ifndef CLJ_INT
#define CLJ_INT CLJ_FIXNUM
#endif

/** @brief Get human-readable name for type
 * @param type Type to get name for
 * @return Type name string
 */
const char* clj_type_name(CljType type);

#endif // SUBJECTIVE_C_TYPES_H
