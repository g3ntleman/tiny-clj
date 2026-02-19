#ifndef TINY_CLJ_FUNCTION_H
#define TINY_CLJ_FUNCTION_H

#include "object.h"
#include "runtime.h" // For BuiltinFn
#include "map.h"
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
    uint8_t param_count;      // Number of entries in params[] (includes '&' marker for variadics)
    int8_t variadic_index;    // -1 = not variadic, >= 0 = index of & in params
    ID body;                  // Function body (AST to evaluate)
    CljPersistentVector *env_stack;  // Environment stack (vector of maps), COW-optimized
    struct CljSymbol *name_sym;
    struct CljNamespace *ns;
    ID params[];              // Inline parameter array (flexible array member)
} CljFunction;

CljFunction* make_function(ID *params, int param_count, ID body, CljPersistentVector *env_stack, struct CljSymbol *name_sym, struct CljNamespace *ns);

// Native function constructor (CljCFunc)
ID make_named_func(BuiltinFn fn, struct CljSymbol *name_sym);


// Type predicates
static inline bool is_closure(ID obj) {
    return obj && !IS_IMMEDIATE(obj) && TAG(obj) == CLJ_CLOSURE;
}

static inline bool is_func(ID obj) {
    return obj && !IS_IMMEDIATE(obj) && TAG(obj) == CLJ_FUNC;
}

// True for both native (CLJ_FUNC) and interpreted (CLJ_CLOSURE) functions
static inline bool is_callable(ID obj) {
    if (!obj || IS_IMMEDIATE(obj)) return false;
    CljType tag = TAG(obj);
    return tag == CLJ_FUNC || tag == CLJ_CLOSURE;
}

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
