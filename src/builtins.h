#ifndef TINY_CLJ_BUILTINS_H
#define TINY_CLJ_BUILTINS_H

#include "object.h"

typedef CljObject* (*CljNativeFn)(CljObject **args, int argc);

typedef enum { FN_GENERIC, FN_ARITY1, FN_ARITY2, FN_ARITY3 } FnKind;

typedef struct BuiltinEntry {
    const char *name;
    FnKind kind;
    union {
        CljNativeFn generic;
        CljObject* (*fn1)(CljObject*);
        CljObject* (*fn2)(CljObject*, CljObject*);
        CljObject* (*fn3)(CljObject*, CljObject*, CljObject*);
    } u;
} BuiltinEntry;

// Example implementations
CljObject* nth2(CljObject *vec, CljObject *idx);
CljObject* conj2(CljObject *vec, CljObject *val);
CljObject* assoc3(CljObject *vec, CljObject *idx, CljObject *val);
CljObject* native_if(CljObject **args, int argc);

// Function value constructors
CljObject* make_func(CljObject* (*fn)(CljObject **args, int argc), void *env);

// Eval helpers
CljObject* apply_builtin(const BuiltinEntry *entry, CljObject **args, int argc);

// Builtins registration
void register_builtins();

#endif
