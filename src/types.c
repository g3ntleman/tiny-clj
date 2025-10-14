#include "types.h"

const char* clj_type_name(CljType type) {
    // Add bounds checking to prevent invalid type access
    if (type < 0 || type >= CLJ_TYPE_COUNT) {
        return "Invalid";
    }
    
    switch (type) {
        case CLJ_NIL: return "Nil";
        case CLJ_INT: return "Integer";
        case CLJ_FLOAT: return "Float";
        case CLJ_STRING: return "String";
        case CLJ_SYMBOL: return "Symbol";
        case CLJ_VECTOR: return "Vector";
        case CLJ_WEAK_VECTOR: return "WeakVector";
        case CLJ_MAP: return "Map";
        case CLJ_LIST: return "List";
        case CLJ_FUNC: return "Function";
        case CLJ_BOOL: return "Boolean";
        case CLJ_EXCEPTION: return "Exception";
        case CLJ_SEQ: return "Sequence";
        case CLJ_UNKNOWN: return "Unknown";
        default: return "Unknown";
    }
}


