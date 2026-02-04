#include "atom.h"
#include "memory.h"
#include "value.h"
#include "eval.h"
#include "exception.h"
#include "runtime.h"
#include "namespace.h"
#include <stdlib.h>
#include <stdbool.h>

/** Create an atom with initial value.
 * @param value Initial value (can be NULL/nil or immediate)
 * @return New atom object with RC=1 (caller must release)
 */
CljAtom* make_atom(ID value) {
    CljAtom *atom = ALLOC(CljAtom, 1);
    if (!atom) {
        throw_oom();
        return NULL;
    }

    atom->base.type = CLJ_ATOM;
    // RETAIN handles nil and immediates safely (ignores them)
    atom->value = RETAIN(value);

    return atom;
}

/** Get the current value of an atom.
 * @param atom Atom object
 * @return Current value (caller must release if not immediate)
 */
ID atom_deref(CljAtom *atom) {
    if (!atom) return NULL;
    // RETAIN handles nil and immediates safely (ignores them)
    return RETAIN(atom->value);
}

/** Set the value of an atom directly.
 * @param atom Atom object
 * @param new_value New value (can be NULL/nil or immediate)
 * @return New value (caller must release if not immediate)
 */
ID atom_reset(CljAtom *atom, ID new_value) {
    if (!atom) return NULL;

    // Use ASSIGN to safely replace atom value (releases old, retains new)
    ASSIGN(atom->value, new_value);

    // Return new value (RETAIN handles nil and immediates safely)
    return RETAIN(new_value);
}

/** Apply a function to the atom's value and update it.
 * @param atom Atom object
 * @param fn Function to apply
 * @param args Additional arguments (can be NULL)
 * @param argc Number of additional arguments
 * @return New value (caller must release if not immediate)
 */
ID atom_swap(CljAtom *atom, ID fn, ID *args, unsigned int argc) {
    // Validate arguments (Clojure/JVM behavior: throw IllegalArgumentException)
    if (!atom) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "swap! requires an atom",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Resolve symbol to function if necessary (Clojure/JVM behavior)
    // ns_resolve automatically searches clojure.core, so we don't need to set current_ns
    if (fn && TAG(fn) == CLJ_SYMBOL) {
        // ns_resolve searches clojure.core even if current_ns is different
        // Pass NULL for st to use default namespace - ns_resolve will still search clojure.core
        ID resolved = ns_resolve(NULL, as_symbol(fn));
        if (resolved != NOT_FOUND) {
            // resolved is retained by the map, so we can use it directly
            // No need to RELEASE old fn (it's a parameter) or RETAIN resolved (already retained)
            fn = resolved;
        }
    }

    // Validate that fn is a valid function (Clojure/JVM throws IllegalArgumentException/ClassCastException)
    if (!fn || (TAG(fn) != CLJ_FUNC && TAG(fn) != CLJ_CLOSURE)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "swap! requires a function",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Get current value (RETAIN handles nil and immediates safely)
    ID current_value = RETAIN(atom->value);

    // Prepare function call arguments: [current_value, ...args]
    // Use malloc instead of calloc - array is immediately filled
    ID *fn_args = (ID*)malloc((argc + 1) * sizeof(ID));
    if (!fn_args) {
        RELEASE(current_value);
        throw_oom();
        return NULL;
    }

    fn_args[0] = current_value;  // First argument is current atom value
    for (unsigned int i = 0; i < argc; i++) {
        // RETAIN handles nil and immediates safely (ignores them)
        fn_args[i + 1] = RETAIN(args[i]);
    }

    // Call function with current value and additional args
    EvalState *st = get_global_eval_state();
    CljPersistentMap *env = st ? (CljPersistentMap*)st->current_ns->mappings : NULL;

    ID new_value = NULL;
    TRY {
        new_value = eval_function_call(fn, fn_args, argc + 1, env, st);
    } CATCH(ex) {
        // Cleanup on exception (RELEASE handles nil and immediates safely)
        for (unsigned int i = 0; i < argc + 1; i++) {
            RELEASE(fn_args[i]);
        }
        free(fn_args);
        RELEASE(current_value);
        return NULL;
    } END_TRY

    // Cleanup function arguments (RELEASE handles nil and immediates safely)
    for (unsigned int i = 0; i < argc + 1; i++) {
        RELEASE(fn_args[i]);
    }
    free(fn_args);

    RELEASE(current_value);

    if (!new_value) {
        // Function returned nil or error
        return NULL;
    }

    // Update atom with new value
    atom_reset(atom, new_value);

    // Return new value (already retained by atom_reset, but we need another retain for caller)
    // RETAIN handles nil and immediates safely (ignores them)
    return RETAIN(new_value);
}
