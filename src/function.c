/*
 * Function Creation and Management
 *
 * Functions for creating Clojure function objects (CljFunction).
 */

#include <stdlib.h>
#include "function.h"
#include "value.h"  // For IS_IMMEDIATE macro
#include "memory.h"
#include "runtime.h"
#include "object.h"
#include "symbol.h"  // For SYM_AMP

/** Create interpreted function closure; rc=1, caller releases. */
CljFunction* make_function(ID *params, int param_count, ID body, CljPersistentVector *env_stack, CljSymbol *name_sym, struct CljNamespace *ns) {
    if (param_count < 0 || param_count > MAX_FUNCTION_PARAMS) return NULL;
    if (param_count > 0 && !params) return NULL;

    // Find variadic index (position of & in params), -1 if not variadic
    int8_t variadic_index = -1;
    for (int i = 0; i < param_count; i++) {
        if (params[i] == SYM_AMP) {
            variadic_index = (int8_t)i;
            break;
        }
    }

    size_t size = sizeof(CljFunction) + ((size_t)param_count * sizeof(ID));
    CljFunction *func = (CljFunction*)alloc(size, 1, CLJ_CLOSURE);

    func->base.type = CLJ_CLOSURE;  // Interpreted functions use CLJ_CLOSURE type
    func->param_count = (uint8_t)param_count;
    func->variadic_index = variadic_index;
    func->body = RETAIN(body);
    // Persistent env_stack is always heap-managed (vector of maps).
    // It may be shared across closures; RETAIN is required for correctness.
    func->env_stack = env_stack ? (CljPersistentVector*)RETAIN(env_stack) : NULL;
    // Name is stored as an interned symbol (singleton), so we can safely borrow it.
    func->name_sym = name_sym;
    func->ns = ns ? (struct CljNamespace*)RETAIN(ns) : NULL;
    for (int i = 0; i < param_count; i++) {
        func->params[i] = RETAIN(params[i]);
    }

    return func;
}

// -----------------------------------------------------------------------------
// Native function constructor (CljCFunc)
// -----------------------------------------------------------------------------
/** Wrap C builtin as callable function; rc=1, caller releases. */
ID make_named_func(BuiltinFn fn, CljSymbol *name_sym)
{
    CljCFunc *func = ALLOC(CljCFunc, 1);

    func->base.type = CLJ_FUNC;
    func->fn = fn;

    // Name is stored as an interned symbol (singleton), so we can safely borrow it.
    func->name_sym = name_sym;

    return (ID)func;
}
