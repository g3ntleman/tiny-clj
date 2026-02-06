#include "types.h"

const char* clj_type_name(CljType type) {
    // Add bounds checking to prevent invalid type access
    if (type < 0 || type >= CLJ_TYPE_COUNT) {
        return "Invalid";
    }

    switch (type) {
        case CLJ_FIXNUM: return "Integer";
        case CLJ_CHAR: return "Character";
        case CLJ_BOOL: return "Boolean";
        case CLJ_FLOAT: return "Float";
        case CLJ_STRING: return "String";
        case CLJ_SYMBOL: return "Symbol";
        case CLJ_VECTOR_PERSISTENT: return "Vector";
        case CLJ_MAP_PERSISTENT: return "Map";
        case CLJ_LIST: return "List";
        case CLJ_AST_NODE: return "ASTNode";
        case CLJ_AST_CALL: return "ASTCall";
        case CLJ_CALLSITE_CACHE: return "CallsiteCache";
        case CLJ_FUNC: return "Function";
        case CLJ_CLOSURE: return "Closure";
        case CLJ_EXCEPTION: return "Exception";
        case CLJ_BYTE_ARRAY: return "ByteArray";
        case CLJ_ATOM: return "Atom";
        case CLJ_SEQ: return "Sequence";
        case CLJ_VECTOR_TRANSIENT: return "TransientVector";
        case CLJ_MAP_TRANSIENT: return "TransientMap";
        case CLJ_NAMESPACE: return "Namespace";
        case CLJ_RAW_MEMORY: return "RawMemory";
        case CLJ_SYMBOL_TOKEN: return "SymbolToken";
        case CLJ_HASHMAP: return "HashMap";
        case CLJ_REGEX: return "Regex";
        case CLJ_LAZY_SEQ: return "LazySeq";
        case CLJ_INSTANT: return "Instant";
        case CLJ_UUID: return "UUID";
        case CLJ_SLOT_REF: return "SlotRef";
        case CLJ_NIL: return "Nil";
        default: return "Unknown";
    }
}
