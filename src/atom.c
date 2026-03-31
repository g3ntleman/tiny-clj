#include "atom.h"
#include "memory.h"
#include "value.h"
#include "eval.h"
#include "exception.h"
#include "runtime.h"
#include "namespace.h"
#include <stdlib.h>
#include <stdbool.h>

#if CLJ_ATOM_USE_MUTEX
static inline void atom_lock(CljAtom *atom) {
  (void)pthread_mutex_lock(&atom->mutex);
}

static inline void atom_unlock(CljAtom *atom) {
  (void)pthread_mutex_unlock(&atom->mutex);
}
#else
static inline void atom_lock(CljAtom *atom) {
  (void)atom;
}

static inline void atom_unlock(CljAtom *atom) {
  (void)atom;
}
#endif

/** Create an atom with initial value.
 * @param value Initial value (can be NULL/nil or immediate)
 * @return New atom object with RC=1 (caller must release)
 */
CljAtom *make_atom(ID value) {
  CljAtom *atom = ALLOC(CljAtom, 1);

  atom->base.type = CLJ_ATOM;
#if CLJ_ATOM_USE_MUTEX
  (void)pthread_mutex_init(&atom->mutex, NULL);
#endif
  // RETAIN handles nil and immediates safely (ignores them)
  atom->value = RETAIN(value);

  return atom;
}

ID atom_deref(CljAtom *atom) {
  if (!atom)
    return NULL;
  atom_lock(atom);
  ID value = RETAIN(atom->value);
  atom_unlock(atom);
  return AUTORELEASE(value);
}

ID atom_deref_owned(CljAtom *atom) {
  if (!atom)
    return NULL;
  atom_lock(atom);
  ID value = RETAIN(atom->value);
  atom_unlock(atom);
  return value;
}

ID atom_peek(CljAtom *atom) {
  if (!atom)
    return NULL;
  atom_lock(atom);
  ID value = atom->value;
  atom_unlock(atom);
  return value;
}

void atom_set(CljAtom *atom, ID new_value) {
  if (!atom)
    return;
  atom_lock(atom);
  ASSIGN(atom->value, new_value);
  atom_unlock(atom);
}

/** Internal helper: set atom value and return owned result. */
static ID atom_reset_owned(CljAtom *atom, ID new_value) {
  if (!atom)
    return NULL;

  atom_set(atom, new_value);

  // Return new value (RETAIN handles nil and immediates safely)
  return RETAIN(new_value);
}

ID atom_reset(CljAtom *atom, ID new_value) {
  return AUTORELEASE(atom_reset_owned(atom, new_value));
}

/** Apply a function to the atom's value and update it (owned result).
 * @param atom Atom object
 * @param fn Function to apply
 * @param args Additional arguments (can be NULL)
 * @param argc Number of additional arguments
 * @return New value (caller must release if not immediate)
 */
static ID atom_swap_owned(CljAtom *atom, ID fn, ID *args, unsigned int argc) {
  // Validate arguments (Clojure/JVM behavior: throw IllegalArgumentException)
  if (!atom) {
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "swap! requires an atom",
                    __FILE__, __LINE__, 0);
    return NULL;
  }

  ID fn_local = fn;

  // Resolve symbol to function if necessary (Clojure/JVM behavior)
  // ns_resolve automatically searches clojure.core, so we don't need to set current_ns
  if (fn_local && TAG(fn_local) == CLJ_SYMBOL) {
    // ns_resolve searches clojure.core even if current_ns is different
    // Pass NULL for st to use default namespace - ns_resolve will still search clojure.core
    ID resolved = ns_resolve(NULL, as_symbol(fn_local));
    if (resolved != NOT_FOUND) {
      // resolved is retained by the map, so we can use it directly
      // No need to RELEASE old fn (it's a parameter) or RETAIN resolved (already retained)
      fn_local = resolved;
    }
  }

  // Validate that fn is a valid function (Clojure/JVM throws IllegalArgumentException/ClassCastException)
  if (!fn_local || (TAG(fn_local) != CLJ_FUNC && TAG(fn_local) != CLJ_CLOSURE)) {
    throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "swap! requires a function",
                    __FILE__, __LINE__, 0);
    return NULL;
  }

  // Get current value (RETAIN handles nil and immediates safely)
  ID current_value = atom_deref_owned(atom);

  // Keep the common swap! arities on the stack to avoid hot-path heap churn.
  ID fn_args_buf[4];
  ID *fn_args = fn_args_buf;
  bool fn_args_on_heap = false;
  if (argc + 1u > (sizeof(fn_args_buf) / sizeof(fn_args_buf[0]))) {
    fn_args = (ID *)CLJ_MALLOC((argc + 1) * sizeof(ID));
    if (!fn_args) {
      RELEASE(current_value);
      return NULL;
    }
    fn_args_on_heap = true;
  }

  fn_args[0] = current_value; // First argument is current atom value
  for (unsigned int i = 0; i < argc; i++) {
    // RETAIN handles nil and immediates safely (ignores them)
    fn_args[i + 1] = RETAIN(args[i]);
  }

  // Call function with current value and additional args
  EvalState *st = get_global_eval_state();
  CljPersistentMap *env = st ? (CljPersistentMap *)st->current_ns->mappings : NULL;

  ID new_value = NULL;
  TRY {
    new_value = eval_function_call((ID)fn_local, fn_args, argc + 1, env, st);
  }
  CATCH(ex) {
    // Cleanup on exception (RELEASE handles nil and immediates safely)
    for (unsigned int i = 0; i < argc + 1; i++) {
      RELEASE(fn_args[i]);
    }
    if (fn_args_on_heap) {
      CLJ_FREE(fn_args);
    }
    return NULL;
  }
  END_TRY

  // Cleanup function arguments (RELEASE handles nil and immediates safely)
  for (unsigned int i = 0; i < argc + 1; i++) {
    RELEASE(fn_args[i]);
  }
  if (fn_args_on_heap) {
    CLJ_FREE(fn_args);
  }

  if (!new_value) {
    // Function returned nil or error
    return NULL;
  }

  // Update atom with new value
  return atom_reset_owned(atom, new_value);
}

ID atom_swap(CljAtom *atom, ID fn, ID *args, unsigned int argc) {
  return AUTORELEASE(atom_swap_owned(atom, fn, args, argc));
}

void atom_destroy(CljAtom *atom) {
  if (!atom)
    return;
#if CLJ_ATOM_USE_MUTEX
  (void)pthread_mutex_destroy(&atom->mutex);
#endif
}
