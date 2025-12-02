#ifndef FUNCTION_CALL_H
#define FUNCTION_CALL_H

#include "object.h"
#include "map.h"  // Must be included before namespace.h (map.h -> value.h -> symbol.h)
#include "namespace.h"
#include "list.h"

// Evaluation context used during function, let, recur, etc.
// All fields are direct pointers to already-managed objects (see MEMORY_POLICY.md).
// EvalContext instances themselves live on the stack and require no retain/release.
typedef struct {
    // Environment (direct pointers, no nested structs)
    CljMap *env;           // Current environment map (can be NULL)
    CljList *env_stack;    // Environment stack for closures (can be NULL)

    // Evaluation state
    EvalState *st;         // Evaluation state (can be NULL)

    // Parameter substitution
    ID *params;            // Parameter names (can be NULL)
    ID *param_values;      // Parameter values (can be NULL)
    int param_count;       // Number of parameters (0 if none)

    // Recur state
    ID *recur_args;        // Recur arguments (can be NULL)
    int *recur_arg_count;  // Pointer to recur argument count (can be NULL)
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
