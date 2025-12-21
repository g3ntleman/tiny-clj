/**
 * @file macro.h
 * @brief Macro registry and expansion infrastructure
 *
 * Provides functions for:
 * - Registering macros in namespaces
 * - Looking up macros by symbol
 * - Macro expansion (delegating to Clojure implementation after bootstrap)
 */

#ifndef TINY_CLJ_MACRO_H
#define TINY_CLJ_MACRO_H

#include "object.h"
#include "symbol.h"
#include "function.h"
#include "namespace.h"  // For CljNamespace and EvalState types

/**
 * @brief Register a macro in the current namespace
 * @param ns Namespace to register macro in
 * @param name Symbol name of the macro
 * @param macro_fn Function object (with :macro true metadata)
 */
void register_macro(CljNamespace *ns, CljSymbol *name, CljFunction *macro_fn);

/**
 * @brief Look up a macro by symbol in the given namespace
 * @param ns Namespace to search
 * @param name Symbol name to look up
 * @return Macro function or NULL if not found
 */
CljFunction* lookup_macro(CljNamespace *ns, CljSymbol *name);

/**
 * @brief Look up a macro by symbol in the current namespace and clojure.core
 * @param st EvalState with current namespace
 * @param name Symbol name to look up
 * @return Macro function or NULL if not found
 */
CljFunction* lookup_macro_resolve(EvalState *st, CljSymbol *name);

/**
 * @brief Expand a macro form (delegates to Clojure macroexpand after bootstrap)
 * @param form Form to expand
 * @param st EvalState
 * @return Expanded form (may be same as input if not a macro call)
 *
 * During bootstrap (before clojure.core is loaded), returns form unchanged.
 * After bootstrap, delegates to Clojure's macroexpand function.
 */
CljObject* macro_expand(CljObject *form, EvalState *st);

/**
 * @brief Check if a form is a macro call
 * @param form Form to check
 * @param st EvalState
 * @return true if form is a list starting with a macro symbol
 */
bool is_macro_call(CljObject *form, EvalState *st);

#endif // TINY_CLJ_MACRO_H

