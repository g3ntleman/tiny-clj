/*
 * Function Creation and Management
 * 
 * Functions for creating Clojure function objects (CljFunction).
 */

#include <stdlib.h>
#include <string.h>
#include "function.h"
#include "value.h"  // For IS_IMMEDIATE macro
#include "memory.h"
#include "runtime.h"
#include "object.h"
#include "vector.h"

/**
 * @brief Allocate and initialize parameter vector for function
 * @param func Function object to allocate params for
 * @param params Source parameter array
 * @param param_count Number of parameters
 * @return 0 on success, -1 on failure (func is cleaned up on failure)
 */
static int allocate_function_params(CljFunction *func, ID *params, int param_count) {
    if (param_count > 0 && params) {
        // Create vector for parameters
        CljVector *vec = make_vector(param_count, CLJ_VECTOR);
        if (!vec) {
            DEALLOC(func);
            throw_oom();
            return -1;
        }
        
        // Add all parameters to vector (vector_conj retains elements)
        for (int i = 0; i < param_count; i++) {
            CljVector *new_vec = vector_conj(vec, RETAIN(params[i]));
            if (!new_vec) {
                RELEASE(vec);
                DEALLOC(func);
                throw_oom();
                return -1;
            }
            // vector_conj may return a new vector (COW), so update vec
            if (new_vec != vec) {
                RELEASE(vec);
                vec = new_vec;
            }
        }
        
        func->params = vec;  // Vector is already retained (rc=1 from make_vector)
    } else {
        func->params = NULL;
    }
    return 0;
}

CljFunction* make_function(ID *params, int param_count, ID body, CljList *env_stack, const char *cname) {
    if (param_count < 0 || param_count > MAX_FUNCTION_PARAMS) return NULL;
    
    CljFunction *func = (CljFunction*)alloc(sizeof(CljFunction), 1, CLJ_CLOSURE);
    if (!func) throw_oom();
    
    func->base.type = CLJ_CLOSURE;  // Interpreted functions use CLJ_CLOSURE type
    func->base.rc = 1;
    func->body = RETAIN(body);
    func->env_stack = RETAIN(env_stack);
    func->name = cname ? strdup(cname) : NULL;
    
    // Allocate and initialize parameter array
    if (allocate_function_params(func, params, param_count) != 0) {
        return NULL;
    }
    
    return func;
}

