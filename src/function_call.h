#ifndef FUNCTION_CALL_H
#define FUNCTION_CALL_H

#include "object.h"
#include "map.h"  // Must be included before namespace.h (map.h -> value.h -> symbol.h)
#include "namespace.h"
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
    CljList *env_stack;        // List of environment maps (most recent first) - idiomatic Clojure-style environment chain
    EvalState *st;             // Evaluation state
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
ID eval_symbol(CljSymbol *symbol, EvalState *st);

// Weitere Built-in Funktionen
ID eval_seq(CljList *list, CljMap *env);

// For-loop functions
ID eval_for(CljList *list, CljMap *env);
ID eval_doseq(CljList *list, CljMap *env);
ID eval_dotimes(CljList *list, CljMap *env, EvalState *st);

// Let bindings
ID eval_let(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);

// Function definition macro
ID eval_defn(CljList *list, CljMap *env, EvalState *st);

// Hilfsfunktionen
ID eval_arg(CljList *list, int index, CljMap *env, EvalState *st);
ID eval_arg_with_context(CljList *list, int index, CljMap *env, EvalState *st, const EvalContext *ctx);
bool is_symbol(ID v, const char *name);

// Time output suppression (for tests)
void set_suppress_time_output(bool suppress);

// Reset eval arg depth (for test isolation)
void reset_eval_arg_depth(void);

// Convenience function for string evaluation
/**
 * @brief Parse and evaluate a Clojure expression from a string (convenience)
 * @param expr_str The Clojure expression as a string
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL on error
 */
ID eval_string(const char* expr_str, EvalState *eval_state);

#endif
