/*
 * CljObject Header
 *
 * Core data structure definitions for Tiny-Clj:
 * - CljObject base structure for all Clojure data types
 * - Type checking and casting utilities
 * - Memory management with reference counting
 */

#ifndef SUBJECTIVE_C_OBJECT_H
#define SUBJECTIVE_C_OBJECT_H

#define ID void*
typedef void* CljValue;

#include "types.h"
#include "common.h"
#include <stdio.h>
#include <execinfo.h>

#define INDEX_NOT_FOUND (-1)
#define LAST_SINGLETON_TYPE CLJ_SYMBOL
#define IS_SINGLETON_TYPE(type) ((type) <= LAST_SINGLETON_TYPE)
#define TRACKS_RETAINS(obj) ((obj) && !is_singleton(obj))
#define IS_SINGLETON(obj) is_singleton(obj)

#define TYPE_OF_CljList CLJ_LIST
#define TYPE_OF_CljASTNode CLJ_AST_NODE
#define TYPE_OF_CljCallsiteCache CLJ_CALLSITE_CACHE
#define TYPE_OF_CljSymbol CLJ_SYMBOL
#define TYPE_OF_CljFunction CLJ_CLOSURE
#define TYPE_OF_CljCFunc CLJ_FUNC
#define TYPE_OF_CljVector CLJ_VECTOR
#define TYPE_OF_CljPersistentMap CLJ_MAP
#define TYPE_OF_CljMap CLJ_MAP
#define TYPE_OF_CLJException CLJ_EXCEPTION
#define TYPE_OF_CljSeqIterator CLJ_SEQ
#define TYPE_OF_CljByteArray CLJ_BYTE_ARRAY
#define TYPE_OF_CljAtom CLJ_ATOM
#define TYPE_OF_int CLJ_INT
#define TYPE_OF_double CLJ_FLOAT
#define TYPE_OF_char CLJ_STRING
#define TYPE_OF_CljNamespace CLJ_NAMESPACE
#define TYPE_OF(struct_type) TYPE_OF_##struct_type

typedef struct CljObject {
    uint8_t type;
    uint8_t flags;
    int16_t rc;
} CljObject;

#define CLJ_FLAG_SPECIAL     0x01  // Special Form Symbol
#define CLJ_FLAG_ARITHMETIC  0x02  // Arithmetic Operator (+ - * /)

static inline CljType TAG(ID obj) {
    if ((uintptr_t)obj & 0x1) {
        return (CljType)((uintptr_t)obj & 0x7);
    }
    if (!obj) return CLJ_NIL;
    CljObject *obj_ptr = (CljObject*)obj;
#ifdef DEBUG
    if (obj_ptr && (uintptr_t)obj_ptr >= 0x1000 && obj_ptr->rc == ZOMBIE_RC) {
    }
#endif
    return obj_ptr->type;
}

static inline bool is_singleton(CljObject *obj) {
    if (!obj || (uintptr_t)obj < 0x1000) {
        return true;
    }
#ifdef DEBUG
    if (obj->rc == ZOMBIE_RC) {
        return false;
    }
#endif
    CljType obj_type = obj->type;
    if (IS_SINGLETON_TYPE(obj_type)) {
        return true;
    }
    if (obj->rc == SINGLETON_RC) {
        return true;
    }
    return false;
}

bool clj_equal(ID a, ID b);
static inline bool clj_is_truthy(CljObject *v) {
    if (v) {
        if (((uintptr_t)v & 0x7) == 5) {
            uint8_t special = (uint8_t)((uintptr_t)v >> 3);
            if (special == 0) return false;
        }
        return true;
    }
    return false;
}

int reference_count(CljObject *obj);
static inline void* assert_type(CljObject *obj, CljType expected_type) {
    if (obj && TAG(obj) == expected_type) {
        return obj;
    }
    fprintf(stderr, "assert_type failed: expected %d actual %d\n",
            (int)expected_type, obj ? (int)TAG(obj) : -1);
    void *trace[16];
    int trace_count = backtrace(trace, 16);
    backtrace_symbols_fd(trace, trace_count, fileno(stderr));
    CLJ_ASSERT(0 && "Type assertion failed");
    return NULL;
}

#endif // SUBJECTIVE_C_OBJECT_H
