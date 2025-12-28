#ifndef FUNCTION_CALL_H
#define FUNCTION_CALL_H

#include "object.h"
#include "map.h"  // Must be included before namespace.h (map.h -> value.h -> symbol.h)
#include "namespace.h"
#include "list.h"
#include "environment.h"  // For CallFrame

// Evaluation context used during function, let, recur, etc.
// All fields are direct pointers to already-managed objects (see MEMORY_POLICY.md).
// EvalContext instances themselves live on the stack and require no retain/release.
typedef struct {
    // Environment (direct pointers, no nested structs)
    CljMap *env;           // Current environment map (can be NULL)
    CljList *env_stack;    // Environment stack for closures (can be NULL)
    CallFrame *frame;      // Stack-based call frame for parameters (can be NULL)

    // Evaluation state
    EvalState *st;         // Evaluation state (can be NULL)

    // Recur state
    ID *recur_args;        // Recur arguments (can be NULL)
    int *recur_arg_count;  // Pointer to recur argument count (can be NULL)
    int recur_param_count; // Number of parameters for recur (0 = infer from provided)
} EvalContext;

// Special Form evaluation function pointer type (defined here where types are known)
typedef ID (*SpecialFormEvalFn)(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);

// Extended function-call entry points
ID eval_function_call(ID fn, ID *args, unsigned int argc, CljMap *env, EvalState *st);
ID eval_body(ID body, CljMap *env, EvalState *st, const EvalContext *ctx);
// Internal function - uses EvalContext for parameter substitution
ID eval_body_with_params(ID body, const EvalContext *ctx);
// List evaluation (optionally accepts EvalContext for recur support)
ID eval_list(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);

// Special form evaluators
ID eval_def(CljList *list, CljMap *env, EvalState *st);
ID eval_ns(CljList *list, CljMap *env, EvalState *st);
ID eval_var(CljList *list, CljMap *env, EvalState *st);
ID eval_list_function(CljList *list, CljMap *env);
ID eval_fn(CljList *list, CljMap *env, EvalState *st);
ID eval_fn_with_context(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_symbol(CljSymbol *symbol, EvalState *st);
ID eval_time(CljList *list, CljMap *env, EvalState *st);

// Additional built-in helpers

// For-loop functions
ID eval_for(CljList *list, CljMap *env);
ID eval_doseq(CljList *list, CljMap *env);
ID eval_dotimes(CljList *list, CljMap *env, EvalState *st);

// Let bindings
ID eval_let(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);

// Helper functions
ID eval_arg(CljList *list, int index, CljMap *env, EvalState *st);
ID eval_arg_with_context(CljList *list, int index, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_arg_from_expr_with_context(ID expr, CljMap *env, EvalState *st, const EvalContext *ctx);

// Time output suppression (for tests)
void set_suppress_time_output(bool suppress);

// Reset eval arg depth (for test isolation)
void reset_eval_arg_depth(void);

// Convenience functions for string evaluation
/**
 * @brief Evaluate a parsed CljValue (handles immediate values and heap objects)
 * @param parsed The parsed CljValue (can be immediate or heap object)
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL only if result is nil
 */
ID eval_parsed_value(CljValue parsed, EvalState *eval_state);

/**
 * @brief Parse and evaluate a Clojure expression from a string (convenience)
 * @param expr_str The Clojure expression as a string
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL on error
 */
ID eval_string(const char* expr_str, EvalState *eval_state);

// Common evaluation helpers
ID* alloc_obj_array(int size, ID *stack_buffer);
void free_obj_array(ID *array, ID *stack_buffer);
CljObject* list_get_element(CljList *list, int index);

#endif
