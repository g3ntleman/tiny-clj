/*
 * Function Creation and Management
 * 
 * Functions for creating Clojure function objects (CljFunction).
 */

#include <stdlib.h>
#include <string.h>
#include "function.h"
#include "memory.h"
#include "exception.h"
#include "runtime.h"
#include "object.h"
#include "value.h"

/**
 * @brief Allocate and initialize parameter array for function
 * @param func Function object to allocate params for
 * @param params Source parameter array
 * @param param_count Number of parameters
 * @return 0 on success, -1 on failure (func is cleaned up on failure)
 */
static int allocate_function_params(CljFunction *func, ID *params, int param_count) {
    if (param_count > 0 && params) {
        // Use alloc for memory profiling, even though it's not a CljObject
        // Pass CLJ_UNKNOWN as type since it's just raw memory
        func->params = (ID*)alloc(sizeof(ID), param_count, CLJ_UNKNOWN);
        if (!func->params) {
            DEALLOC(func);
            throw_oom(CLJ_CLOSURE);
            return -1;
        }
        for (int i = 0; i < param_count; i++) {
            func->params[i] = RETAIN(params[i]);
        }
    } else {
        func->params = NULL;
    }
    return 0;
}

CljFunction* make_function(ID *params, int param_count, ID body, CljMap *closure_env, const char *name) {
    if (param_count < 0 || param_count > MAX_FUNCTION_PARAMS) return NULL;
    
    CljFunction *func = (CljFunction*)alloc(sizeof(CljFunction), 1, CLJ_CLOSURE);
    if (!func) throw_oom(CLJ_CLOSURE);
    
    func->base.type = CLJ_CLOSURE;  // Interpreted functions use CLJ_CLOSURE type
    func->base.rc = 1;
    func->param_count = param_count;
    func->body = RETAIN(body);
    func->closure_env = RETAIN(closure_env);
    func->name = name ? strdup(name) : NULL;
    
    // Allocate and initialize parameter array
    if (allocate_function_params(func, params, param_count) != 0) {
        return NULL;
    }
    
    return func;
}

