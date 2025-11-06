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

CljObject* make_function(CljObject **params, int param_count, CljObject *body, CljObject *closure_env, const char *name) {
    if (param_count < 0 || param_count > MAX_FUNCTION_PARAMS) return NULL;
    
    CljFunction *func = (CljFunction*)alloc(sizeof(CljFunction), 1, CLJ_CLOSURE);
    if (!func) throw_oom(CLJ_CLOSURE);
    
    func->base.type = CLJ_CLOSURE;  // Interpreted functions use CLJ_CLOSURE type
    func->base.rc = 1;
    func->param_count = param_count;
    func->body = RETAIN(body);
    func->closure_env = RETAIN(closure_env);
    func->name = name ? strdup(name) : NULL;
    
    // Parameter-Array kopieren
    if (param_count > 0 && params) {
        func->params = (CljObject**)malloc(sizeof(CljObject*) * param_count);
        if (!func->params) {
            DEALLOC(func);
            throw_oom(CLJ_CLOSURE);
        }
        for (int i = 0; i < param_count; i++) {
            func->params[i] = RETAIN(params[i]);
        }
    } else {
        func->params = NULL;
    }
    
    return (CljObject*)func;
}

