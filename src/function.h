#ifndef TINY_CLJ_FUNCTION_H
#define TINY_CLJ_FUNCTION_H

#include "object.h"

typedef struct {
    CljObject base;
    CljObject* (*fn)(CljObject **args, int argc);
    void *env;
    const char *name;
} CljFunc;

typedef struct {
    CljObject base;
    CljObject **params;
    int param_count;
    CljObject *body;
    CljObject *closure_env;
    const char *name;
} CljFunction;

CljObject* make_function(CljObject **params, int param_count, CljObject *body, CljObject *closure_env, const char *name);

// Function call helpers
/** Call function with argv; returns result or error object. */
CljObject* clj_call_function(CljObject *fn, int argc, CljObject **argv);
/** Apply function to array args in given env; returns result. */
CljObject* clj_apply_function(CljObject *fn, CljObject **args, int argc, CljObject *env);

// Type-safe casting
static inline CljFunction* as_function(ID obj) {
    return (CljFunction*)assert_type((CljObject*)obj, CLJ_CLOSURE);
}

// Helper: check if a function object is native (CljFunc) or interpreted (CljFunction)
static inline int is_native_fn(CljObject *fn) {
    // Native builtins are represented as CljFunc; interpreted functions as CljFunction
    if (TYPE(fn) != CLJ_FUNC) return 0;
    
    // Additional check: native functions have a function pointer
    CljFunc *native_func = (CljFunc*)fn;
    return native_func->fn != NULL;
}

#endif
