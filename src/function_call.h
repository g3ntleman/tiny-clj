#ifndef FUNCTION_CALL_H
#define FUNCTION_CALL_H

#include "object.h"
#include "namespace.h"
#include "map.h"
#include "list.h"

// Evaluation context structures for parameter substitution
// Parameter substitution context
typedef struct {
    ID *params;      // Parameter names
    ID *values;      // Parameter values
    int param_count; // Number of parameters
} ParamContext;

// Evaluation environment
typedef struct {
    CljMap *closure_env;  // Closure environment map (can be NULL)
    EvalState *st;        // Evaluation state
} EvalEnv;

// Recur state (optional - only needed for recur)
typedef struct {
    ID *recur_args;      // Recur arguments (pointer to local array in caller)
    int *recur_arg_count; // Recur argument count (pointer to local variable in caller)
} RecurContext;

// Combined evaluation context
typedef struct {
    ParamContext *params;  // Parameter substitution (can be NULL if no params)
    EvalEnv *env;          // Evaluation environment (required)
    RecurContext *recur;   // Recur state (can be NULL if not in recur context)
} EvalContext;

// Helper functions for context initialization (DRY)
static inline EvalEnv eval_env_create(CljMap *closure_env, EvalState *st) {
    return (EvalEnv){.closure_env = closure_env, .st = st};
}

static inline ParamContext param_context_create(ID *params, ID *values, int param_count) {
    return (ParamContext){.params = params, .values = values, .param_count = param_count};
}

static inline RecurContext recur_context_create(ID *recur_args, int *recur_arg_count) {
    return (RecurContext){.recur_args = recur_args, .recur_arg_count = recur_arg_count};
}

static inline EvalContext eval_context_create(ParamContext *params, EvalEnv *env, RecurContext *recur) {
    return (EvalContext){.params = params, .env = env, .recur = recur};
}

// Convenience function for common case: context with params and env, no recur
static inline EvalContext eval_context_create_simple(ParamContext *params, EvalEnv *env) {
    return eval_context_create(params, env, NULL);
}

// Convenience function for context with params, env, and recur
static inline EvalContext eval_context_create_with_recur(ParamContext *params, EvalEnv *env, RecurContext *recur) {
    return eval_context_create(params, env, recur);
}

// Erweiterte Funktionsaufruf-Funktionen
ID eval_function_call(ID fn, ID *args, int argc, CljMap *env, EvalState *st);
ID eval_body(ID body, CljMap *env, EvalState *st, const EvalContext *ctx);
// Internal function - uses EvalContext for parameter substitution
ID eval_body_with_params(ID body, const EvalContext *ctx);
// List evaluation with context (supports recur via RecurContext)
ID eval_list_with_context(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
// Simplified list evaluation (optionally accepts EvalContext for recur support)
ID eval_list(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);

// Definition und Funktionen
ID eval_def(CljList *list, CljMap *env, EvalState *st);
ID eval_ns(CljList *list, CljMap *env, EvalState *st);
ID eval_var(CljList *list, CljMap *env, EvalState *st);
ID eval_list_function(CljList *list, CljMap *env);
ID eval_fn(CljList *list, CljMap *env, EvalState *st);
ID eval_symbol(ID symbol, EvalState *st);

// Weitere Built-in Funktionen
ID eval_seq(CljList *list, CljMap *env);

// For-loop functions
ID eval_for(CljList *list, CljMap *env);
ID eval_doseq(CljList *list, CljMap *env);
ID eval_dotimes(CljList *list, CljMap *env);

// Let bindings
ID eval_let(CljList *list, CljMap *env, EvalState *st);

// Function definition macro
ID eval_defn(CljList *list, CljMap *env, EvalState *st);

// Hilfsfunktionen
ID eval_arg(CljList *list, int index, CljMap *env, EvalState *st);
bool is_symbol(ID v, const char *name);

// Time output suppression (for tests)
void set_suppress_time_output(bool suppress);

#endif
