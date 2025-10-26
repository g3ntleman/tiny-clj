#ifndef TINY_CLJ_BUILTINS_H
#define TINY_CLJ_BUILTINS_H

#include "object.h"
#include "runtime.h"

typedef ID (*CljNativeFn)(ID *args, int argc);

typedef enum { FN_GENERIC, FN_ARITY1, FN_ARITY2, FN_ARITY3 } FnKind;

typedef struct BuiltinEntry {
    const char *name;
    FnKind kind;
    union {
        CljNativeFn generic;
        ID (*fn1)(ID);
        ID (*fn2)(ID, ID);
        ID (*fn3)(ID, ID, ID);
    } u;
} BuiltinEntry;

// Example implementations
ID nth2(ID *args, int argc);
ID conj2(ID vec, ID val);
ID assoc3(ID *args, int argc);
ID native_if(ID *args, int argc);
ID native_type(ID *args, int argc);
ID native_array_map(ID *args, int argc);

// Function value constructors
ID make_named_func(BuiltinFn fn, void *env, const char *name);

// Eval helpers
ID apply_builtin(const BuiltinEntry *entry, ID *args, int argc);

// Builtins registration
void register_builtins();

// Variadic functions (Phase 1)
ID native_str(ID *args, int argc);
// Arithmetic wrapper functions removed - using *_variadic directly
ID native_add_variadic(ID *args, int argc);
ID native_sub_variadic(ID *args, int argc);
ID native_mul_variadic(ID *args, int argc);
ID native_div_variadic(ID *args, int argc);

// Transient functions
ID native_transient(ID *args, int argc);
ID native_persistent(ID *args, int argc);
ID native_conj_bang(ID *args, int argc);

// Comparison operators
ID native_lt(ID *args, int argc);
ID native_gt(ID *args, int argc);
ID native_le(ID *args, int argc);
ID native_ge(ID *args, int argc);
ID native_eq(ID *args, int argc);

#endif
