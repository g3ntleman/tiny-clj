#ifndef TINY_CLJ_FUNCTION_H
#define TINY_CLJ_FUNCTION_H

#include "object.h"
#include <subjective-c/map.h>
#include "vector.h"
#include "list.h"
#include <stdint.h>

struct CljNamespace;
struct CljSymbol;

typedef struct {
    CljObject base;
    ID (*fn)(ID*args, unsigned int argc);
    struct CljSymbol *name_sym;
} CljCFunc;

typedef struct {
    CljObject base;
    CljVector *params;  // Parameter vector (can be NULL if no parameters)
    ID *params_array;   // Cached raw params array (borrowed from params)
    uint8_t param_count; // Cached vector_count(params)
    uint8_t has_recur;   // 1 if function body contains `recur` (enables TCO loop)
    ID body;  // Function body (AST to evaluate)
    CljList *env_stack;  // Environment stack (list of maps) - idiomatic Clojure-style
    const char *name;
    struct CljNamespace *ns;
    int8_t variadic_index;  // -1 = not variadic, >= 0 = index of & in params
} CljFunction;

CljFunction* make_function(ID *params, int param_count, ID body, CljList *env_stack, const char *cname, struct CljNamespace *ns);


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
