#include "list.h"
#include "object.h"
#include "eval.h"
#include "symbol.h"
#include <stdio.h>
#include "exception.h"
#include "function.h"
#include "validation.h"
#include "builtins.h"
#include "optimize.h"
#include "parser.h" // For eval_parsed
#include "reader.h" // For Reader API (used by eval_string)
#include "common.h"

// Branch prediction hints for hot paths
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#include "error_messages.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "seq.h"
#include "namespace.h"
#include "memory.h"
#include "memory_profiler.h"
#include "meta.h"
#include "value.h"
#include "environment.h"
#include "ast.h"
#include "ast_canon.h"
#include "vector.h"
#include "env_stack.h"
#include "strings.h" // For pr_str
#include "eval_arithmetic.h"
#include "debug.h" // For print_ast
#include "eval_comparison.h"
#include <time.h>

#include "eval_sequence.h"
#include "eval_special_forms.h"
#include "macro.h" // For lookup_macro_resolve

#include <signal.h>
extern __attribute__((weak)) volatile sig_atomic_t g_clojure_core_last_form;

// -----------------------------------------------------------------------------
// Compiled AST toggle (used by tests)
// -----------------------------------------------------------------------------
// The public API lives in eval.h. The current implementation is a minimal toggle
// to satisfy tests and allow future integration of compiled/preattached AST eval.
static int g_eval_use_compiled_ast = 0;

void eval_set_use_compiled_ast(int enabled) {
  g_eval_use_compiled_ast = enabled ? 1 : 0;
}

static ID eval_noncanonical_list_form(ID list_form);

static void rewrite_recursive_calls_in_slot(ID *slot, CljSymbol *unqualified, CljSymbol *qualified) {
  if (!slot || !unqualified || !qualified) {
    return;
  }
  ID expr = *slot;
  if (!expr || IS_IMMEDIATE(expr)) {
    return;
  }

  unsigned char tag = TAG(expr);
  if (tag == CLJ_SYMBOL) {
    if ((CljSymbol *)expr == unqualified) {
      *slot = qualified;
    }
    return;
  }

  if (tag == CLJ_AST_NODE) {
    CljASTNode *node = as_ast_node(expr);
    if (node) {
      rewrite_recursive_calls_in_slot((ID *)&node->first, unqualified, qualified);
      rewrite_recursive_calls_in_slot((ID *)&node->rest, unqualified, qualified);
    }
    return;
  }
  if (tag == CLJ_AST_CALL) {
    CljASTCall *call = as_ast_call(expr);
    if (call) {
      rewrite_recursive_calls_in_slot((ID *)&call->op, unqualified, qualified);
      if (call->args) {
        ID *data = vector_as_array(call->args);
        unsigned int count = vector_count(call->args);
        if (data) {
          for (unsigned int i = 0; i < count; i++) {
            rewrite_recursive_calls_in_slot(&data[i], unqualified, qualified);
          }
        }
      }
    }
    return;
  }
  if (tag == CLJ_LIST) {
    CljList *list = as_list(expr);
    if (list) {
      rewrite_recursive_calls_in_slot((ID *)&list->first, unqualified, qualified);
      rewrite_recursive_calls_in_slot((ID *)&list->rest, unqualified, qualified);
    }
    return;
  }

  if (tag == CLJ_VECTOR_PERSISTENT) {
    CljPersistentVector *vec = as_persistent_vector(expr);
    if (vec) {
      unsigned int count = vector_count(vec);
      ID *data = vector_as_array(vec);
      if (data) {
        for (unsigned int i = 0; i < count; ++i) {
          rewrite_recursive_calls_in_slot(&data[i], unqualified, qualified);
        }
      }
    }
    return;
  }
}

static inline bool symbol_name_matches(CljSymbol *a, CljSymbol *b) {
  if (!a || !b)
    return false;
  if (a == b)
    return true;
  if (!a->cname || !b->cname)
    return false;
  return strcmp(a->cname, b->cname) == 0;
}

static inline ID resolve_current_closure_self_symbol(const EvalContext *ctx, ID sym) {
  if (!ctx || !ctx->current_fn || !sym || IS_IMMEDIATE(sym) || TAG(sym) != CLJ_SYMBOL) {
    return NOT_FOUND;
  }
  if (!is_closure(ctx->current_fn)) {
    return NOT_FOUND;
  }
  CljFunction *cur_fn = as_function(ctx->current_fn);
  if (!cur_fn || !cur_fn->name_sym) {
    return NOT_FOUND;
  }
  if (symbol_name_matches(cur_fn->name_sym, as_symbol(sym))) {
    return ctx->current_fn;
  }
  return NOT_FOUND;
}

// Named fn literals used via (def name (fn name ...)) don't need an extra
// self-binding frame: recursion can resolve through the namespace mapping.
static void drop_def_self_binding_frame(CljFunction *func, CljSymbol *def_sym, ID fn_value) {
  if (!func || !func->env_stack || !def_sym || !func->name_sym)
    return;
  if (!symbol_name_matches(func->name_sym, def_sym))
    return;

  unsigned int frame_count = vector_count(func->env_stack);
  if (frame_count == 0)
    return;

  ID frame_obj = vector_nth(func->env_stack, frame_count - 1);
  CljPersistentMap *frame_map = as_map(frame_obj);
  if (!frame_map || map_count((ID)frame_map) != 1)
    return;

  ID bound = map_get_sentinel((ID)frame_map, (ID)def_sym, NOT_FOUND);
  if (bound != fn_value)
    return;

  // Transfer the ownership previously held by the self-binding map
  // so eval_fn's AUTORELEASE release remains balanced.
  RETAIN(fn_value);
  vector_pop_inplace(&func->env_stack);
}

// Evaluation context structures are defined in function_call.h

#include "map.h"
#include "record.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

// Global variable to suppress time output in tests
static bool g_suppress_time_output = false;

void set_suppress_time_output(bool suppress) {
  g_suppress_time_output = suppress;
}

// Forward declarations
ID eval_body_with_params(ID body, const EvalContext *ctx);
ID eval_time(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
ID eval_arg_from_expr_with_context(ID expr, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
static ID eval_ast_call(CljASTCall *call, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
static ID call_function_with_args_and_context_vec(ID fn, CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx);
static ID eval_function_call_from_vector(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, ID op,
                                         ID call_form, const EvalContext *ctx);
// is_special_symbol is now in symbol.c
static INLINE bool is_builtin_function(CljSymbol *symbol);

// Forward declarations for loop evaluation
ID eval_body_with_env(ID body, CljPersistentMap *env, EvalState *st);

// Helper function to throw unresolved symbol exception (DRY principle)
static INLINE bool should_suggest_require_for_ns(const char *ns_name) {
  // Keep the hint focused on real namespaces, not aliases like "str".
  // Also never suggest requiring clojure.core.
  if (!ns_name || !ns_name[0])
    return false;
  if (strcmp(ns_name, "clojure.core") == 0)
    return false;
  return strchr(ns_name, '.') != NULL;
}

static void throw_unresolved_symbol_exception_parts(const char *ns_name,
                                                    const char *sym_name,
                                                    bool suggest_require) {
  const char *name = (sym_name && sym_name[0]) ? sym_name : "unknown";
  if (ns_name && ns_name[0]) {
    if (suggest_require && should_suggest_require_for_ns(ns_name)) {
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "Unable to resolve symbol: %s/%s in this context. (require '%s) missing?",
                                ns_name, name, ns_name);
      return;
    }
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "Unable to resolve symbol: %s/%s in this context", ns_name, name);
    return;
  }
  throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                            "Unable to resolve symbol: %s in this context", name);
}

static void throw_unresolved_symbol_exception_symbol(const CljSymbol *sym) {
  const char *name = (sym && sym->cname) ? sym->cname : "unknown";
  const char *ns = (sym && sym->ns_name && sym->ns_name->cname) ? sym->ns_name->cname : NULL;
  bool suggest = false;
  if (ns && should_suggest_require_for_ns(ns)) {
    // Only suggest require if the namespace is not loaded.
    suggest = (ns_find(ns) == NULL);
  }
  throw_unresolved_symbol_exception_parts(ns, name, suggest);
}

// Extended function call implementation with complete evaluation
/** @brief Main function call evaluator */
ID eval_function_call(ID fn, ID *args, unsigned int argc, CljPersistentMap *env, EvalState *st) {
  // for Clojure functions. For native functions, env is not used.
  (void)env; // Suppress unused parameter warning

  CLJ_ASSERT(is_callable(fn));

  // Check if it's a native function (CljCFunc) or Clojure function (CljFunction)
  if (is_native_fn(fn)) {
    // It's a native C function (CljCFunc)
    CljCFunc *native_func = (CljCFunc *)fn;
    CLJ_ASSERT(native_func && native_func->fn);
    ID result;
    if (UNLIKELY((native_func->base.flags & CLJ_CFUNC_FLAG_NEEDS_EVAL_STATE) != 0u)) {
      extern void builtin_set_eval_state(EvalState * st);
      builtin_set_eval_state(st);
      result = native_func->fn(args, argc);
      builtin_set_eval_state(NULL); // Clear after call
    } else {
      result = native_func->fn(args, argc);
    }
    /* Args are from eval (autoreleased); do not release them here or result may be
     * a structural tail of an arg (e.g. rest(list)) and would be double-freed. */
    return result;
  }

  // It's a Clojure function (CljFunction)
  CljFunction *func = (CljFunction *)fn;
  if (!func) {
    return make_exception(EXCEPTION_RUNTIME, "Invalid function object", NULL, 0, 0);
  }
  CljNamespace *saved_ns = st ? st->current_ns : NULL;
  bool switched_ns = false;
  if (st && func->ns && st->current_ns != func->ns) {
    st->current_ns = func->ns;
    switched_ns = true;
  }

  // Arity check - variadic functions accept >= required params
  int param_count = (int)func->param_count;
  int8_t vi = func->variadic_index;
  unsigned int required = (param_count > 0) ? (unsigned int)param_count : 0;
  if (vi < 0) {
    if (argc != required) {
      throw_exception(EXCEPTION_ARITY, "Arity mismatch in function call", NULL, 0, 0);
      if (switched_ns) {
        st->current_ns = saved_ns;
      }
      return NULL;
    }
  } else {
    if (argc < (unsigned int)vi) {
      throw_exception(EXCEPTION_ARITY, "Arity mismatch in function call", NULL, 0, 0);
      if (switched_ns) {
        st->current_ns = saved_ns;
      }
      return NULL;
    }
  }

  // OPTIMIZATION: Use static arrays instead of STACK_ALLOC to avoid alloca overhead
  ID current_args[16];
  ID recur_args[16];
  int used_recur_slots = 0;
  ID *params_array = (param_count > 0) ? func->params : NULL;

  // Variadic handling: build effective params/values only when needed
  ID variadic_params[16];
  ID *effective_params = params_array;
  int effective_count = param_count;

  CljList *variadic_rest = NULL;

  if (UNLIKELY(vi >= 0)) {
    // Variadic: params before &, then rest param bound to list
    effective_count = vi + 1;
    for (int i = 0; i < vi; i++) {
      variadic_params[i] = params_array[i];
      current_args[i] = args[i];
    }
    variadic_params[vi] = params_array[vi + 1]; // rest param after &
    // Collect remaining args into list (nil if none)
    CljList *rest = NULL;
    for (int i = argc - 1; i >= vi; i--) {
      CljList *next = make_list(args[i], rest);
      RELEASE(rest);
      rest = next;
    }
    current_args[vi] = rest;
    variadic_rest = rest;
    effective_params = variadic_params;
  } else {
    // Not variadic: direct copy
    for (int i = 0; i < param_count; i++)
      current_args[i] = args[i];
  }

  for (int i = 0; i < effective_count; i++)
    recur_args[i] = NULL;
  int current_argc = effective_count;
  int recur_arg_count = -1;

  // Create call frame with parameters (fixed-size stack variable)
  CLJ_ASSERT(effective_count <= CALLFRAME_MAX_PARAMS && "Too many parameters");
  CallFrame call_frame_storage;
  CallFrame *call_frame = &call_frame_storage;
  frame_init(call_frame, NULL);
  frame_set_bindings_init(call_frame, NULL, effective_params, current_args, current_argc);

  // TCO Loop - iterate on recur
  ID result = NULL;
  // Local owned env_stack for this call. We RETAIN the function's captured env_stack
  // so any mutations we do for nested lets will COW instead of mutating the function.
  CljPersistentVector *call_env_stack = func->env_stack ? (CljPersistentVector *)RETAIN(func->env_stack) : NULL;
  do {
    // Reset recur state for each iteration
    recur_arg_count = -1; // -1 = no tail call

    // OPTIMIZATION: Only cleanup recur_args if recur was actually used in previous iteration
    // For functions without recur (like fib), this check is always false - zero overhead
    if (used_recur_slots > 0) {
      for (int i = 0; i < used_recur_slots; i++) {
        ID rv = recur_args[i];
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
        if (rv && !IS_IMMEDIATE(rv)) {
          int rc = retain_count(rv);
          uint32_t pool_count = autorelease_count(rv);
          if (rc > (int)pool_count) {
            RELEASE(rv);
          }
        }
#else
        RELEASE(rv);
#endif
        recur_args[i] = NULL;
      }
      used_recur_slots = 0;
    }

    // Evaluate function body with context (stack-only, no allocations)
    EvalContext eval_ctx = {
        .env = NULL,
        .env_stack = call_env_stack, // Closure environment stack (vector of maps)
        .frame = call_frame,         // Stack-based frame for parameters
        .st = st,
        .current_fn = fn,
        .recur_args = recur_args,
        .recur_arg_count = &recur_arg_count,
        .recur_param_count = param_count // Fixed for this function
    };
    // If an exception is thrown, longjmp will jump to the outer handler and this function
    // will never return, so the loop will not continue
    ID new_result = eval_body_with_params(func->body, &eval_ctx);
    // Check if recur was triggered in THIS function
    // With C stack, nested functions have their own stack frames, so recur_arg_count
    // only changes if recur was used in THIS function
    if (recur_arg_count >= 0) {
      // Tail call detected: recur was used in this function
      CLJ_ASSERT(recur_arg_count <= param_count);
      // new_result may be borrowed/autoreleased (e.g. list literals).
      // Do not release it here; recur ignores this value semantically.

      // Update argc and copy new arguments from recur_args
      CLJ_ASSERT(recur_arg_count >= 0 && recur_arg_count <= param_count);
      current_argc = recur_arg_count;
      for (int i = 0; i < current_argc; i++) {
        // Ownership note:
        // eval_handle_recur() RETAINs each evaluated recur arg and stores it in recur_args[].
        // frame_set_bindings() below RETAINs again for the call frame. We must keep
        // recur_args[] populated until the next loop iteration's cleanup releases the
        // transfer retain; clearing here leaks one retain per recur step.
        current_args[i] = recur_args[i];
      }
      used_recur_slots = current_argc;

      // Recreate call frame with new parameters (stack-allocated)
      frame_set_bindings(call_frame, NULL, effective_params, current_args, current_argc);

      // Continue loop - recur_arg_count will be reset at the start of the next iteration
      continue;
    }

    // No recur - this is the final result.
    // Do not retain here: callee results are already safe for the caller's pool.
    result = new_result;
    break;
  } while (true);

  // current_args[i] is stored in call_env, and call_env holds a reference to it.
  // If we release current_args[i] here, the object might be freed, but call_env
  // still holds a pointer to it. When call_env is released later, RELEASE will
  // be called on the already-freed object, causing a use-after-free error.
  // The call_env will be released below, which will properly release all stored values.

  // OPTIMIZATION: Only cleanup if recur args were actually set
  if (used_recur_slots > 0) {
    for (int i = 0; i < used_recur_slots; i++) {
      ID rv = recur_args[i];
#if defined(DEBUG) && defined(ZOMBIE_ENABLED)
      if (rv && !IS_IMMEDIATE(rv)) {
        int rc = retain_count(rv);
        uint32_t pool_count = autorelease_count(rv);
        if (rc > (int)pool_count) {
          RELEASE(rv);
        }
      }
#else
      RELEASE(rv);
#endif
    }
  }

  // Cleanup call frame (stack-allocated, but may contain retained values)
  frame_release(call_frame);

  RELEASE(call_env_stack);
  if (variadic_rest && (ID)variadic_rest != result) {
    RELEASE(variadic_rest);
  }
  if (switched_ns) {
    st->current_ns = saved_ns;
  }

  return result;
}

// DRY: Central symbol resolution function with environment stack support
// Resolves symbol by searching through environment stack (vector of maps)
static INLINE CljPersistentMap *env_stack_head(CljPersistentVector *stack) {
  return env_stack_top(stack);
}

static INLINE CljPersistentMap *get_closure_env(const EvalContext *ctx) {
  if (!ctx) {
    return NULL;
  }
  CljPersistentMap *from_stack = env_stack_head(ctx->env_stack);
  if (from_stack) {
    return from_stack;
  }
  return ctx->env;
}

static INLINE EvalState *get_eval_state(const EvalContext *ctx, EvalState *fallback) {
  if (ctx && ctx->st) {
    return ctx->st;
  }
  return fallback;
}

static const EvalContext *ensure_eval_context(CljPersistentMap *env,
                                              EvalState *st,
                                              const EvalContext *ctx,
                                              EvalContext *local_ctx,
                                              CljPersistentVector **owned_stack) {
  *owned_stack = NULL;
  if (!ctx) {
    *local_ctx = (EvalContext){
        .env = env,
        .env_stack = NULL,
        .frame = NULL,
        .st = st,
        .current_fn = NULL,
        .recur_args = NULL,
        .recur_arg_count = NULL,
        .recur_param_count = 0};
    return local_ctx;
  }

  *local_ctx = *ctx;

  if (!local_ctx->env_stack && env) {
    // Do not synthesize an env_stack from the env map: env is borrowed and must not
    // be retained/released via a temporary stack container.
    local_ctx->env_stack = NULL;
  }

  if (!local_ctx->env) {
    local_ctx->env = get_closure_env(local_ctx);
    if (!local_ctx->env) {
      local_ctx->env = env;
    }
  }

  if (!local_ctx->st) {
    local_ctx->st = st;
  }

  return local_ctx;
}

// Forward declarations (defined later in this file)
static INLINE bool is_dynamic_var_symbol(const CljSymbol *symbol);
static INLINE ID dynamic_binding_lookup(EvalState *st, CljSymbol *symbol);

// Extended version that also searches in CallFrame
static INLINE ID resolve_symbol_in_env_with_frame(CljPersistentVector *env_stack, CljPersistentMap *fallback_env, CallFrame *frame, ID sym, EvalState *st) {
  if (!sym || TAG(sym) != CLJ_SYMBOL) {
    return NOT_FOUND;
  }

  // Fast-path: Check frame first (most common case for parameters)
  if (frame) {
    ID frame_value = NOT_FOUND;
    if (frame_lookup(frame, sym, &frame_value)) {
      if (frame_value == NOT_FOUND) {
        return (ID)SYM_NIL;
      }
      return frame_value;
    }
  }

  // OPTIMIZATION: If no env_stack and no fallback_env, go direct to namespace
  // This is common for function calls like (fib ...) where fib is in namespace
  if (!env_stack && !fallback_env) {
    // Fast-path: check current namespace mappings directly so a "found but nil" value is preserved.
    if (st && st->current_ns && st->current_ns->mappings) {
      ID resolved = map_get(st->current_ns->mappings, sym);
      if (resolved != NOT_FOUND) {
        return resolved ? resolved : (ID)SYM_NIL;
      }
    }

    // Dynamic vars can be bound to nil (NULL) and must be treated as resolved.
    if (st) {
      CljSymbol *sym_obj = as_symbol(sym);
      if (sym_obj && is_dynamic_var_symbol(sym_obj)) {
        ID bound = dynamic_binding_lookup(st, sym_obj);
        if (bound != NOT_FOUND) {
          return bound ? bound : (ID)SYM_NIL;
        }
      }
    }

    // Final fallback: full symbol resolution (may throw on unresolved).
    if (st) {
      ID resolved_ns = eval_symbol(as_symbol(sym), st);
      if (resolved_ns && resolved_ns != sym) {
        return resolved_ns;
      }
    }

    return NOT_FOUND;
  }

  // Search env_stack (for let bindings, closure captures)
  ENV_STACK_FOR_EACH_REVERSE(env_stack, env_obj_id) {
    if (is_map(env_obj_id)) {
      CljPersistentMap *env = (CljPersistentMap *)env_obj_id;
      ID resolved = map_get(env, sym);
      if (resolved != NOT_FOUND) {
        return resolved ? resolved : (ID)SYM_NIL;
      }
    }
  }

  if (fallback_env) {
    ID resolved = map_get(fallback_env, sym);
    if (resolved != NOT_FOUND) {
      return resolved ? resolved : (ID)SYM_NIL;
    }
  }

  // Fallback to namespace mappings directly (preserves "found but nil").
  if (st && st->current_ns && st->current_ns->mappings) {
    ID resolved = map_get(st->current_ns->mappings, sym);
    if (resolved != NOT_FOUND) {
      return resolved ? resolved : (ID)SYM_NIL;
    }
  }

  // Dynamic vars can be bound to nil (NULL) and must be treated as resolved.
  if (st) {
    CljSymbol *sym_obj = as_symbol(sym);
    if (sym_obj && is_dynamic_var_symbol(sym_obj)) {
      ID bound = dynamic_binding_lookup(st, sym_obj);
      if (bound != NOT_FOUND) {
        return bound ? bound : (ID)SYM_NIL;
      }
    }
  }

  // Final fallback: full symbol resolution.
  if (st) {
    ID resolved_ns = eval_symbol(as_symbol(sym), st);
    if (resolved_ns && resolved_ns != sym) {
      return resolved_ns;
    }
  }

  return NOT_FOUND;
}

// NOTE: CallFrame→env_stack materialization was previously eager.
// The closure capture path now uses lazy stack-backed env_stack + promotion.

// Helper: Add namespace mappings to environment

// Evaluate body with environment lookup (for loops)
ID eval_body_with_env(ID body, CljPersistentMap *env, EvalState *st) {
  CLJ_ASSERT(env != NULL);
  CLJ_ASSERT(body != NULL);

  // Check if body is an immediate value
  if (IS_IMMEDIATE(body)) {
    return body;
  }

  CljObject *body_obj = (CljObject *)body;
  switch (body_obj->type) {
  case CLJ_SYMBOL: {
    // Look up symbol in environment
    return map_get_sentinel((CljPersistentMap *)env, body, NULL);
  }

  case CLJ_LIST:
  case CLJ_AST_NODE: {
    return eval_noncanonical_list_form(body);
  }
  case CLJ_AST_CALL: {
    return eval_ast_call(as_ast_call(body), env, st, NULL);
  }

  default:
    // Literal value
    return body;
  }
}

// Simplified body evaluation with parameter binding
ID eval_body_with_params(ID body, const EvalContext *ctx) {
  // Handle nil body gracefully (represents Clojure nil)
  if (!body) {
    return NULL;
  }

  // Immediate values don't need retain/release and must not be treated as pointers.
  if (IS_IMMEDIATE(body)) {
    return body;
  }
  // Heap object invariant: non-immediate IDs must be valid object pointers.
  CLJ_ASSERT((uintptr_t)body >= 0x1000 && "eval_body_with_params: invalid non-immediate pointer");

  unsigned char body_tag = TAG(body);

  // Lexical addressing fast-path: (depth, slot) reference into CallFrame chain.
  if (body_tag == CLJ_SLOT_REF) {
    const CljSlotRef *ref = (const CljSlotRef *)body;
    CLJ_ASSERT(ctx && ctx->frame && "CLJ_SLOT_REF requires frame context");
    ID v = frame_get_slot(ctx->frame, ref->depth, ref->slot);
    if (v == NOT_FOUND || !v)
      return NULL;
    if (IS_IMMEDIATE(v))
      return v;
    // Frame owns this value; return a caller-usable reference per MEMORY_POLICY.
    return AUTORELEASE(RETAIN(v));
  }

  if (body_tag == CLJ_SYMBOL) {
    // CRITICAL: Check if symbol is a keyword FIRST - keywords evaluate to themselves
    // This must come BEFORE symbol resolution attempts
    if (IS_KEYWORD(body)) {
      return body;
    }

    CljSymbol *body_sym = as_symbol(body);
    CLJ_ASSERT(body_sym != NULL && "is_symbol(body) but as_symbol returned NULL");

    // Use central symbol resolution function (DRY: handles environment stack and frames)
    if (ctx) {
      CljPersistentMap *ctx_env_map = ctx->env_stack ? env_stack_head(ctx->env_stack) : NULL;
      // First check frame directly - frame lookups can legitimately return symbols
      // (e.g., macro parameters like (defmacro m [name] name) called with (m name))
      if (ctx->frame) {
        ID frame_value = NOT_FOUND;
        if (frame_lookup(ctx->frame, body, &frame_value)) {
          if (frame_value == NOT_FOUND) {
            return NULL; // Parameter bound to nil
          }
          // Frame lookups return the bound value directly - no self-resolution check
          // This is correct for macros where a symbol parameter can have a symbol value
          CLJ_ASSERT(frame_value && "frame lookup must return value or NOT_FOUND");
          if (IS_IMMEDIATE(frame_value))
            return frame_value;
          return AUTORELEASE(RETAIN(frame_value));
        }
      }

      // Then check env_stack and namespace.
      // If there is no env_stack, we still must use ctx->env as the fallback environment
      // (e.g. eval_body_vector_with_base_env tests pass bindings via ctx->env only).
      CljPersistentMap *fallback_env = ctx_env_map ? ctx_env_map : ctx->env;
      ID resolved_id = resolve_symbol_in_env_with_frame(ctx->env_stack, fallback_env, NULL, body, get_eval_state(ctx, NULL));
      if (resolved_id != NOT_FOUND) {
        if (!resolved_id || resolved_id == SYM_NIL) {
          return NULL;
        }
        // CRITICAL: If resolved_id is still a symbol (not a value), throw exception
        // This prevents infinite loops where a symbol resolves to itself
        // NOTE: This check only applies to env_stack/namespace lookups, not frame lookups
        if (is_symbol(resolved_id) && !IS_KEYWORD(resolved_id)) {
          bool resolves_to_self = (resolved_id == body);
          if (!resolves_to_self && resolved_id && body) {
            bool structural_equal = clj_equal(resolved_id, body);
            resolves_to_self = structural_equal;
          }
          if (resolves_to_self) {
            throw_unresolved_symbol_exception_symbol(body_sym);
          }
        }
        return AUTORELEASE(RETAIN(resolved_id));
      }

      ID self_fn = resolve_current_closure_self_symbol(ctx, body);
      if (self_fn != NOT_FOUND) {
        return AUTORELEASE(RETAIN(self_fn));
      }
    }
    // If still not found, try namespace lookup (for recursive function calls)
    // ns_resolve takes CljObject* (only objects, not immediates) and returns ID
    // body is a symbol (CljObject*), so we can pass it directly
    EvalState *ctx_state_after_env = get_eval_state(ctx, NULL);
    if (ctx_state_after_env) {
      ID resolved_id = ns_resolve(ctx_state_after_env, as_symbol(body));
      if (resolved_id != NOT_FOUND) {
        if (!resolved_id || resolved_id == SYM_NIL) {
          return NULL;
        }
        // This can happen if a symbol is stored in namespace instead of its value
        // In this case, we should throw an exception instead of returning the symbol
        if (is_symbol(resolved_id)) {
          // Symbol found in namespace but value is also a symbol - this is an error
          throw_unresolved_symbol_exception_symbol(as_symbol(body));
          return NULL;
        }
        return AUTORELEASE(RETAIN(resolved_id));
      }
    }
    // Special case: nil should evaluate to NULL (not SYM_NIL)
    if (body == SYM_NIL) {
      // nil is represented as NULL - return NULL directly
      return NULL;
    }
    // Symbol not found - throw exception
    CljSymbol *sym_obj = as_symbol(body);
    throw_unresolved_symbol_exception_symbol(sym_obj);
  }

  // For non-symbol, non-slotref objects, dispatch on the already computed tag.
  switch (body_tag) {
  case CLJ_AST_NODE: {
    // AST-Nodes can contain vectors as first element
    // Check if first element is a vector (by tag, not is_vector which may have additional checks)
    CljASTNode *node = as_ast_node(body);
    if (node && node->first && (TAG(node->first) == CLJ_VECTOR_PERSISTENT || TAG(node->first) == CLJ_VECTOR_TRANSIENT)) {
      // AST-Node wrapping a vector - evaluate the vector
      return eval_body_with_params(node->first, ctx);
    }
    // Otherwise treat as legacy list-like form.
    return eval_noncanonical_list_form(body);
  }
  case CLJ_AST_CALL: {
    CljPersistentMap *env_map = get_closure_env(ctx);
    EvalState *ctx_state = get_eval_state(ctx, NULL);
    if (!ctx_state)
      ctx_state = builtin_get_eval_state();
    return eval_ast_call(as_ast_call(body), env_map, ctx_state, ctx);
  }

  case CLJ_LIST: {
    // Legacy non-canonical list forms are rejected.
    return eval_noncanonical_list_form(body);
  }

  case CLJ_VECTOR_PERSISTENT:
  case CLJ_VECTOR_TRANSIENT: {
    // Vector literals need to have their elements evaluated
    CljPersistentVector *vec = (CljPersistentVector *)body;
    unsigned int count = vector_count(vec);

    // Empty vector - return as-is
    if (count == 0) {
      return body;
    }

    // Create new vector with evaluated elements
    CljPersistentVector *result = make_vector(count, STRONG);

    VECTOR_FOR_EACH(vec, elem) {
      ID eval_elem = NULL;

      // Evaluate element recursively
      if (elem) {
        eval_elem = eval_body_with_params(elem, ctx);
      }

      // vector_conj may return a new owned vector (or the same pointer in-place).
      // Keep a single owned reference in `result` without extra RETAIN churn.
      CljPersistentVector *old_result = result;
      result = vector_conj_owned(result, eval_elem);
      if (result != old_result) {
        RELEASE(old_result);
      }
    }

    return AUTORELEASE(result);
  }

  case CLJ_MAP_PERSISTENT: {
    CljPersistentMap *map = (CljPersistentMap *)body;
    CljPersistentMap *result = map_empty();

    MAP_FOR_EACH(map, key, value) {
      ID eval_key = key ? eval_body_with_params(key, ctx) : NULL;
      ID eval_value = value ? eval_body_with_params(value, ctx) : NULL;

      map_assoc_inplace(&result, eval_key, eval_value);
    }

    return AUTORELEASE(result);
  }

  default:
    // Literal value
    return body;
  }
}

// Non-ctx eval body path: separated to prevent its large frame (many locals in switch)
// from bloating eval_body when only the ctx-forwarding path is taken.
// This saves ~250 bytes of stack per eval_body call in the common (ctx != NULL) case.
static ID __attribute__((noinline)) eval_body_no_ctx(ID body, CljPersistentMap *env, EvalState *st) {
  if (!body) {
    return NULL;
  }

  CljPersistentMap *eval_env = eval_env_or_ns_mappings(env, st);
  if (!eval_env) {
    throw_exception(EXCEPTION_RUNTIME, "Missing evaluation environment", __FILE__, __LINE__, 0);
  }

  // Handle immediate values (fixnums, chars, booleans, nil)
  if (IS_IMMEDIATE(body)) {
    return body; // Immediate values evaluate to themselves
  }

  // Simplified implementation - would normally evaluate the AST
  switch (((CljObject *)body)->type) {
  case CLJ_LIST:
  case CLJ_AST_NODE: {
    return eval_noncanonical_list_form(body);
  }
  case CLJ_AST_CALL: {
    return eval_ast_call(as_ast_call(body), eval_env, st, NULL);
  }
  case CLJ_SYMBOL: {
    // Check if symbol is a keyword - keywords evaluate to themselves
    // CRITICAL: This must come BEFORE symbol resolution attempts
    if (IS_KEYWORD(body)) {
      return body;
    }

    // Special case: nil should evaluate to NULL (not SYM_NIL)
    if (body == SYM_NIL) {
      return NULL; // nil evaluates to NULL
    }

    // No ctx in this path (ctx-based resolution handled by eval_body_with_params)

    // Resolve symbol - first try local environment, then namespace
    // Note: We need to check if key exists, not just if value is non-NULL,
    // because nil (NULL) is a valid value
    if (is_map(eval_env)) {
      // Use sentinel to distinguish "key not found" from "value is nil"
      ID result_id = map_get((CljPersistentMap *)eval_env, body);
      if (result_id != NOT_FOUND) {
        return AUTORELEASE(RETAIN(result_id));
      }
    }

    // If not found in local environment, try namespace
    if (st && st->current_ns && st->current_ns->mappings) {
      // Use sentinel to distinguish "key not found" from "value is nil"
      ID result_id = map_get(st->current_ns->mappings, body);
      if (result_id != NOT_FOUND) {
        return AUTORELEASE(RETAIN(result_id));
      }
    }

    // If still not found, try global symbol resolution (includes clojure.core)
    // This is important for built-in functions like inc, dec, etc.
    if (st) {
      CljSymbol *sym = as_symbol(body);
      if (sym && is_dynamic_var_symbol(sym)) {
        ID bound = dynamic_binding_lookup(st, sym);
        if (bound != NOT_FOUND) {
          return AUTORELEASE(RETAIN(bound)); // may be NULL for nil
        }
      }

      ID resolved = eval_symbol(sym, st);
      if (resolved) {
        // Special case: nil should evaluate to NULL (not SYM_NIL)
        if (resolved == SYM_NIL) {
          return NULL; // nil evaluates to NULL
        }
        return AUTORELEASE(RETAIN(resolved));
      }
    }

    // Symbol not found - this should throw an exception
    throw_exception(EXCEPTION_RUNTIME, "Unable to resolve symbol in this context",
                    __FILE__, __LINE__, 0);
    return NULL;
  }

  case CLJ_VECTOR_PERSISTENT: {
    // Vector literals need to have their elements evaluated
    // This is necessary for cases like [(f x) (g x)] where f and g should be called
    CljPersistentVector *vec = (CljPersistentVector *)body;
    unsigned int count = vector_count(vec);

    // Empty vector - return as-is
    if (count == 0) {
      return body;
    }

    // Create new vector with evaluated elements
    CljPersistentVector *result = make_vector(count, STRONG);

    VECTOR_FOR_EACH(vec, elem) {
      ID eval_elem = NULL;

      // Check for SYM_NIL before calling eval_body
      if (is_symbol(elem) && elem == SYM_NIL) {
        eval_elem = NULL; // nil evaluates to NULL
      } else if (elem) {
        eval_elem = eval_body_no_ctx(elem, env, st);
      }

      CljPersistentVector *old_result = result;
      result = vector_conj_owned(result, eval_elem);
      if (result != old_result) {
        RELEASE(old_result);
      }
    }

    return AUTORELEASE(result);
  }

  case CLJ_MAP_PERSISTENT: {
    // Map literals need to have their keys and values evaluated
    // This is necessary for cases like {nil "value"} where nil should be evaluated to NULL
    CljPersistentMap *map = (CljPersistentMap *)body;
    CljPersistentMap *result = map_empty();

    MAP_FOR_EACH(map, key, value) {
      // Cache tags for performance
      int key_tag = key ? TAG(key) : 0;
      int value_tag = value ? TAG(value) : 0;

      // Evaluate key and value (nil should evaluate to NULL)
      // Check for SYM_NIL before calling eval_body to avoid symbol resolution
      ID eval_key = NULL;
      if (key && key_tag == CLJ_SYMBOL && key == (CljObject *)SYM_NIL) {
        eval_key = NULL; // nil evaluates to NULL
      } else if (key) {
        eval_key = eval_body_no_ctx(key, env, st);
      }

      ID eval_value = NULL;
      if (value && value_tag == CLJ_SYMBOL && value == (CljObject *)SYM_NIL) {
        eval_value = NULL; // nil evaluates to NULL
      } else if (value) {
        eval_value = eval_body_no_ctx(value, env, st);
      }

      map_assoc_inplace(&result, eval_key, eval_value);

      // eval_body_no_ctx follows MEMORY_POLICY; do not RELEASE here.
    }

    return AUTORELEASE(result);
  }

  default:
    // Literal value
    return body;
  }
}

// Lightweight dispatcher: when ctx is set, forward directly to eval_body_with_params
// without allocating the full frame of eval_body_no_ctx. This saves ~250 bytes of stack
// per eval recursion level (measured: 304 → ~48 bytes).
ID eval_body(ID body, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  if (ctx) {
    return eval_body_with_params(body, ctx);
  }
  return eval_body_no_ctx(body, env, st);
}

// Non-canonical list forms are no longer evaluated via runtime list->AST bridges.
// Keep empty-list compatibility (`()` currently evaluates to nil in this runtime),
// and fail fast on any non-empty list to catch upstream canonicalization regressions.
static ID eval_noncanonical_list_form(ID list_form) {
  if (!list_form || !is_list_type(TAG(list_form)))
    return NULL;
  CljList *list = as_list(list_form);
  if (!list || list_empty(list))
    return NULL;
  throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                            "Cannot evaluate non-canonical list form");
  return NULL;
}
static ID resolve_list_operator(ID op, CljPersistentMap *env, EvalState *st, const EvalContext *ctx,
                                CljObject *call_form, bool hot_ctx_cache_already_checked);

// Recursion depth tracking for nested eval_arg/call dispatch.
// tiny-clj is currently single-threaded in supported runtime modes; keep this as a
// plain static to avoid thread-local access overhead in the eval hot path.
static int g_eval_arg_depth = 0;

// Recursion depth tracking for eval_ast_call (stack overflow guard).
static int g_eval_ast_call_depth = 0;

// Stack-based recursion guard: measures actual C stack usage (no heap alloc needed).
// s_eval_stack_base is set at top-level eval entry (eval_parsed_value) and compared
// against the current stack position in eval_ast_call.  After longjmp-based exception
// recovery, the stack unwinds, so the measurement auto-corrects.
static uintptr_t s_eval_stack_base = 0;
#ifdef DEBUG
static ptrdiff_t s_eval_stack_peak = 0;
#endif

// Maximum eval stack consumption before we throw StackOverflowError.
// ESP32 REPL builds run with a 32 KB main task stack; keep a guard but leave
// enough room so larger library namespaces can still load on-device.
// Desktop: 8 MB stack, so 256 KB is very conservative.
#if defined(ESP_PLATFORM) || defined(ESP32_BUILD)
#define EVAL_STACK_LIMIT 28672 /* 28 KB */
#define EVAL_STACK_CHECK_START_DEPTH 4
#define EVAL_STACK_CHECK_INTERVAL_MASK 0x1 /* check every 2 frames after start */
#else
#define EVAL_STACK_LIMIT 2097152 /* 2 MB (desktop stack is 8 MB+) */
#define EVAL_STACK_CHECK_START_DEPTH 16
#define EVAL_STACK_CHECK_INTERVAL_MASK 0x7 /* check every 8 frames after start */
#endif

// Reset eval depths (for test isolation and after exception recovery)
void reset_eval_depths(void) {
  g_eval_arg_depth = 0;
  g_eval_ast_call_depth = 0;
}

#ifdef DEBUG
ptrdiff_t eval_stack_peak(void) { return s_eval_stack_peak; }
#endif

// Legacy alias
void reset_eval_arg_depth(void) {
  reset_eval_depths();
}

// ============================================================================
// Helper functions for call dispatch and environment lookup
// ============================================================================

static INLINE bool is_dynamic_var_symbol(const CljSymbol *symbol) {
  return symbol && ((symbol->base.flags & CLJ_FLAG_DYNAMIC) != 0);
}

static INLINE ID dynamic_binding_lookup(EvalState *st, CljSymbol *symbol) {
  if (!st || !symbol || !st->dynamic_bindings) {
    return NOT_FOUND;
  }

  CljPersistentVector *bindings = vector_persistent(st->dynamic_bindings);
  if (!bindings)
    return NOT_FOUND;
  unsigned int depth = vector_count(bindings);
  for (unsigned int i = depth; i > 0; i--) {
    ID frame_id = vector_nth(bindings, i - 1);
    if (!frame_id || TAG(frame_id) != CLJ_MAP_PERSISTENT) {
      continue;
    }
    ID v = map_get((CljPersistentMap *)frame_id, (ID)symbol);
    if (v != NOT_FOUND) {
      // v may be NULL (nil) - that's a valid binding value.
      return v;
    }
  }

  return NOT_FOUND;
}

// Handle recur special form
// Resolve operator symbol from environment or namespace
// DRY: Uses central resolve_symbol_in_env function
static INLINE ID callsite_get_cached_resolution(ID call_form, CljSymbol *symbol, uint16_t epoch) {
  if (!call_form || !symbol)
    return NULL;
  CljType tag = TAG(call_form);
  if (tag == CLJ_AST_NODE) {
    return ast_node_get_cached_resolution((CljASTNode *)call_form, symbol, epoch);
  }
  if (tag == CLJ_AST_CALL) {
    return ast_call_get_cached_resolution((CljASTCall *)call_form, symbol, epoch);
  }
  return NULL;
}

/* Hot path helper for eval_ast_call: avoids generic call-form dispatch and extra function calls. */
static INLINE ID ast_call_get_cached_resolution_fast(const CljASTCall *call, CljSymbol *symbol, uint16_t epoch) {
  if (!call || !symbol)
    return NULL;
  ID cache_obj = call->callsite_cache;
  if (!cache_obj || TAG(cache_obj) != CLJ_CALLSITE_CACHE)
    return NULL;
  CljCallsiteCache *cache = (CljCallsiteCache *)cache_obj;
  if (cache->symbol != symbol || cache->epoch != epoch || cache->epoch_generation != g_runtime.resolve_cache_generation)
    return NULL;
  return cache->resolved;
}

static INLINE void callsite_update_cache(ID call_form, CljSymbol *symbol, ID resolved, uint16_t epoch) {
  if (!call_form || !symbol || !resolved)
    return;
  CljType tag = TAG(call_form);
  if (tag == CLJ_AST_NODE) {
    ast_node_update_callsite_cache((CljASTNode *)call_form, symbol, resolved, epoch);
    return;
  }
  if (tag == CLJ_AST_CALL) {
    ast_call_update_callsite_cache((CljASTCall *)call_form, symbol, resolved, epoch);
    return;
  }
}

static INLINE ID resolve_list_operator(ID op, CljPersistentMap *env, EvalState *st, const EvalContext *ctx,
                                       CljObject *call_form, bool hot_ctx_cache_already_checked) {
  if (!op)
    return op;
  bool op_is_immediate = IS_IMMEDIATE(op);
  unsigned char op_tag = op_is_immediate ? 0 : TAG(op);

  // Lexical addressing: operator can be a SlotRef (e.g. higher-order calls like (pred x)).
  if (!op_is_immediate && op_tag == CLJ_SLOT_REF) {
    const CljSlotRef *ref = (const CljSlotRef *)op;
    if (ctx && ctx->frame) {
      // NOTE: NULL means nil; NOT_FOUND means invalid slot/depth.
      ID v = frame_get_slot(ctx->frame, ref->depth, ref->slot);
      return (v == NOT_FOUND) ? NULL : v;
    }
    return NULL;
  }

  if (op_is_immediate || op_tag != CLJ_SYMBOL) {
    return op;
  }

  CljSymbol *op_sym = as_symbol(op);
  bool op_is_dynamic = is_dynamic_var_symbol(op_sym);

  // === HOT PATH: ctx present (typical for fib and other recursive functions) ===
  if (ctx) {
    // 1) Frame lookup (parameters like n) - fastest path
    if (ctx->frame) {
      ID frame_value = NULL;
      if (frame_lookup(ctx->frame, op, &frame_value)) {
        return frame_value;
      }
    }

    // 2) Callsite cache (cached functions like fib itself)
    // IMPORTANT: Dynamic vars must never use callsite caches.
    if (!hot_ctx_cache_already_checked && !op_is_dynamic && call_form && g_runtime.resolve_cache_epoch != 0) {
      bool call_form_is_ast_call = (call_form && TAG(call_form) == CLJ_AST_CALL);
      ID cached_call =
          call_form_is_ast_call
              ? ast_call_get_cached_resolution_fast((const CljASTCall *)call_form, op_sym, g_runtime.resolve_cache_epoch)
              : callsite_get_cached_resolution((ID)call_form, op_sym, g_runtime.resolve_cache_epoch);
      if (cached_call) {
        return cached_call;
      }
    }

    // Resolve cache disabled: skip global cache lookup
  }

  // === COLD PATH: ctx fehlt oder Cache-Miss ===
  EvalContext local_ctx;
  CljPersistentVector *owned_env_stack = NULL;
  const EvalContext *effective_ctx = ctx;
  EvalState *ctx_st = get_eval_state(ctx, st);
  CljPersistentVector *resolve_stack = ctx ? ctx->env_stack : NULL;
  if (!effective_ctx) {
    effective_ctx = ensure_eval_context(env, st, NULL, &local_ctx, &owned_env_stack);
    ctx_st = get_eval_state(effective_ctx, st);
    resolve_stack = effective_ctx ? effective_ctx->env_stack : NULL;
  }
  ID resolved = NULL;

  // NOTE: We intentionally do NOT drop a single-frame env_stack here.
  // A single map frame can still hold real lexical bindings (e.g. let-recursion self-binding).

  // Parameter validation: Check st and ctx before continuing
  if (!ctx_st || !effective_ctx) {
    // Fallback to namespace lookup if context not available
    resolved = eval_symbol(op_sym, ctx_st);
    ID return_value = resolved ? resolved : op;
    RELEASE(owned_env_stack);
    return return_value;
  }

  bool cache_enabled = g_runtime.resolve_cache_epoch != 0;
  bool allow_callsite_cache = call_form && op_sym && !op_is_dynamic && !resolve_stack && cache_enabled;

  // OPTIMIZATION: Qualified symbols skip env_stack - go direct to namespace
  // Unqualified symbols check env_stack first (let bindings, closures)
  bool is_qualified = op_sym && op_sym->ns_name;

  // Check env_stack only if not qualified and stack exists
  if (!is_qualified && resolve_stack) {
    ENV_STACK_FOR_EACH_REVERSE(resolve_stack, env_obj) {
      if (is_map(env_obj)) {
        ID found = map_get((CljPersistentMap *)env_obj, op);
        if (found != NOT_FOUND) {
          resolved = found;
          break;
        }
      }
    }
  }

  if (resolved) {
    RELEASE(owned_env_stack);
    return resolved;
  }

  if (effective_ctx) {
    ID self_fn = resolve_current_closure_self_symbol(effective_ctx, op);
    if (self_fn != NOT_FOUND) {
      RELEASE(owned_env_stack);
      return self_fn;
    }
  }

  // Namespace lookup - this result can be cached
  // For qualified symbols, this is the primary lookup path
  // For unqualified symbols, this is the fallback after env_stack
  resolved = eval_symbol(op_sym, ctx_st);

  // Callsite cache (optimization)
  if (cache_enabled && allow_callsite_cache && !op_is_dynamic && resolved && ctx_st && ctx_st->current_ns && TAG(resolved) != CLJ_SYMBOL) {
    callsite_update_cache(call_form, op_sym, resolved, g_runtime.resolve_cache_epoch);
  }

  RELEASE(owned_env_stack);
  return resolved ? resolved : op;
}

static inline bool is_map_like_eval(ID obj) {
  if (!obj)
    return false;
  unsigned char tag = TAG(obj);
  return tag == CLJ_MAP_PERSISTENT || tag == CLJ_MAP_TRANSIENT || tag == CLJ_RECORD;
}

static inline ID keyword_lookup_default_result(ID default_val) {
  return default_val;
}

static inline ID map_like_get_sentinel_eval(ID map_like, ID key, ID not_found) {
  if (!map_like)
    return not_found;
  unsigned char tag = TAG(map_like);
  if (tag == CLJ_MAP_PERSISTENT || tag == CLJ_MAP_TRANSIENT) {
    return map_get_sentinel((CljValue)map_like, (CljValue)key, (CljValue)not_found);
  }
  if (tag == CLJ_RECORD) {
    return record_get_sentinel(map_like, key, not_found);
  }
  return not_found;
}

static INLINE ID callsite_get_cache_obj(ID call_form) {
  if (!call_form)
    return NULL;
  CljType tag = TAG(call_form);
  if (tag == CLJ_AST_NODE) {
    return ast_node_get_callsite_cache((const CljASTNode *)call_form);
  }
  if (tag == CLJ_AST_CALL) {
    return ast_call_get_callsite_cache((const CljASTCall *)call_form);
  }
  return NULL;
}

static INLINE void callsite_set_cache_obj(ID call_form, ID cache_obj) {
  if (!call_form)
    return;
  CljType tag = TAG(call_form);
  if (tag == CLJ_AST_NODE) {
    ast_node_set_callsite_cache((CljASTNode *)call_form, cache_obj);
    return;
  }
  if (tag == CLJ_AST_CALL) {
    ast_call_set_callsite_cache((CljASTCall *)call_form, cache_obj);
    return;
  }
}

static INLINE CljCallsiteCache *callsite_cache_for_keyword_lookup(ID call_form, CljSymbol *keyword_sym) {
  if (!call_form || !keyword_sym)
    return NULL;

  CljCallsiteCache *cache = as_callsite_cache(callsite_get_cache_obj(call_form));
  if (!cache) {
    ID created = AUTORELEASE(make_callsite_cache(keyword_sym, (ID)keyword_sym, g_runtime.resolve_cache_epoch));
    callsite_set_cache_obj(call_form, created);
    cache = as_callsite_cache(callsite_get_cache_obj(call_form));
    if (!cache)
      return NULL;
  }

  if (cache->symbol != keyword_sym ||
      cache->epoch != g_runtime.resolve_cache_epoch ||
      cache->epoch_generation != g_runtime.resolve_cache_generation) {
    cache->symbol = keyword_sym;
    cache->epoch = g_runtime.resolve_cache_epoch;
    cache->epoch_generation = g_runtime.resolve_cache_generation;
    ASSIGN(cache->resolved, (ID)keyword_sym);
    cache->lookup_hint_index = UINT8_MAX;
  }

  return cache;
}

static INLINE void callsite_cache_store_lookup_hint(CljCallsiteCache *cache, int index) {
  if (!cache)
    return;
  if (index >= 0 && index < (int)UINT8_MAX) {
    cache->lookup_hint_index = (uint8_t)index;
  } else {
    cache->lookup_hint_index = UINT8_MAX;
  }
}

static INLINE ID map_get_with_lookup_hint(ID map_obj, ID key, uint8_t hint_index, int *out_index) {
  if (out_index)
    *out_index = -1;
  CljPersistentMap *map_data = map_backing(map_obj);
  if (!map_data || map_data->count <= 0)
    return NOT_FOUND;

  unsigned int count = (unsigned int)map_data->count;
  if (hint_index != UINT8_MAX && (unsigned int)hint_index < count) {
    ID hinted_key = (ID)map_data->data[2 * hint_index];
    if (hinted_key == key || (hinted_key && key && clj_equal(hinted_key, key))) {
      if (out_index)
        *out_index = (int)hint_index;
      return (ID)map_data->data[2 * hint_index + 1];
    }
  }

  for (unsigned int i = 0; i < count; i++) {
    ID stored_key = (ID)map_data->data[2 * i];
    if (stored_key == key || (stored_key && key && clj_equal(stored_key, key))) {
      if (out_index)
        *out_index = (int)i;
      return (ID)map_data->data[2 * i + 1];
    }
  }

  return NOT_FOUND;
}

static INLINE ID record_get_with_lookup_hint(ID record_obj, ID key, uint8_t hint_index, int *out_index) {
  if (out_index)
    *out_index = -1;
  CljPersistentRecord *record = as_record(record_obj);
  if (!record)
    return NOT_FOUND;

  unsigned int field_count = record_declared_field_count(record);
  if (hint_index != UINT8_MAX && (unsigned int)hint_index < field_count) {
    ID hinted_key = record_key_at_index(record_obj, hint_index);
    if (hinted_key == key || (hinted_key && key && clj_equal(hinted_key, key))) {
      if (out_index)
        *out_index = (int)hint_index;
      return record_get_by_index(record_obj, hint_index);
    }
  }

  int index = record_field_index(record_obj, key);
  if (index >= 0) {
    if (out_index)
      *out_index = index;
    return record_get_by_index(record_obj, (unsigned int)index);
  }

  return NOT_FOUND;
}

static INLINE ID eval_map_lookup_vec(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx, ID map) {
  unsigned int argc = args ? vector_count(args) : 0;
  if (argc != 1) {
    throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                              "Wrong number of args (%u) passed to: clojure.lang.PersistentArrayMap", argc);
    return NULL;
  }

  ID key_expr = vector_nth(args, 0);
  ID key = eval_arg_from_expr_with_context(key_expr, env, st, ctx);
  if (!key)
    return NULL;

  ID result = map_like_get_sentinel_eval(map, key, NULL);
  return AUTORELEASE(RETAIN(result));
}

static INLINE ID eval_function_call_from_vector(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, ID op,
                                                ID call_form, const EvalContext *ctx) {
  if (!op)
    return NULL;

  unsigned char op_tag = TAG(op);
  if (op_tag == CLJ_FUNC || op_tag == CLJ_CLOSURE) {
    ID result = call_function_with_args_and_context_vec(op, args, env, st, ctx);
    if (result == SYM_NIL)
      return NULL;
    return result;
  }

  if (is_keyword(op)) {
    unsigned int argc = args ? vector_count(args) : 0;
    if (argc < 1 || argc > 2) {
      const char *kw_name = "keyword";
      if (is_symbol(op)) {
        CljSymbol *s = as_symbol(op);
        if (s && s->cname)
          kw_name = s->cname;
      }
      throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                "Wrong number of args (%u) passed to: %s", argc, kw_name);
      return NULL;
    }

    ID target_expr = vector_nth(args, 0);
    ID target = NULL;
    if (target_expr && !IS_IMMEDIATE(target_expr) && TAG(target_expr) == CLJ_SLOT_REF && ctx && ctx->frame) {
      const CljSlotRef *target_ref = (const CljSlotRef *)target_expr;
      ID slot_value = frame_get_slot(ctx->frame, target_ref->depth, target_ref->slot);
      if (slot_value != NOT_FOUND) {
        target = slot_value;
      }
    } else {
      target = eval_arg_from_expr_with_context(target_expr, env, st, ctx);
    }
    ID default_val = NULL;
    if (argc == 2) {
      default_val = eval_arg_from_expr_with_context(vector_nth(args, 1), env, st, ctx);
    }

    if (!target || !is_map_like_eval(target)) {
      return keyword_lookup_default_result(default_val);
    }

    ID found = NOT_FOUND;
    CljCallsiteCache *lookup_cache = callsite_cache_for_keyword_lookup(call_form, as_symbol(op));
    uint8_t hint_index = lookup_cache ? lookup_cache->lookup_hint_index : UINT8_MAX;
    int resolved_index = -1;

    unsigned char target_tag = TAG(target);
    if (target_tag == CLJ_RECORD) {
      found = record_get_with_lookup_hint(target, op, hint_index, &resolved_index);
    } else if (target_tag == CLJ_MAP_PERSISTENT || target_tag == CLJ_MAP_TRANSIENT) {
      found = map_get_with_lookup_hint(target, op, hint_index, &resolved_index);
    } else {
      found = map_like_get_sentinel_eval(target, op, NOT_FOUND);
    }

    callsite_cache_store_lookup_hint(lookup_cache, resolved_index);
    if (found != NOT_FOUND)
      RETAIN(found);

    if (found == NOT_FOUND) {
      return keyword_lookup_default_result(default_val);
    }

    return AUTORELEASE(found);
  }

  if (op_tag == CLJ_SYMBOL) {
    CljObject *fn = eval_symbol(as_symbol(op), st);
    if (!fn) {
      if (op == SYM_NIL) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                  "Cannot call nil as a function");
        return NULL;
      }
      return NULL;
    }
    if ((ID)fn == (ID)SYM_NIL) {
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "Cannot call nil as a function");
      return NULL;
    }

    unsigned char fn_tag = TAG(fn);
    if (fn_tag == CLJ_MAP_PERSISTENT || fn_tag == CLJ_MAP_TRANSIENT || fn_tag == CLJ_RECORD) {
      ID r = eval_map_lookup_vec(args, env, st, ctx, fn);
      return r;
    }

    if (fn_tag == CLJ_FUNC || fn_tag == CLJ_CLOSURE) {
      if (g_eval_arg_depth >= MAX_CALL_STACK_DEPTH) {
        throw_exception(EXCEPTION_STACK_OVERFLOW,
                        "Maximum evaluation depth exceeded in nested function calls",
                        __FILE__, __LINE__, 0);
        return NULL;
      }
      g_eval_arg_depth++;
      ID result = call_function_with_args_and_context_vec(fn, args, env, st, ctx);
      g_eval_arg_depth--;
      if (result == SYM_NIL)
        return NULL;
      return result;
    }

    if (fn_tag == CLJ_LIST) {
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "Cannot call list as a function");
      return NULL;
    }

    if (fn_tag == CLJ_VECTOR_PERSISTENT || fn_tag == CLJ_VECTOR_TRANSIENT) {
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "Cannot call Vector as a function");
      return NULL;
    }

    if (fn_tag == CLJ_SYMBOL) {
      CljSymbol *sym = as_symbol(fn);
      if (sym == SYM_UNQUOTE_SPLICE) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                  "unquote-splice can only be used inside quasiquote");
        return NULL;
      }
      if (is_builtin_function(sym)) {
        BuiltinFn native_func = native_function_lookup(sym);
        if (native_func) {
          ID argv[16];
          unsigned int argc = 0;
          CljPersistentMap *eval_env = is_map(env) ? env : eval_env_or_ns_mappings(env, st);
          unsigned int count = args ? vector_count(args) : 0;
          unsigned int limit = (count < 16) ? count : 16;
          for (unsigned int i = 0; i < limit; i++) {
            ID arg_expr = vector_nth(args, i);
            argv[argc++] = eval_arg_from_expr_with_context(arg_expr, eval_env, st, ctx);
          }
          ID result = native_func(argv, argc);
          /* Builtins return pool-safe refs (already autoreleased); do not AUTORELEASE again. */
          return result;
        }
      }
      const char *sym_name = sym && sym->cname ? sym->cname : "unknown";
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "Cannot call %s as a function", sym_name);
      return NULL;
    }

    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "Cannot call object of type %d as a function", fn_tag);
    return NULL;
  }

  return NULL;
}

static INLINE ID call_function_with_args_and_context_vec(ID fn, CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  ID argv[16];
  unsigned int argc = 0;
  unsigned char fn_tag = TAG(fn);
  CljPersistentMap *eval_env = is_map(env) ? env : eval_env_or_ns_mappings(env, st);

  unsigned int count = args ? vector_count(args) : 0;
  unsigned int limit = (count < 16) ? count : 16;
  for (unsigned int i = 0; i < limit; i++) {
    ID arg_expr = vector_nth(args, i);
    argv[i] = eval_arg_from_expr_with_context(arg_expr, eval_env, st, ctx);
  }
  argc = limit;

  CljNamespace *saved_ns = st ? st->current_ns : NULL;
  CljNamespace *target_ns = NULL;
  if (fn_tag == CLJ_CLOSURE) {
    CljFunction *closure_fn = (CljFunction *)fn;
    target_ns = closure_fn->ns;
  }

  bool switched_ns = false;
  if (st && target_ns && st->current_ns != target_ns) {
    st->current_ns = target_ns;
    switched_ns = true;
  }

  ID result = eval_function_call(fn, argv, argc, env, st);

  if (switched_ns) {
    st->current_ns = saved_ns;
  }

  if (result == SYM_NIL) {
    return NULL;
  }
  /* HACK: Compensate for a retain-counting bug: exceptions can reach this path with rc=0
   * (e.g. when returned from macro expansion / canonicalize eval). RETAIN once so
   * caller does not release an already-dead object. TODO: Fix at source and remove. */
  if (!IS_IMMEDIATE(result) && TAG(result) == CLJ_EXCEPTION)
    RETAIN(result);
  /* Eval path returns autoreleased refs (MEMORY_POLICY). Do not AUTORELEASE again. */
  return result;
}

static ID eval_ast_call(CljASTCall *call, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  int next_eval_ast_call_depth = g_eval_ast_call_depth + 1;
  // Stack-based depth guard: measure actual C stack usage to prevent stack overflow.
  // More reliable than a counter: fires based on real stack consumption, not call count.
  // Works correctly after longjmp (stack unwinds, measurement auto-corrects).
  // Performance: in hot recursive code (fib), checking on every call is expensive.
  // We check every N frames once recursion gets beyond a small depth; overshoot is bounded.
  if (next_eval_ast_call_depth >= EVAL_STACK_CHECK_START_DEPTH &&
      (next_eval_ast_call_depth & EVAL_STACK_CHECK_INTERVAL_MASK) == 0) {
    char stack_marker;
    if (s_eval_stack_base != 0) {
      uintptr_t cur = (uintptr_t)(void *)&stack_marker;
      ptrdiff_t used = (ptrdiff_t)((cur >= s_eval_stack_base)
                                       ? (cur - s_eval_stack_base)
                                       : (s_eval_stack_base - cur));
      if (used < 0)
        used = -used; // handle stack growth direction

#ifdef DEBUG
      if (used > s_eval_stack_peak) {
        s_eval_stack_peak = used;
      }
#endif
      if (used > EVAL_STACK_LIMIT) {
        throw_exception(EXCEPTION_STACK_OVERFLOW,
                        "Maximum eval stack depth exceeded",
                        __FILE__, __LINE__, 0);
        return NULL; // unreachable (longjmp)
      }
    }
  }
  g_eval_ast_call_depth = next_eval_ast_call_depth;

tail_restart: // Target for tail-call optimization (if/when branch → restart without new frame)

  if (!call) {
    g_eval_ast_call_depth--;
    return NULL;
  }

  EvalState *effective_st = ctx ? get_eval_state(ctx, st) : st;
  if (!effective_st) {
    effective_st = builtin_get_eval_state();
  }

  CljPersistentMap *effective_env = ctx ? get_closure_env(ctx) : NULL;
  if (!effective_env)
    effective_env = env;
  effective_env = eval_env_or_ns_mappings(effective_env, effective_st);

  ID op = call->op;
  if (!op) {
    g_eval_ast_call_depth--;
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "Cannot call nil as a function");
    return NULL;
  }
  unsigned char op_tag = TAG(op);

  CljPersistentVector *args = call->args;

  if (op_tag == CLJ_SYMBOL) {
    CljSymbol *original_op_sym = as_symbol(op);
    if (original_op_sym) {
      ID result = NULL;
      bool handled = false;

      // --- Tail-call optimized: inline if ---
      // Saves ~500 bytes of stack per if-level by avoiding
      // eval_special_if → eval_body → eval_body_with_params → eval_ast_call recursion.
      if (original_op_sym == SYM_IF) {
        unsigned int argc = args ? vector_count(args) : 0;
        if (argc >= 2) {
          ID cond_expr = vector_nth(args, 0);
          ID cond_val = eval_arg_from_expr_with_context(cond_expr, effective_env, effective_st, ctx);
          bool truthy = clj_is_truthy(cond_val);
          ID branch = truthy ? vector_nth(args, 1) : (argc >= 3 ? vector_nth(args, 2) : NULL);
          if (!branch) {
            g_eval_ast_call_depth--;
            return NULL;
          }
          // If branch is an AST call, restart eval_ast_call without new stack frame
          if (TAG(branch) == CLJ_AST_CALL) {
            call = as_ast_call(branch);
            goto tail_restart;
          }
          // Otherwise evaluate the branch expression
          result = ctx ? eval_body_with_params(branch, ctx)
                       : eval_body(branch, effective_env, effective_st, NULL);
          g_eval_ast_call_depth--;
          return result;
        }
        g_eval_ast_call_depth--;
        return NULL;
      }

      // --- Tail-call optimized: inline when ---
      if (original_op_sym == SYM_WHEN) {
        unsigned int argc = args ? vector_count(args) : 0;
        ID cond_expr = (argc >= 1) ? vector_nth(args, 0) : NULL;
        ID cond_val = eval_arg_from_expr_with_context(cond_expr, effective_env, effective_st, ctx);
        bool truthy = cond_val ? clj_is_truthy(cond_val) : false;
        if (!truthy) {
          g_eval_ast_call_depth--;
          return NULL;
        }
        // Evaluate all body forms except the last for side effects
        for (unsigned int i = 1; i + 1 < argc; i++) {
          ID body_expr = vector_nth(args, i);
          if (!body_expr)
            continue;
          ID r = ctx ? eval_body_with_params(body_expr, ctx)
                     : eval_body(body_expr, effective_env, effective_st, NULL);
          (void)r;
        }
        // Last body form: tail-call optimize
        if (argc >= 2) {
          ID last_expr = vector_nth(args, argc - 1);
          if (!last_expr) {
            g_eval_ast_call_depth--;
            return NULL;
          }
          if (TAG(last_expr) == CLJ_AST_CALL) {
            call = as_ast_call(last_expr);
            goto tail_restart;
          }
          result = ctx ? eval_body_with_params(last_expr, ctx)
                       : eval_body(last_expr, effective_env, effective_st, NULL);
          g_eval_ast_call_depth--;
          return result;
        }
        g_eval_ast_call_depth--;
        return NULL;
      }

      if (original_op_sym == SYM_DEF) {
#if defined(META_ENABLED) && META_ENABLED
        // Preserve def form metadata by attaching it to the args vector.
        if (call && call->args) {
          ID form_meta = meta_get((ID)call);
          if (form_meta && !meta_get((ID)call->args)) {
            meta_set((CljObject *)call->args, form_meta);
          }
        }
#endif
        result = eval_def(args, effective_env, effective_st);
        handled = true;
      } else if (original_op_sym == SYM_DEFMACRO) {
        result = eval_special_defmacro(args, effective_env, effective_st, ctx);
        handled = true;
      } else if (original_op_sym == SYM_NS) {
        result = eval_ns(args, effective_env, effective_st);
        handled = true;
      } else if (original_op_sym == SYM_DOSEQ) {
        result = eval_doseq(args, effective_env, effective_st, ctx);
        handled = true;
      } else if (original_op_sym == SYM_DOTIMES) {
        // OPTIMIZATION: Use thread-local EvalState instead of creating temporary
        EvalState *eval_st = effective_st ? effective_st : builtin_get_eval_state();
        result = eval_dotimes(args, effective_env, eval_st, ctx);
        handled = true;
      } else if (is_special_symbol(original_op_sym)) {
        CljSpecialSymbol *special = (CljSpecialSymbol *)original_op_sym;
        if (special->eval_fn) {
          SpecialFormEvalFn fn = (SpecialFormEvalFn)special->eval_fn;
          result = fn(args, effective_env, effective_st, ctx);
          handled = true;
        }
      }

      if (handled) {
        /* MEMORY_POLICY: eval returns autoreleased so callers need not release. */
        g_eval_ast_call_depth--;
        return result;
      }
    }
  }

  if (is_list_type(op_tag) || op_tag == CLJ_AST_CALL) {
    op = eval_body(op, effective_env, effective_st, ctx);
    if (!op) {
      g_eval_ast_call_depth--;
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "Cannot call nil as a function");
      return NULL;
    }
    if (IS_IMMEDIATE(op)) {
      g_eval_ast_call_depth--;
      const char *type_name = is_fixed((CljValue)op) ? "number" : (is_bool((CljValue)op) ? "boolean" : "immediate value");
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "Cannot call %s as a function (this may indicate a macro expansion error)", type_name);
      return NULL;
    }
    op_tag = TAG(op);
  }

  if (op_tag == CLJ_MAP_PERSISTENT || op_tag == CLJ_MAP_TRANSIENT || op_tag == CLJ_RECORD) {
    g_eval_ast_call_depth--;
    ID r = eval_map_lookup_vec(call->args, effective_env, effective_st, ctx, op);
    return r;
  }

  if (op_tag == CLJ_VECTOR_PERSISTENT || op_tag == CLJ_VECTOR_TRANSIENT) {
    g_eval_ast_call_depth--;
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "Cannot call Vector as a function");
    return NULL;
  }

  unsigned char original_op_tag = op_tag;
  CljSymbol *original_op_sym = (original_op_tag == CLJ_SYMBOL) ? as_symbol(op) : NULL;

  if (call && original_op_sym && g_runtime.resolve_cache_epoch != 0) {
    ID cached_fn = ast_call_get_cached_resolution_fast(call, original_op_sym, g_runtime.resolve_cache_epoch);
    if (cached_fn) {
      unsigned char cached_tag = TAG(cached_fn);
      if (cached_tag == CLJ_FUNC || cached_tag == CLJ_CLOSURE) {
        g_eval_ast_call_depth--;
        ID cached_result = call_function_with_args_and_context_vec(cached_fn, call->args, effective_env, effective_st, ctx);
        // Keep ownership identical to the non-cached function-call path.
        // call_function_with_args_and_context_vec already returns pool-safe
        // refs for eval results, so adding AUTORELEASE here can double-enqueue
        // objects (notably seq wrappers from native next/rest).
        return cached_result;
      }
    }
  }

  ID resolved_op = resolve_list_operator(op, effective_env, effective_st, ctx, (CljObject *)call, true);
  op = resolved_op;

  op_tag = op ? TAG(op) : 0;
  if (op && is_map_like_eval(op)) {
    g_eval_ast_call_depth--;
    ID r = eval_map_lookup_vec(call->args, effective_env, effective_st, ctx, op);
    return r;
  }

  if (op && (op_tag == CLJ_SYMBOL || op_tag == CLJ_FUNC || op_tag == CLJ_CLOSURE)) {
    g_eval_ast_call_depth--;
    return eval_function_call_from_vector(call->args, effective_env, effective_st, op, (ID)call, ctx);
  }

  g_eval_ast_call_depth--;
  if (IS_IMMEDIATE(op)) {
    const char *type_name = is_fixed((CljValue)op) ? "number" : (is_bool((CljValue)op) ? "boolean" : "immediate value");
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "Cannot call %s as a function (this may indicate a macro expansion error)", type_name);
    return NULL;
  }

  if (is_list_type(op_tag) || op_tag == CLJ_AST_CALL) {
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "Cannot call list as a function");
    return NULL;
  }

  if (!op) {
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "Cannot call nil as a function");
    return NULL;
  }

  throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                            "Cannot call %s as a function", clj_type_name(((CljObject *)op)->type));
  return NULL;
}

ID eval_def(CljPersistentVector *args, CljPersistentMap *env, EvalState *st) {
  CLJ_ASSERT(env != NULL);

  unsigned int argc = args ? vector_count(args) : 0;

  // Get the symbol name (first argument) - don't evaluate it, just get the symbol
  CljObject *symbol = (argc >= 1) ? (CljObject *)vector_nth(args, 0) : NULL;
  if (!symbol || TAG(symbol) != CLJ_SYMBOL) {
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "def requires a symbol as first argument",
                    NULL, 0, 0);
    return NULL;
  }

  // Get the value (second argument) - evaluate this
  // Note: value_expr can be NULL if nil was parsed (nil is represented as NULL)
  ID value_expr = (argc >= 2) ? vector_nth(args, 1) : NULL;
  if (argc < 2) {
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                    "def requires a value expression as second argument",
                    NULL, 0, 0);
    return NULL;
  }
  // If argc >= 2 but value_expr is NULL, it's nil (valid case)

  // Evaluate the value expression
  // Use current namespace mappings as environment for evaluation
  // This ensures that builtin functions like + are available during evaluation
  CljPersistentMap *eval_env = (st && st->current_ns) ? st->current_ns->mappings : env;
  ID value = NULL;
  if (value_expr) {
    value = eval_body(value_expr, eval_env, st, NULL);
  }
  // If value_expr is NULL, value remains NULL (nil case)
  // value can be NULL if nil was evaluated (legitimate case)
  // If evaluation failed, eval/eval_parsed should have thrown an exception

  // Store the symbol-value binding in the environment
  if (!st) {
    throw_exception(EXCEPTION_RUNTIME,
                    "def requires an evaluation state",
                    NULL, 0, 0);
    return NULL;
  }

  if (!st->current_ns) {
    throw_exception(EXCEPTION_RUNTIME,
                    "def requires a current namespace",
                    NULL, 0, 0);
    return NULL;
  }

  // Resolve symbol once for reuse
  CljSymbol *sym = as_symbol(symbol);
  CljFunction *closure_func = NULL;
  // If the value is a function, set its name and rewrite recursive calls
  // CRITICAL: Only for CLJ_CLOSURE (Clojure functions), not CLJ_FUNC (native functions)
  if (is_closure(value)) {
    CljFunction *func = as_function(value);
    if (func) {
      closure_func = func;
      // Ensure closure carries its defining namespace for later resolution.
      if (!func->ns && st && st->current_ns) {
        func->ns = (CljNamespace *)RETAIN(st->current_ns);
      }
      if (sym && sym->cname[0]) {
        if (!func->name_sym)
          func->name_sym = sym;

        // Rewrite recursive calls to use qualified name (for TCO optimization)
        // Only if namespace is available (avoids unnecessary work)
        if (st->current_ns && st->current_ns->name) {
          CljSymbol *qualified = intern_symbol(st->current_ns->name, sym->cname);
          if (qualified && qualified != sym) {
            rewrite_recursive_calls_in_slot((ID *)&func->body, sym, qualified);
          }
        }
      }
    }
  }

  // Store in namespace (value can be NULL/nil - legitimate case)
  ns_define(st->current_ns, symbol, value);

  // For (def f (fn f ...)) / defn-style definitions, drop the extra closure
  // self-binding frame after namespace binding is established.
  if (closure_func) {
    drop_def_self_binding_frame(closure_func, sym, value);
  }

  // Apply metadata to value
  // In Clojure, metadata from ^#^{...} (def ...) is applied to the value
#if defined(META_ENABLED) && META_ENABLED
  if (value) {
    unsigned char value_tag = TAG(value);
    ID form_meta = meta_get((ID)args);

    // Optimized: Only process metadata for functions (most common case)
    // For non-functions, just copy form metadata if present
    if (value_tag == CLJ_CLOSURE || value_tag == CLJ_FUNC) {
      if (is_map(form_meta)) {
        CljPersistentMap *meta_map = (CljPersistentMap *)RETAIN(form_meta);
        // Add :name and :ns directly (map_assoc overwrites if present, :name/:ns are rare)
        if (SYM_KW_NAME && sym && sym->cname && sym->cname[0] != '\0') {
          CljString *name_str = make_string(sym->cname);
          if (name_str) {
            ASSIGN(meta_map, map_assoc(meta_map, SYM_KW_NAME, name_str));
            RELEASE(name_str);
          }
        }
        if (SYM_KW_NS && st->current_ns && st->current_ns->name) {
          ASSIGN(meta_map, map_assoc(meta_map, SYM_KW_NS, st->current_ns->name));
        }
        meta_set(value, meta_map);
        RELEASE(meta_map);
      }
    } else if (form_meta) {
      meta_set(value, form_meta);
    }
  }
#endif // META_ENABLED

  // Return the symbol key that was actually stored in the namespace mappings.
  // For non-core namespaces, ns_define qualifies & interns unqualified symbols
  // (e.g. user/test-fn). Returning that canonical key keeps (def ...) consistent
  // with direct map_get-based lookups in tests and tooling.
  CljSymbol *ret_sym = sym;
  if (st->current_ns && sym && sym->cname) {
    if (st->current_ns->name == SYM_CLOJURE_CORE) {
      ret_sym = intern_symbol_global(sym->cname);
    } else if (sym->ns_name && st->current_ns->name && sym->ns_name == st->current_ns->name) {
      ret_sym = sym;
    } else if (sym->ns_name && sym->ns_name->cname) {
      ret_sym = sym;
    } else if (st->current_ns->name && st->current_ns->name->cname) {
      ret_sym = intern_symbol(st->current_ns->name, sym->cname);
    }
  }
  return ret_sym;
}

ID eval_ns(CljPersistentVector *args, CljPersistentMap *env, EvalState *st) {
  CLJ_ASSERT(env != NULL);
  (void)env;
  assert(st != NULL);

  // Get namespace name (first argument) - use list_get_element like eval_def
  unsigned int argc = args ? vector_count(args) : 0;
  CljObject *ns_name_obj = (argc >= 1) ? (CljObject *)vector_nth(args, 0) : NULL;
  if (!ns_name_obj || TAG(ns_name_obj) != CLJ_SYMBOL) {
    eval_error("ns expects a symbol", st);
    return NULL;
  }

  CljSymbol *ns_sym = as_symbol(ns_name_obj);
  if (!ns_sym || !ns_sym->cname[0]) {
    eval_error("ns symbol has no name", st);
    return NULL;
  }

  // Switch to namespace (creates if not exists)
  evalstate_set_ns(st, ns_sym->cname);

  // Process :require clauses: (ns name (:require [ns :as alias]))
  // Avoid list_nth in a loop (linked lists would make this O(n^2)).
  bool require_needs_eval_state = builtin_native_fn_needs_eval_state(native_require);
  for (unsigned int i = 1; i < argc; i++) {
    CljObject *clause = (CljObject *)vector_nth(args, i);
    if (!clause)
      continue;

    CljSymbol *clause_sym = NULL;
    CljPersistentVector *specs_vec = NULL;
    CljASTCall *call = NULL;

    CljType clause_tag = TAG(clause);
    if (clause_tag == CLJ_AST_CALL) {
      call = as_ast_call(clause);
    } else if (is_list_type(clause_tag)) {
      ID canonical_clause = canonicalize_ast(clause, st);
      if (canonical_clause && TAG(canonical_clause) == CLJ_AST_CALL) {
        call = as_ast_call(canonical_clause);
      }
    } else {
      continue;
    }

    if (!call || !call->op || TAG(call->op) != CLJ_SYMBOL) {
      continue;
    }

    clause_sym = as_symbol(call->op);
    specs_vec = call->args;

    if (!clause_sym || !clause_sym->cname) {
      continue;
    }

    // Check if this is a :require clause
    if (clause_sym->cname[0] == ':' && strcmp(clause_sym->cname, ":require") == 0) {
      // Process require specs: (:require [ns :as alias] [ns2 :as alias2])
      if (specs_vec) {
        unsigned int spec_count = vector_count(specs_vec);
        for (unsigned int j = 0; j < spec_count; j++) {
          ID spec = vector_nth(specs_vec, j);
          if (!spec)
            continue;
          extern void builtin_set_eval_state(EvalState * st);
          if (require_needs_eval_state)
            builtin_set_eval_state(st);
          ID spec_args[1] = {spec};
          (void)native_require(spec_args, 1);
          if (require_needs_eval_state)
            builtin_set_eval_state(NULL); // Clear after call
        }
      } else {
        CljList *spec_node = list_or_null(as_list(LIST_REST(as_list(clause))));
        for (CljList *s = spec_node; s; s = list_or_null(as_list(LIST_REST(s)))) {
          CljObject *spec = LIST_FIRST(s);
          if (!spec)
            continue;
          extern void builtin_set_eval_state(EvalState * st);
          if (require_needs_eval_state)
            builtin_set_eval_state(st);
          ID spec_id = spec;
          ID spec_args[1] = {spec_id};
          (void)native_require(spec_args, 1);
          if (require_needs_eval_state)
            builtin_set_eval_state(NULL); // Clear after call
        }
      }
    }
  }

  return NULL;
}

ID eval_var(CljPersistentVector *args, CljPersistentMap *env, EvalState *st) {
  (void)env; // Not used

  // Get symbol name (first argument)
  unsigned int argc = args ? vector_count(args) : 0;
  CljObject *sym_obj = (argc >= 1) ? (CljObject *)vector_nth(args, 0) : NULL;
  if (!sym_obj || TAG(sym_obj) != CLJ_SYMBOL) {
    eval_error("var expects a symbol", st);
    return NULL;
  }

  CljSymbol *sym = as_symbol(sym_obj);
  if (!sym || !sym->cname[0]) {
    eval_error("var symbol has no name", st);
    return NULL;
  }

  // Look up the symbol in the current namespace
  ID value = ns_resolve(st, sym);
  if (value == NOT_FOUND) {
    // Try to find the symbol in the current namespace mappings
    CljPersistentMap *mappings = st->current_ns->mappings;
    if (mappings) {
      value = map_get(mappings, sym_obj);
    }
  }

  if (value == NOT_FOUND) {
    eval_error("var: symbol not found", st);
    return NULL;
  }

  // Normalize SYM_NIL to runtime nil (NULL)
  if (value == SYM_NIL) {
    return NULL;
  }

  // Return the value (in Clojure, var returns the actual value, not a var object)
  return value;
}

// ============================================================================
// AST Transformation Functions for TCO
// ============================================================================
// TCO functions moved to optimize.c

ID eval_fn(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  // Some call paths (notably during bootstrap / lazy builder thunks) may not have
  // an explicit EvalState or lexical env map available.
  if (!st)
    st = builtin_get_eval_state();
  if (!st)
    st = get_global_eval_state();
  if (!env && st && st->current_ns) {
    env = st->current_ns->mappings;
  }
  if (!env) {
    // Final fallback: empty environment.
    env = map_empty();
  }
  unsigned int argc = args ? vector_count(args) : 0;

  // Get potential function name (for named fn like (fn step [x] ...))
  ID second = (argc >= 1) ? vector_nth(args, 0) : NULL; // name oder params
  CljSymbol *fn_name = NULL;
  ID params_list = NULL;
  ID body = NULL;
  unsigned int body_start = 0;

  // Check if second element is a symbol (named fn) but NOT a keyword
  // Also check it's not a vector (anonymous fn case)
  if (is_symbol(second) && !IS_KEYWORD(second)) {
    // Named fn: (fn name [params] body...)
    fn_name = (CljSymbol *)second;
    params_list = (argc >= 2) ? vector_nth(args, 1) : NULL;
    body_start = 2;
  } else {
    // Anonymous fn: (fn [params] body...)
    params_list = second;
    body_start = 1;
  }

  if (body_start >= argc) {
    return NULL;
  }

  body = vector_nth(args, body_start);

  // Handle multiple body expressions by creating canonical (do ...) AST call.
  bool body_is_multi = ((argc - body_start) > 1);
  bool body_owned = false;
  if (body_is_multi) {
    unsigned int body_count = argc - body_start;
    CljPersistentVector *do_args = make_vector((int)body_count, STRONG);
    for (unsigned int i = body_start; i < argc; i++) {
      vector_conj_inplace(&do_args, vector_nth(args, i));
    }
    CljASTCall *do_call = make_ast_call((ID)SYM_DO, do_args);
    RELEASE(do_args);
    body = (ID)do_call;
    body_owned = true;
  }

  // Multi-arity fn/defn syntax is not supported yet:
  //   (fn ([x] ...) ([x y] ...))
  //   (defn f ([] ...) ([x] ...))
  // In those forms, params_list is itself an arity clause; its first element
  // is another parameter declaration (vector/list/seq).
  ID first_param = NULL;
  if (params_list && is_list_type(TAG(params_list))) {
    CljList *param_clause = as_list(params_list);
    first_param = (param_clause != NULL) ? LIST_FIRST(param_clause) : NULL;
  } else if (params_list && is_seq(params_list)) {
    first_param = seq_first(params_list);
  } else if (params_list && TAG(params_list) == CLJ_AST_CALL) {
    // Canonicalized multi-arity clause like ([] 0) arrives as AST call with
    // vector/list op. Handle this explicitly so defn/fn throws instead of
    // silently evaluating to nil.
    CljASTCall *clause = as_ast_call(params_list);
    first_param = clause ? clause->op : NULL;
  }
  if (first_param &&
      (is_vector(first_param) || is_list_type(TAG(first_param)) || is_seq(first_param))) {
    throw_exception(EXCEPTION_RUNTIME,
                    "Multi-arity fn/defn is not implemented yet",
                    __FILE__, __LINE__, 0);
    return NULL;
  }

  // Parameters can be a vector [a b] or a list (a b)
  bool is_vector_params = is_vector(params_list);
  if (!params_list || (!is_list_type(TAG(params_list)) && !is_vector_params)) {
    return NULL;
  }

  if (!body) {
    return NULL;
  }

  // Check if body is :native marker (for native function stubs)
  // Keywords are interned, so pointer comparison suffices
  if (body == (CljObject *)SYM_KW_NATIVE) {
    // Named fn required for :native lookup
    if (!fn_name || !fn_name->cname) {
      throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                      "fn with :native requires a function name",
                      NULL, 0, 0);
      return NULL;
    }

    // Lookup native function by name (try qualified first, then unqualified)
    BuiltinFn native_func = NULL;
    CljSymbol *lookup_symbol = fn_name;
    if (st && st->current_ns && st->current_ns->name) {
      CljSymbol *qualified = intern_symbol(st->current_ns->name, fn_name->cname);
      if (qualified)
        lookup_symbol = qualified;
    }
    native_func = native_function_lookup(lookup_symbol);
    if (!native_func && lookup_symbol != fn_name) {
      native_func = native_function_lookup(fn_name);
    }
    if (!native_func) {
      char error_msg[256];
      size_t pos = 0;
      pos = format_append(error_msg, pos, sizeof(error_msg),
                          "Native function not found for: ");
      format_append(error_msg, pos, sizeof(error_msg), fn_name->cname);
      throw_exception(EXCEPTION_RUNTIME, error_msg, NULL, 0, 0);
    }

    // Create and return native function object
    uint8_t native_flags = builtin_native_fn_needs_eval_state(native_func) ? CLJ_CFUNC_FLAG_NEEDS_EVAL_STATE : 0u;
    return AUTORELEASE(make_named_func_with_flags(native_func, fn_name, native_flags));
  }

  // Convert parameter list/vector to array
  // NOTE: Destructuring is handled at AST canonicalization time, so params are always symbols here
  int param_count = is_vector_params
                        ? vector_count(as_vector(params_list))
                        : list_count(as_list(params_list));

  ID params_stack[16];
  ID *params = alloc_obj_array(param_count, params_stack);

  if (is_vector_params) {
    for (int i = 0; i < param_count; i++) {
      params[i] = vector_nth(as_vector(params_list), i);
      if (!params[i] || TAG(params[i]) != CLJ_SYMBOL) {
        free_obj_array(params, params_stack);
        return NULL;
      }
    }
  } else {
    int i = 0;
    CljList *p = as_list(params_list);
    while (p && i < param_count) {
      params[i] = LIST_FIRST(p);
      if (!params[i] || TAG(params[i]) != CLJ_SYMBOL) {
        free_obj_array(params, params_stack);
        return NULL;
      }
      i++;

      CljObject *rest = LIST_REST(p);
      if (!rest || !is_list_type(TAG(rest))) {
        break;
      }
      p = as_list(rest);
    }

    if (i != param_count) {
      free_obj_array(params, params_stack);
      return NULL;
    }
  }

  // Optimize named function bodies via one optimizer walk.
  if (fn_name && body) {
    ID orig_body = body;
    ID transformed = optimize_function_body_walk(body, (ID)fn_name,
                                                 (CljObject **)params, param_count, body);
    if (transformed) {
      if (transformed == orig_body) {
        // optimize_function_body_walk retains; drop the extra retain.
        RELEASE(transformed);
      } else {
        if (body_owned) {
          RELEASE(orig_body);
        }
        body = transformed;
        body_owned = true;
      }
    }
  }

  // Capture env_stack from context if available (vector of maps).
  CljPersistentVector *fn_env_stack = NULL;
  bool fn_env_stack_owned = false;
  if (ctx && ctx->env_stack) {
    fn_env_stack = ctx->env_stack;
  } else {
    // Fallback: capture env only when it's not the current namespace mappings.
    // Avoid capturing namespace mappings to prevent cycles (fn -> env_stack -> ns map -> fn).
    CljPersistentMap *env_source = eval_env_or_ns_mappings(env, st);
    bool is_current_ns_env = st && st->current_ns && st->current_ns->mappings == env_source;
    if (env_source && !is_current_ns_env) {
      fn_env_stack = NULL;
      fn_env_stack_owned = true;
      env_stack_push_inplace(&fn_env_stack, env_source);
    }
  }

  // If we are inside a function call with a CallFrame, eagerly capture parameters into a map.
  // This ensures closures can reference parameters after the call returns.
  // The captured param-map must be below any let frames so let-bindings still shadow params.
  // Capture when any frame in the chain has params (closure inside a let must see outer defn params).
  // Single loop: first phase walks chain (collect frames + total), second phase fills map (outer first).
  if (ctx && ctx->frame) {
    CallFrame *frames[32];
    int depth = 0, di = 0;
    unsigned int total = 0;
    CljPersistentMap *param_map = NULL;
    CallFrame *f = ctx->frame;
    for (;;) {
      if (f && depth < 32) {
        frames[depth++] = f;
        if (f->param_count > 0)
          total += (unsigned int)f->param_count;
        f = f->parent;
      } else if (!param_map) {
        if (total == 0)
          break;
        param_map = make_map((int)total);
        di = depth - 1;
      } else if (di >= 0) {
        CallFrame *fr = frames[di--];
        for (int i = 0; i < fr->param_count; i++)
          map_assoc_inplace(&param_map, fr->params[i], frame_decode_value(fr->values[i]));
      } else {
        unsigned int base_cnt = vector_count(fn_env_stack);
        CljPersistentVector *combined = NULL;
        env_stack_push_inplace(&combined, param_map);
        RELEASE(param_map);
        for (unsigned int i = 0; i < base_cnt; i++)
          vector_conj_inplace(&combined, vector_nth(fn_env_stack, i));
        if (fn_env_stack_owned)
          RELEASE(fn_env_stack);
        fn_env_stack = combined;
        fn_env_stack_owned = true;
        break;
      }
    }
  }

  // Nested closures (e.g. lazy-seq thunks) cannot use EvalContext.current_fn from the
  // creator call after the creator returns. Capture the creator's named self symbol as a
  // lexical env binding so local recursive named fns remain resolvable inside nested closures.
  if (ctx && is_closure(ctx->current_fn)) {
    CljFunction *creator_fn = as_function(ctx->current_fn);
    if (creator_fn && creator_fn->name_sym) {
      CljPersistentMap *self_bind = map_empty();
      map_assoc_inplace(&self_bind, (ID)creator_fn->name_sym, ctx->current_fn);

      unsigned int base_cnt = vector_count(fn_env_stack);
      CljPersistentVector *combined = NULL;
      env_stack_push_inplace(&combined, self_bind);
      RELEASE(self_bind);
      for (unsigned int i = 0; i < base_cnt; i++) {
        vector_conj_inplace(&combined, vector_nth(fn_env_stack, i));
      }
      if (fn_env_stack_owned) {
        RELEASE(fn_env_stack);
      }
      fn_env_stack = combined;
      fn_env_stack_owned = true;
    }
  }

  // Create function object
  CljFunction *fn = make_function(params, param_count, body, fn_env_stack, fn_name, st ? st->current_ns : NULL);
  if (body_owned) {
    RELEASE(body);
    body_owned = false;
  }
  if (fn_env_stack_owned) {
    RELEASE(fn_env_stack);
  }

  // Named local recursion is resolved via EvalContext.current_fn (cycle-free).

  free_obj_array(params, params_stack);

  return AUTORELEASE(fn);
}

// is_special_symbol is now in symbol.c (uses dynamic registration)
// This inline version was removed to use the centralized implementation

// Check if symbol is a builtin function (+, -, *, /, etc.)
// Uses compact array-based lookup for smaller code size
static INLINE bool is_builtin_function(CljSymbol *symbol) {
  if (!symbol)
    return false;
  return (symbol == SYM_PLUS ||
          symbol == SYM_MINUS ||
          symbol == SYM_MULTIPLY ||
          symbol == SYM_DIVIDE ||
          symbol == SYM_EQUALS ||
          symbol == SYM_LT ||
          symbol == SYM_GT ||
          symbol == SYM_LE ||
          symbol == SYM_GE ||
          symbol == SYM_PRINT ||
          symbol == SYM_PRINTLN ||
          symbol == SYM_STR ||
          symbol == SYM_NTH ||
          symbol == SYM_FIRST ||
          symbol == SYM_REST ||
          symbol == SYM_COUNT ||
          symbol == SYM_CONS ||
          symbol == SYM_NEXT ||
          symbol == SYM_DOSEQ ||
          symbol == SYM_DOTIMES);
}

ID eval_symbol(CljSymbol *symbol, EvalState *st) {
  if (!symbol) {
    return NULL;
  }

  // Special case: nil evaluates to NULL
  if (symbol == SYM_NIL) {
    return NULL;
  }

  // Keywords evaluate to themselves
  if (IS_KEYWORD(symbol)) {
    return symbol;
  }

  // Fallback: handle qualified symbols that weren't split into ns_name
  // (e.g., cname contains "clojure.core/filter")
  if (!symbol->ns_name && symbol->cname) {
    const char *slash = strchr(symbol->cname, '/');
    if (slash && slash > symbol->cname && slash[1] != '\0') {
      char ns_buf[SYMBOL_NAME_MAX_LEN];
      char sym_buf[SYMBOL_NAME_MAX_LEN];
      size_t ns_len = (size_t)(slash - symbol->cname);
      size_t sym_len = strlen(slash + 1);
      if (ns_len < sizeof(ns_buf) && sym_len < sizeof(sym_buf)) {
        memcpy(ns_buf, symbol->cname, ns_len);
        ns_buf[ns_len] = '\0';
        memcpy(sym_buf, slash + 1, sym_len);
        sym_buf[sym_len] = '\0';

        CljSymbol *ns_sym = intern_symbol_global(ns_buf);
        CljSymbol *qualified = ns_sym ? intern_symbol(ns_sym, sym_buf) : NULL;
        if (qualified) {
          ID resolved = ns_resolve(st, qualified);
          if (resolved != NOT_FOUND) {
            return resolved;
          }
        }
      }
    }
  }

  // Dynamic vars: if an earmuffed symbol is dynamically bound, return its binding.
  // Values may be NULL (nil) and are stored directly in binding maps.
  if (is_dynamic_var_symbol(symbol)) {
    ID bound = dynamic_binding_lookup(st, symbol);
    if (bound != NOT_FOUND) {
      return bound;
    }
  }

  // *ns* is represented as the current namespace object.
  // This makes it dynamically bindable by updating EvalState.current_ns in (binding ...).
  if (symbol == SYM_NS_STAR) {
    if (st && st->current_ns) {
      return (ID)st->current_ns;
    }
    return (ID)ns_get_or_create("user", NULL);
  }

  // CRITICAL: Handle qualified symbols (symbol->ns_name is set during parsing)
  // Parser already splits qualified symbols into name and namespace
  // This avoids string parsing in the hot-path
  // NOTE: Alias resolution is now done in the parser, not at runtime
  // OPTIMIZATION: For fully qualified symbols, use pointer directly (no re-interning)
  // This eliminates expensive strcmp calls in hot paths
  if (symbol->ns_name && symbol->ns_name->cname) {
    ID resolved = ns_resolve(st, symbol);
    if (resolved != NOT_FOUND) {
      return resolved;
    }

    // Qualified symbol not found in target namespace
    const char *cname = symbol->cname ? symbol->cname : "unknown";
    const char *ns_cname = symbol->ns_name && symbol->ns_name->cname ? symbol->ns_name->cname : "unknown";
    bool suggest_require = false;
    if (ns_cname && should_suggest_require_for_ns(ns_cname)) {
      suggest_require = (ns_find(ns_cname) == NULL);
    }
    throw_unresolved_symbol_exception_parts(ns_cname, cname, suggest_require);
  }

  // Check special forms first - they return themselves
  // Builtin functions need to be resolved from namespace
  if (is_special_symbol(symbol)) {
    return symbol;
  }

  // For builtin functions, resolve from namespace to get the actual function object
  // CRITICAL: All functions must be registered in the namespace (via register_builtins()
  // or via :native stubs in clojure.core.clj). No fallback to native_function_lookup
  // to avoid hiding errors where functions are used before they are defined.
  ID value = ns_resolve(st, symbol);
  if (value != NOT_FOUND) {
    // Normalize nil representations
    if (!value || value == SYM_NIL) {
      return NULL;
    }
    // ns_resolve returns a value that is safe to use in our scope
    // No need for AUTORELEASE/RETAIN - we didn't create the object
    return value;
  }

  // If not found in namespace but is a builtin, return symbol as fallback
  // (handled by the function-call dispatch path).
  if (is_builtin_function(symbol)) {
    return symbol;
  }

  // CLOJURE COMPATIBILITY: Native functions (e.g. clojure.string/trim) are only
  // accessible after explicit (require 'clojure.string). The native_function_table
  // is only used by (defn ... :native) stubs, not by symbol resolution.

  // Symbol not found in namespace - this is an error
  // Functions must be registered via register_builtins() or :native stubs
  const char *cname = symbol->cname ? symbol->cname : "unknown";
  throw_unresolved_symbol_exception_parts(NULL, cname, false);
  return NULL;
}

ID eval_seq(CljList *list, CljPersistentMap *env) {
  CLJ_ASSERT(env != NULL);
  CljObject *arg = eval_arg(list, 1, env, NULL);
  if (!arg)
    return NULL;

  // If argument is already nil, return nil
  // Note: nil is now represented as NULL, so no special nil check needed

  // Check if argument is seqable
  if (!is_seqable(arg)) {
    return NULL;
  }

  // For lists, check if empty - if so, return nil
  switch (arg->type) {
  case CLJ_LIST:
  case CLJ_AST_NODE: {
    CljList *list_data = as_list(arg);
    if (!LIST_FIRST(list_data))
      return NULL; // Empty list -> nil
    return arg;
  }

  default: {
    if (arg->type == CLJ_SEQ)
      return arg;
    CljSeqIterator *it = (CljSeqIterator *)AUTORELEASE(make_seq(arg));
    if (!it)
      return NULL;
    return (CljObject *)it;
  }
  }
}
// ============================================================================
// FOR-LOOP IMPLEMENTATIONS
// ============================================================================

/**
 * @brief Helper function to extend environment with a new binding
 * @param env The existing environment
 * @param var The variable to bind
 * @param element The value to bind to the variable
 * @return A new environment with the binding added
 */
static CljPersistentMap *extend_env_with_binding(CljPersistentMap *env, CljObject *var, CljObject *element) {
  // Estimate capacity: existing bindings + new binding
  int capacity = env ? ((CljPersistentMap *)env)->count + 1 : 4;
  CljPersistentMap *new_env = (CljPersistentMap *)make_map(capacity);
  if (new_env) {
    // Copy existing environment bindings
    if (env) {
      MAP_FOR_EACH(env, key, value) {
        CljPersistentMap *updated = map_assoc(new_env, key, value);
        ASSIGN(new_env, updated);
      }
    }
    // Add new binding (overwrites the existing value if the key already exists)
    // CRITICAL: map_assoc may return a new map (COW), so we must use the result
    CljPersistentMap *updated_env = map_assoc(new_env, var, element);
    ASSIGN(new_env, updated_env);
  }
  return new_env;
}

// NOTE: Legacy native `for`/`for*` special forms were removed.
// `for` is implemented as a Clojure macro in `clojure.core` and expands to lazy primitives.

ID eval_doseq(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  CLJ_ASSERT(env != NULL);
  // (doseq [binding coll] expr)
  // Executes expr for side effects, returns nil

  unsigned int argc = args ? vector_count(args) : 0;
  if (argc == 0) {
    return NULL;
  }

  CljObject *binding_list = (CljObject *)vector_nth(args, 0);
  CljObject *body = (argc >= 2) ? (CljObject *)vector_nth(args, 1) : NULL;
  if (!binding_list || !body) {
    return NULL;
  }
  // Parse binding: [var coll] - support both vectors and lists without allocations
  ID var = NULL;
  ID coll_expr = NULL;

  if (is_vector(binding_list)) {
    CljPersistentVector *vec = as_vector(binding_list);
    CLJ_ASSERT(vec != NULL && "vector binding must cast to non-null vector");
    if (vector_count(vec) < 2)
      return NULL;
    var = vector_nth(vec, 0);
    coll_expr = vector_nth(vec, 1);
  } else if (is_list_type(TAG(binding_list))) {
    CljList *binding_data = as_list(binding_list);
    CLJ_ASSERT(binding_data != NULL && "list binding must cast to non-null list");
    if (!binding_data->first)
      return NULL;
    var = binding_data->first;
    CljList *rest_list = as_list(binding_data->rest);
    if (!rest_list)
      return NULL;
    coll_expr = rest_list->first;
  } else {
    return NULL;
  }

  // Evaluate collection expression in current env (preserve ctx for lexical lookup)
  ID coll_eval = RETAIN(eval_body(coll_expr, env, st, ctx));
  if (!coll_eval) {
    return NULL;
  }

  ID cur = (ID)make_seq(coll_eval);
  if (cur) {
    while (!seq_empty(cur)) {
      CljObject *element = (CljObject *)seq_first(cur);

      WITH_AUTORELEASE_POOL({
        if (ctx) {
          CljPersistentVector *new_stack = (CljPersistentVector *)RETAIN(ctx->env_stack);
          CljPersistentMap *self_bind = RETAIN(map_assoc(map_empty(), var, element));
          env_stack_push_inplace(&new_stack, self_bind);
          RELEASE(self_bind);

          EvalContext inner_ctx = *ctx;
          inner_ctx.env_stack = new_stack;

          ID body_result = eval_body(body, NULL, st, &inner_ctx);
          RELEASE(new_stack);
          RELEASE(body_result);
        } else {
          CljPersistentMap *new_env = extend_env_with_binding(env, var, element);
          if (new_env) {
            ID body_result = eval_body_with_env(body, new_env, st);
            RELEASE(body_result);
            RELEASE(new_env);
          }
        }
      });

      // Advance using the central helper so list-vs-seq ownership stays correct.
      seq_next_inplace(&cur);
    }
    RELEASE(cur);
  }
  RELEASE(coll_eval);
  return AUTORELEASE(NULL); // doseq always returns nil
}

ID eval_list_function(CljList *list, CljPersistentMap *env) {
  CLJ_ASSERT(env != NULL);
  (void)env; // Suppress unused parameter warning
  // (list arg1 arg2 ...)
  CLJ_ASSERT(list != NULL && is_list_type(TAG(list)));

  CljList *list_data = as_list(list);

  // Create new list starting from the second element (skip 'list' symbol)
  CljObject *args_list = (CljObject *)LIST_REST(list_data);
  if (!args_list) {
    // No arguments - return empty list (like native_list does)
    return empty_list();
  }

  // Simply return the arguments as a list (they're already evaluated by call dispatch)
  // args_list is part of the list_data structure, which is already safe (caller has strong reference)
  return args_list;
}

// (let [bindings*] body*) — destructuring in ast_canon; here bindings are symbol init-expr pairs.
ID eval_let(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  CLJ_ASSERT(args != NULL && st != NULL);

  CljPersistentMap *eval_env = eval_env_or_ns_mappings(env, st);
  if (!eval_env) {
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "let requires a valid environment");
    return NULL;
  }

  unsigned int argc = vector_count(args);
  CLJ_ASSERT(argc >= 1 && "let: at least bindings vector required");
  CljObject *bindings_vec = (CljObject *)vector_nth(args, 0);
  CLJ_ASSERT(bindings_vec && TAG(bindings_vec) == CLJ_VECTOR_PERSISTENT && "let: bindings must be a vector");
  CljPersistentVector *bindings = as_vector((CljValue)bindings_vec);
  CLJ_ASSERT(bindings != NULL);
  int binding_count = vector_count(bindings);
  CLJ_ASSERT(binding_count % 2 == 0 && "let: even number of binding forms required");

  int pair_count = binding_count / 2;

  CljPersistentVector *let_stack = ctx ? (CljPersistentVector *)RETAIN(ctx->env_stack) : NULL;

  CallFrame let_frame_storage;
  ID binding_slots[CALLFRAME_MAX_PARAMS * 2];
  CLJ_ASSERT(pair_count <= CALLFRAME_MAX_PARAMS && "let: too many bindings");
  CallFrame *let_frame = &let_frame_storage;
  ID *binding_params = binding_slots;
  ID *binding_values = binding_slots + pair_count;
  frame_init(let_frame, ctx ? ctx->frame : NULL);

  CljPersistentMap *let_env_map = make_map(pair_count);
  env_stack_push_inplace(&let_stack, let_env_map);
  RELEASE(let_env_map);

  EvalContext let_ctx = ctx ? *ctx : (EvalContext){0};
  let_ctx.frame = let_frame;
  let_ctx.env_stack = let_stack;
  let_ctx.env = let_ctx.env ? let_ctx.env : eval_env;
  let_ctx.st = let_ctx.st ? let_ctx.st : st;

  int binding_index = 0;
  for (int i = 0; i < binding_count; i += 2) {
    CljValue sym_val = (CljValue)vector_nth(bindings, i);
    CljValue init_val = (CljValue)vector_nth(bindings, i + 1);
    CLJ_ASSERT(sym_val && TAG(sym_val) == CLJ_SYMBOL && "let: binding form must be a symbol");

    ID value = (!init_val || is_fixnum(init_val) || is_special(init_val))
                   ? (ID)init_val
                   : eval_body(init_val, eval_env, st, &let_ctx);

    binding_params[binding_index] = sym_val;
    binding_values[binding_index] = value;
    if (value && !IS_IMMEDIATE(value))
      RETAIN(value);
    frame_set_bindings(let_frame, ctx ? ctx->frame : NULL,
                       binding_params, binding_values, binding_index + 1);

    bool force_cow_let_env_assoc = false;
    if (is_closure(value)) {
      CljFunction *fv = as_function(value);
      if (fv && fv->env_stack) {
        unsigned int captured_count = vector_count(fv->env_stack);
        if (captured_count > 0) {
          ID captured_top = vector_nth(fv->env_stack, captured_count - 1);
          force_cow_let_env_assoc = (captured_top == (ID)let_env_map);
        }
      }
    }
    if (force_cow_let_env_assoc) {
      // The closure captured the current let frame map via a shared env_stack vector.
      // Prevent in-place map mutation here, otherwise the map gains (sym -> closure)
      // and creates a retain-cycle: closure -> env_stack -> let-map -> closure.
      RETAIN(let_env_map);
    }
    CljPersistentMap *updated = map_assoc(let_env_map, sym_val, value);
    if (force_cow_let_env_assoc) {
      RELEASE(let_env_map);
    }
    if (updated && updated != let_env_map && let_ctx.env_stack) {
      vector_assoc_inplace(&let_ctx.env_stack, vector_count(let_ctx.env_stack) - 1, (ID)updated);
      // vector_assoc_inplace may COW and transfer ownership from the old pointer.
      // Keep the local cleanup owner in sync with the context field.
      let_stack = let_ctx.env_stack;
      let_env_map = updated;
    }
    ID stored_value = binding_values[binding_index];
    if (is_closure(stored_value)) {
      CljFunction *f = as_function(stored_value);
      if (f && !f->name_sym && TAG(sym_val) == CLJ_SYMBOL) {
        // Let-bound anonymous fns can recurse cycle-free via EvalContext.current_fn
        // once they are tagged with the binding symbol.
        f->name_sym = as_symbol(sym_val);
      }
    }
    if (value && !IS_IMMEDIATE(value))
      RELEASE(value);
    binding_index++;
  }

  ID result = NULL;
  VECTOR_FOR_EACH(args, body_expr) {
    if (_i == 0)
      continue;
    if (!body_expr)
      continue;
    result = (is_fixnum((CljValue)body_expr) || is_special((CljValue)body_expr))
                 ? body_expr
                 : eval_body(body_expr, let_ctx.env, st, &let_ctx);
  }

  int preserved_result_frame_refs = 0;
  if (result && !IS_IMMEDIATE(result)) {
    for (int i = 0; i < binding_index; i++) {
      if (binding_values[i] == result) {
        preserved_result_frame_refs++;
      }
    }
  }
  frame_release_except(let_frame, result);
  RELEASE(let_stack);
  while (preserved_result_frame_refs-- > 0) {
    RELEASE(result);
  }
  return result;
}

// Helper function for evaluating arguments
ID eval_arg(CljList *list, int index, CljPersistentMap *env, EvalState *st) {
  return eval_arg_with_context(list, index, env, st, NULL);
}

ID eval_arg_with_context(CljList *list, int index, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  CLJ_ASSERT(list != NULL && is_list_type(TAG(list)));

  // Get element from list
  ID element = list_nth(as_list(list), index);
  if (!element)
    return NULL;

  return eval_arg_from_expr_with_context(element, env, st, ctx);
}

ID eval_arg_from_expr_with_context(ID expr, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  if (expr == SYM_NIL)
    return NULL;
  if (!expr)
    return NULL;

  if (IS_IMMEDIATE(expr)) {
    return expr;
  }

  unsigned char expr_tag = TAG(expr);

  CLJ_ASSERT(expr_tag != CLJ_SYMBOL_TOKEN && "Symbol tokens must be canonicalized before evaluation");

  if (expr_tag == CLJ_SLOT_REF) {
    const CljSlotRef *ref = (const CljSlotRef *)expr;
    CLJ_ASSERT(ctx && ctx->frame && "CLJ_SLOT_REF requires frame context");
    ID v = frame_get_slot(ctx->frame, ref->depth, ref->slot);
    if (v == NOT_FOUND || !v)
      return NULL;
    return v;
  }

  if (expr_tag == CLJ_SYMBOL) {
    // NOTE: SYM_NIL already checked at function entry
    if (IS_KEYWORD(expr)) {
      return expr;
    }

    // Use frame_lookup for O(1) parameter resolution (symbols are interned)
    if (ctx && ctx->frame) {
      ID frame_value = NULL;
      if (frame_lookup(ctx->frame, expr, &frame_value)) {
        if (frame_value == NOT_FOUND) {
          return NULL; // Parameter bound to nil
        }
        CLJ_ASSERT(frame_value && "frame lookup must return value or NOT_FOUND");
        return frame_value;
      }
    }

    // Cold-path setup: only needed once we leave symbol/frame fast paths.
    EvalState *eval_st = get_eval_state(ctx, st);
    CljPersistentMap *eval_env = env;
    if (!eval_env && ctx) {
      eval_env = get_closure_env(ctx);
    }
    eval_env = eval_env_or_ns_mappings(eval_env, eval_st);

    ID resolved_value = NULL;
    bool resolved_found = false;

    // If context is provided with env_stack, use resolve_symbol_in_env
    // to search through the entire environment stack (for nested let blocks)
    if (ctx) {
      ID resolved_id = resolve_symbol_in_env_with_frame(ctx->env_stack, eval_env, ctx->frame, expr, eval_st);
      if (resolved_id != NOT_FOUND) {
        if (!resolved_id || resolved_id == SYM_NIL) {
          return NULL;
        }
        // CRITICAL: If resolved_id is still a symbol (not a value), throw exception
        // This prevents infinite loops where a symbol resolves to itself
        if (is_symbol(resolved_id) && !IS_KEYWORD(resolved_id)) {
          bool resolves_to_self = (resolved_id == expr);
          if (!resolves_to_self && resolved_id && expr) {
            resolves_to_self = clj_equal(resolved_id, expr);
          }
          if (resolves_to_self) {
            CljSymbol *sym_obj = as_symbol(expr);
            throw_unresolved_symbol_exception_symbol(sym_obj);
          }
        }
        resolved_value = resolved_id;
        resolved_found = true;
      }
      // Not found in env_stack or still a symbol, fall through to namespace resolution
    }

    if (!resolved_value && is_map(eval_env)) {
      // Use sentinel to distinguish "key not found" from "value is nil"
      ID resolved_id = map_get(eval_env, expr);
      if (resolved_id != NOT_FOUND) {
        // Key exists in map (value may be NULL/nil)
        // map_get returns retained value, eval_arg should return AUTORELEASE
        if (!resolved_id)
          return NULL; // nil
        resolved_value = resolved_id;
        resolved_found = true;
      }
    }

    if (!resolved_value) {
      // Dynamic vars may be bound to nil; allow use in argument position.
      if (eval_st) {
        CljSymbol *sym = as_symbol(expr);
        if (sym && is_dynamic_var_symbol(sym)) {
          ID bound = dynamic_binding_lookup(eval_st, sym);
          if (bound != NOT_FOUND) {
            if (!bound)
              return NULL;
            if (IS_IMMEDIATE(bound))
              return bound;
            return bound;
          }
        }
      }

      ID resolved = ns_resolve(eval_st, as_symbol(expr));
      if (resolved != NOT_FOUND) {
        resolved_found = true;
        if (!resolved || resolved == SYM_NIL) {
          return NULL;
        }
        resolved_value = resolved;
      }
    }

    if (resolved_found) {
      if (!resolved_value) {
        return NULL;
      }
      if (IS_IMMEDIATE(resolved_value)) {
        return resolved_value;
      }
      return resolved_value;
    }

    // Only call as_symbol when needed (error paths)
    CljSymbol *sym_obj = as_symbol(expr);
    if (sym_obj && sym_obj->cname) {
      CljNamespace *ns_candidate = ns_find(sym_obj->cname);
      if (ns_candidate) {
        return (CljObject *)ns_candidate;
      }
    }
    throw_unresolved_symbol_exception_symbol(sym_obj);
    return NULL;
  }

  // Non-symbol heap values that require further evaluation need the full eval context.
  EvalState *eval_st = get_eval_state(ctx, st);
  CljPersistentMap *eval_env = env;
  if (!eval_env && ctx) {
    eval_env = get_closure_env(ctx);
  }
  eval_env = eval_env_or_ns_mappings(eval_env, eval_st);

  if (expr_tag == CLJ_AST_CALL) {
    EvalState *call_st = eval_st ? eval_st : builtin_get_eval_state();
    return eval_ast_call(as_ast_call(expr), eval_env, call_st, ctx);
  }

  if (is_list_type(expr_tag)) {
    return eval_noncanonical_list_form(expr);
  }

  if (expr_tag == CLJ_MAP_PERSISTENT) {
    CljPersistentMap *map = (CljPersistentMap *)expr;
    CljPersistentMap *result = map_empty();

    MAP_FOR_EACH(map, key, value) {
      ID key_id = key;
      ID value_id = value;
      ID eval_key = (key_id == SYM_NIL) ? NULL : eval_body(key_id, eval_env, eval_st, ctx);
      ID eval_value = (value_id == SYM_NIL) ? NULL : eval_body(value_id, eval_env, eval_st, ctx);
      map_assoc_inplace(&result, eval_key, eval_value);
    }

    return AUTORELEASE(result);
  }

  if (expr_tag == CLJ_VECTOR_PERSISTENT || expr_tag == CLJ_VECTOR_TRANSIENT) {
    CljPersistentVector *vec =
        (expr_tag == CLJ_VECTOR_TRANSIENT)
            ? vector_persistent(as_transient_vector(expr))
            : as_persistent_vector(expr);
    unsigned int count = vector_count(vec);
    if (count == 0)
      return expr;

    CljPersistentVector *result = make_vector(count, STRONG);
    VECTOR_FOR_EACH(vec, elem) {
      ID eval_elem = (elem && elem != SYM_NIL) ? eval_body(elem, eval_env, eval_st, ctx) : NULL;
      CljPersistentVector *old_result = result;
      result = vector_conj_owned(result, eval_elem);
      if (result != old_result) {
        RELEASE(old_result);
      }
    }
    return AUTORELEASE(result);
  }

  return expr;
}

ID eval_dotimes(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  CLJ_ASSERT(env != NULL);
  // (dotimes [var n] expr)
  // Executes expr n times with var bound to 0, 1, ..., n-1

  unsigned int argc = args ? vector_count(args) : 0;
  if (argc == 0) {
    return NULL;
  }

  // Extract binding vector/list (first argument after dotimes) and body forms.
  ID binding_list = vector_nth(args, 0);

  if (!binding_list)
    return NULL;

  // Parse binding: [var n] - support both vectors and lists
  ID var = NULL;
  ID n_obj = NULL;

  if (is_vector(binding_list)) {
    CljPersistentVector *vec = as_vector(binding_list);
    CLJ_ASSERT(vec != NULL && "vector binding must cast to non-null vector");
    if (vector_count(vec) < 2)
      return NULL;
    var = vector_nth(vec, 0);
    n_obj = vector_nth(vec, 1);
  } else if (is_list_type(TAG(binding_list))) {
    CljList *binding_data = as_list(binding_list);
    CLJ_ASSERT(binding_data != NULL && "list binding must cast to non-null list");
    if (!binding_data->first)
      return NULL;
    var = binding_data->first;
    CljList *rest_list = as_list(binding_data->rest);
    if (!rest_list || !rest_list->first)
      return NULL;
    n_obj = rest_list->first;
  } else {
    return NULL;
  }

  if (!var || TAG(var) != CLJ_SYMBOL || !n_obj)
    return NULL;

  EvalContext local_ctx = {0};
  CljPersistentVector *owned_stack = NULL;
  const EvalContext *effective_ctx = ensure_eval_context(env, st, ctx, &local_ctx, &owned_stack);

  // Evaluate n (once) using the current lexical context.
  ID n_evaluated = eval_arg_from_expr_with_context(n_obj, env, st, effective_ctx);

  if (!n_evaluated || TAG(n_evaluated) != CLJ_INT)
    return NULL;
  int n = as_fixnum((CljValue)n_evaluated);
  if (n <= 0)
    return AUTORELEASE(NULL);

  // Loop var binding: use a stack CallFrame to avoid allocating a new map/env each iteration.
  CallFrame dotimes_frame;
  frame_init(&dotimes_frame, effective_ctx ? effective_ctx->frame : NULL);
  ID dotimes_params[1] = {var};
  dotimes_frame.params = dotimes_params;
  dotimes_frame.param_count = 1;

  // Evaluate body with context so symbol lookup hits frame_lookup.
  EvalContext dotimes_ctx = effective_ctx ? *effective_ctx : (EvalContext){0};
  dotimes_ctx.env = dotimes_ctx.env ? dotimes_ctx.env : env;
  dotimes_ctx.frame = &dotimes_frame;

  EvalState *eval_st = st ? st : builtin_get_eval_state();
  dotimes_ctx.st = eval_st;

  for (int i = 0; i < n; i++) {
    dotimes_frame.values[0] = frame_encode_value(fixnum((int32_t)i));

    WITH_AUTORELEASE_POOL({
      for (unsigned int b = 1; b < argc; b++) {
        ID body_expr = vector_nth(args, b);
        if (body_expr)
          eval_body(body_expr, env, eval_st, &dotimes_ctx);
      }
    });
  }

  frame_release(&dotimes_frame);
  RELEASE(owned_stack);
  return NULL;
}

// ============================================================================
// EVAL_TIME - Time measurement special form implementation
// ============================================================================
ID eval_time(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  // (time expr)
  if (!args || !st) {
    return NULL;
  }

  // Validate arity: exactly 1 argument
  unsigned int argc = vector_count(args);
  if (!validate_arity((int)argc, 1, "time")) {
    return NULL;
  }

  // Get the expression to time (second element): (time expr)
  CljObject *expr = (CljObject *)vector_nth(args, 0);
  if (!expr) {
    return NULL;
  }

  // Start timing with gettimeofday (works on all Unix systems)
  struct timeval start, end;
  gettimeofday(&start, NULL);

  // Use provided env or fall back to current_ns->mappings (like eval_parsed does)
  CljPersistentMap *eval_env = eval_env_or_ns_mappings(env, st);

  // Evaluate the expression in the current lexical context.
  // Use eval_env so namespace-bound symbols (e.g. +) are available.
  ID result = eval_body((ID)expr, eval_env, st, ctx);

  // End timing
  gettimeofday(&end, NULL);

  // Calculate elapsed time in milliseconds with microsecond precision
  long long start_us = start.tv_sec * 1000000LL + start.tv_usec;
  long long end_us = end.tv_sec * 1000000LL + end.tv_usec;
  double elapsed_ms = (double)(end_us - start_us) / 1000.0;

  // Print timing information (Clojure-compatible: "msecs" format)
  // Suppress output in test context
  if (!g_suppress_time_output) {
    printf("Elapsed time: %.2f msecs\n", elapsed_ms);
  }

  // Return the result of the evaluated expression (Clojure-compatible: return the value)
  // All paths now return AUTORELEASE objects (or immediates/NULL)
  // If result is NULL, return NULL (nil)
  if (!result) {
    return NULL;
  }
  // If result is immediate, return it directly
  if (IS_IMMEDIATE(result)) {
    return result;
  }
  // For heap objects from map_get/ns_resolve, return pool-safe refs as-is.
  return result;
}

/**
 * @brief Evaluate an expression and print heap memory growth
 *
 * Special form: (heap expr)
 * Measures retained heap growth and local peak increase while evaluating expr.
 * Prints: "Heap growth: X bytes (peak +Y bytes)"
 * Returns the result of evaluating expr.
 *
 * @param list The heap form list (heap expr)
 * @param env The evaluation environment
 * @param st The evaluation state
 * @param ctx The evaluation context
 * @return The result of evaluating expr (autoreleased) or NULL (nil)
 */
#ifdef DEBUG
ID eval_heap(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
  // (heap expr) - Memory leak detector.
  // Evaluates expr once and returns a map of per-type bytes deltas.
  if (!args || !st)
    return NULL;

  unsigned int argc = vector_count(args);
  if (!validate_arity((int)argc, 1, "heap"))
    return NULL;

  CljObject *expr = (CljObject *)vector_nth(args, 0);
  CljPersistentMap *eval_env = eval_env_or_ns_mappings(env, st);

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
  // During memory profiling, disable callsite cache to avoid cache churn artifacts.
  uint16_t saved_epoch = g_runtime.resolve_cache_epoch;
  g_runtime.resolve_cache_epoch = 0;
#endif

  // Capture stats before measurement
  MemoryStats stats_before = memory_profiler_get_stats();
  size_t saved_peak_memory_usage = g_memory_stats.peak_memory_usage;
  size_t saved_raw_bytes_peak = g_memory_stats.raw_bytes_peak;
  size_t saved_raw_blocks_peak = g_memory_stats.raw_blocks_peak;
  size_t saved_bytes_peak_by_type[CLJ_TYPE_COUNT];
  for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
    saved_bytes_peak_by_type[i] = g_memory_stats.bytes_peak_by_type[i];
    g_memory_stats.bytes_peak_by_type[i] = g_memory_stats.bytes_current_by_type[i];
  }
  g_memory_stats.peak_memory_usage = g_memory_stats.current_memory_usage;
  g_memory_stats.raw_bytes_peak = g_memory_stats.raw_bytes_current;
  g_memory_stats.raw_blocks_peak = g_memory_stats.raw_blocks_current;

  // Make builtins like (eval) / (read-string) work inside (heap ...)
  // even when this path doesn't go through eval_function_call's builtin wrapper.
  EvalState *saved_builtin_st = builtin_get_eval_state();
  builtin_set_eval_state(st);

  // Measurement pass: eval_body follows MEMORY_POLICY; do not add extra AUTORELEASE.
  WITH_AUTORELEASE_POOL({
    ID measured = eval_body((ID)expr, eval_env, st, ctx);
    (void)measured;
  });

  builtin_set_eval_state(saved_builtin_st);

  // Capture stats after measurement
  MemoryStats stats_after = memory_profiler_get_stats();

  // Calculate total diff
  long long total_diff = (long long)stats_after.current_memory_usage - (long long)stats_before.current_memory_usage;
  long long peak_extra = (long long)stats_after.peak_memory_usage - (long long)stats_before.current_memory_usage;
  if (peak_extra < 0) {
    peak_extra = 0;
  }

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
  // Restore callsite cache epoch
  g_runtime.resolve_cache_epoch = saved_epoch;
#endif

  g_memory_stats.peak_memory_usage = saved_peak_memory_usage > stats_after.peak_memory_usage
                                       ? saved_peak_memory_usage
                                       : stats_after.peak_memory_usage;
  g_memory_stats.raw_bytes_peak = saved_raw_bytes_peak > stats_after.raw_bytes_peak
                                    ? saved_raw_bytes_peak
                                    : stats_after.raw_bytes_peak;
  g_memory_stats.raw_blocks_peak = saved_raw_blocks_peak > stats_after.raw_blocks_peak
                                     ? saved_raw_blocks_peak
                                     : stats_after.raw_blocks_peak;
  for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
    g_memory_stats.bytes_peak_by_type[i] = saved_bytes_peak_by_type[i] > stats_after.bytes_peak_by_type[i]
                                             ? saved_bytes_peak_by_type[i]
                                             : stats_after.bytes_peak_by_type[i];
  }

  // Build result map with per-type diffs. Always return a map, even if all
  // deltas are zero (useful for consistent debugging output).
  CljPersistentMap *result = map_empty();
  ASSIGN(result, map_assoc(result, intern_symbol_global(":total"), fixnum((int)total_diff)));
  ASSIGN(result, map_assoc(result, intern_symbol_global(":peak"), fixnum((int)peak_extra)));

  // Add per-type diffs (bytes_current_by_type tracks actual bytes)
  for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
    long long bytes_diff = (long long)stats_after.bytes_current_by_type[i] - (long long)stats_before.bytes_current_by_type[i];
    if (bytes_diff != 0) {
      const char *type_name = clj_type_name((CljType)i);
      // Build keyword with ":" prefix
      char kw_buf[64];
      kw_buf[0] = ':';
      size_t len = strlen(type_name);
      if (len >= sizeof(kw_buf) - 1)
        len = sizeof(kw_buf) - 2;
      memcpy(kw_buf + 1, type_name, len);
      kw_buf[len + 1] = '\0';
      ASSIGN(result, map_assoc(result, intern_symbol_global(kw_buf), fixnum((int)bytes_diff)));
    }
  }

  // Print summary if output is enabled (map will be printed by REPL anyway)
  if (!g_suppress_time_output) {
    printf("Heap growth: %lld bytes (peak +%lld bytes)\n", total_diff, peak_extra);
  }

  return AUTORELEASE(result);
}
#endif // DEBUG

// ============================================================================
// FUNCTION CALL IMPLEMENTATION
// ============================================================================

// ============================================================================
// EVAL_STRING IMPLEMENTATION
// ============================================================================

/**
 * @brief Evaluate a parsed CljValue (handles immediate values and heap objects)
 * @param parsed The parsed CljValue (can be immediate or heap object)
 * @param eval_state The evaluation state
 * @return The evaluated result (caller-usable per MEMORY_POLICY) or NULL if nil
 *
 * This is a DRY helper used by both eval_string and eval_multiform_string.
 */
ID eval_parsed_value(CljValue parsed, EvalState *eval_state) {
  // Reset eval recursion depth at each top-level eval entry.
  // This ensures the counter is correct even after longjmp-based exception recovery.
  reset_eval_depths();
  // Set stack base ON THIS FRAME (not inside reset_eval_depths, whose frame is gone).
  char _stack_base_marker;
  s_eval_stack_base = (uintptr_t)(void *)&_stack_base_marker;
#ifdef DEBUG
  s_eval_stack_peak = 0;
#endif

  // Check if parsed is an immediate value
  if (IS_IMMEDIATE(parsed)) {
    // For immediate values, return them as CljObject* (they're already evaluated)
    return parsed;
  }

  // For heap objects, evaluate them (use NULL env to use current_ns->mappings)
  ID result = eval_parsed(parsed, eval_state, NULL);

  // Convert SYM_NIL to NULL (nil representation)
  if (result == SYM_NIL) {
    return NULL;
  }

  // eval_parsed/eval_* return caller-usable results per MEMORY_POLICY.
  return result;
}

/**
 * @brief Parse and evaluate a Clojure expression from a string (convenience)
 * @param expr_str The Clojure expression as a string
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased in caller's pool) or NULL only if result is nil.
 *         Caller must not release; use result only while current autorelease pool is active.
 */
ID eval_string(const char *expr_str, EvalState *eval_state) {
  CLJ_ASSERT(expr_str != NULL);
  CLJ_ASSERT(eval_state != NULL);

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
  // While profiling, disable callsite cache for ephemeral ASTs to reduce noise.
  uint16_t saved_epoch = g_runtime.resolve_cache_epoch;
  g_runtime.resolve_cache_epoch = 0;
#endif

  ID result = NULL;
  WITH_AUTORELEASE_POOL({
    Reader reader;
    reader_init(&reader, expr_str);
    reader_set_source_name(&reader, "<string input>");

    CljValue parsed = parse_from_reader(&reader, eval_state);
    if (parsed == NULL) {
      throw_exception(EXCEPTION_PARSE, "Failed to parse expression", __FILE__, __LINE__, 0);
      result = NULL;
    } else {
      result = eval_parsed_value(parsed, eval_state);
      RETAIN(result);
    }
  });
#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
  g_runtime.resolve_cache_epoch = saved_epoch;
#endif
  return AUTORELEASE(result);
}

// ============================================================================
// COMMON EVALUATION HELPERS
// ============================================================================

ID *alloc_obj_array(int size, ID *stack_buffer) {
  if (size <= 16) {
    return stack_buffer;
  }
  return (ID *)CLJ_MALLOC((size_t)size * sizeof(*stack_buffer));
}

void free_obj_array(ID *array, ID *stack_buffer) {
  if (array != stack_buffer) {
    CLJ_FREE((void *)array);
  }
}
