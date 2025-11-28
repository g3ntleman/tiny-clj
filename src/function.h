#ifndef TINY_CLJ_FUNCTION_H
#define TINY_CLJ_FUNCTION_H

#include "object.h"
#include "map.h"
#include "vector.h"
#include "list.h"

typedef struct {
    CljObject base;
    CljObject* (*fn)(CljObject **args, int argc);
    void *env;
    const char *name;
} CljCFunc;

typedef struct {
    CljObject base;
    CljVector *params;  // Parameter vector (can be NULL if no parameters)
    ID body;  // Function body (AST to evaluate)
    CljList *env_stack;  // Environment stack (list of maps) - idiomatic Clojure-style
    const char *name;
} CljFunction;

CljFunction* make_function(ID *params, int param_count, ID body, CljList *env_stack, const char *name);

// Function call helpers
/** Call function with argv; returns result or error object. */
ID clj_call_function(ID fn, int argc, ID *argv);
/** Apply function to array args in given env; returns result. */
ID clj_apply_function(ID fn, ID *args, int argc, ID env);

// Type-safe casting
static inline CljFunction* as_function(ID obj) {
    return (CljFunction*)assert_type((CljObject*)obj, CLJ_CLOSURE);
}

// Helper: check if a function object is native (CljCFunc) or interpreted (CljFunction)
static inline int is_native_fn(CljObject *fn) {
    // Native builtins are represented as CljCFunc; interpreted functions as CljFunction
    if (TAG(fn) != CLJ_FUNC) return 0;
    
    // Additional check: native functions have a function pointer
    CljCFunc *native_func = (CljCFunc*)fn;
    return native_func->fn != NULL;
}

#endif
