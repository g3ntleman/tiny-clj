#ifndef FUNCTION_CALL_H
#define FUNCTION_CALL_H

#include "object.h"
#include "map.h" // Must be included before namespace.h (map.h -> value.h -> symbol.h)
#include "namespace.h"
#include "list.h"
#include "vector.h"
#include "environment.h" // For CallFrame

// Evaluation context used during function, let, recur, etc.
// All fields are direct pointers to already-managed objects (see MEMORY_POLICY.md).
// EvalContext instances themselves live on the stack and require no retain/release.
typedef struct {
  // Environment (direct pointers, no nested structs)
  CljPersistentMap *env;                // Current environment map (can be NULL)
  CljPersistentVector *env_stack;       // Environment stack for closures (can be NULL)
  CallFrame *frame;                     // Stack-based call frame for parameters (can be NULL)
  CljPersistentVector *captured_frames; // Closure-captured CallFrame chain for SlotRef depth>0 (can be NULL)

  // Evaluation state
  EvalState *st; // Evaluation state (can be NULL)
  ID current_fn; // Currently executing closure for self-recursion resolution (can be NULL)

  // Recur state
  ID *recur_args;        // Recur arguments (can be NULL)
  int *recur_arg_count;  // Pointer to recur argument count (can be NULL)
  int recur_param_count; // Number of parameters for recur (0 = infer from provided)
} EvalContext;

// Special Form evaluation function pointer type (defined here where types are known)
typedef ID (*SpecialFormEvalFn)(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);

// Extended function-call entry points
ID eval_function_call(ID fn, ID *args, unsigned int argc, CljPersistentMap *env, EvalState *st);
// MEMORY_POLICY: returns a caller-usable result (heap objects are pool-managed).
ID eval_body(ID body, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
// Internal function - uses EvalContext for parameter substitution
// MEMORY_POLICY: returns a caller-usable result (heap objects are pool-managed).
ID eval_body_with_params(ID body, const EvalContext *ctx);

// Special form evaluators
ID eval_def(CljPersistentVector *args, CljPersistentMap *env, EvalState *st);
ID eval_ns(CljPersistentVector *args, CljPersistentMap *env, EvalState *st);
ID eval_var(CljPersistentVector *args, CljPersistentMap *env, EvalState *st);
ID eval_list_function(CljList *list, CljPersistentMap *env);
ID eval_fn(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_symbol(CljSymbol *symbol, EvalState *st);
ID eval_time(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_heap(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);

// Additional built-in helpers

// For-loop functions
ID eval_doseq(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_dotimes(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);

// Let bindings
ID eval_let(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);

// Helper functions
ID eval_arg(CljList *list, int index, CljPersistentMap *env, EvalState *st);
ID eval_arg_with_context(CljList *list, int index, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_arg_from_expr_with_context(ID expr, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);

// Time output suppression (for tests)
void set_suppress_time_output(bool suppress);

// Reset eval arg depth (for test isolation)
void reset_eval_arg_depth(void);

// Control pretreated AST execution (primarily for tests/benchmarks).
// - enabled = 0: force disabled
// - enabled = 1: force enabled
// - enabled = -1: reset to "read from env on next use"
void eval_set_use_compiled_ast(int enabled);

// Convenience functions for string evaluation.
// All return autoreleased refs (MEMORY_POLICY); callers must NOT RELEASE the result.
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
 * @return The evaluated result (autoreleased). NULL is also a valid successful
 * result for Clojure nil; errors are reported via exceptions.
 */
ID eval_string(const char *expr_str, EvalState *eval_state);

// Common evaluation helpers
ID *alloc_obj_array(int size, ID *stack_buffer);
void free_obj_array(ID *array, ID *stack_buffer);

#endif
