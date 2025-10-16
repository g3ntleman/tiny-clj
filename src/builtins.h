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
CljObject* native_type(CljObject **args, int argc);
CljObject* native_array_map(CljObject **args, int argc);

// Function value constructors
CljObject* make_func(CljObject* (*fn)(CljObject **args, int argc), void *env);
CljObject* make_named_func(CljObject* (*fn)(CljObject **args, int argc), void *env, const char *name);

// Eval helpers
CljObject* apply_builtin(const BuiltinEntry *entry, CljObject **args, int argc);

// Builtins registration
void register_builtins();

// Variadic functions (Phase 1)
CljObject* native_str(CljObject **args, int argc);
CljObject* native_add(CljObject **args, int argc);
CljObject* native_sub(CljObject **args, int argc);
CljObject* native_mul(CljObject **args, int argc);
CljObject* native_div(CljObject **args, int argc);
CljObject* native_add_variadic(CljObject **args, int argc);
CljObject* native_sub_variadic(CljObject **args, int argc);
CljObject* native_mul_variadic(CljObject **args, int argc);
CljObject* native_div_variadic(CljObject **args, int argc);

#endif
