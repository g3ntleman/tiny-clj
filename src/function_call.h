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
    ID closure_env;    // Closure environment
    EvalState *st;     // Evaluation state
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

// Erweiterte Funktionsaufruf-Funktionen
ID eval_function_call(ID fn, ID *args, int argc, CljMap *env, EvalState *st);
ID eval_body(ID body, CljMap *env, EvalState *st);
// Internal function - uses EvalContext for parameter substitution
ID eval_body_with_params(ID body, const EvalContext *ctx);
ID eval_list(CljList *list, CljMap *env, EvalState *st);

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
ID eval_arg(CljList *list, int index, CljMap *env);
ID eval_arg_retained(CljList *list, int index, CljMap *env);
bool is_symbol(ID v, const char *name);

#endif
