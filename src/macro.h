/**
 * @file macro.h
 * @brief Macro registry and lookup
 *
 * Provides functions for registering and looking up macros in namespaces.
 * Macro expansion is done inline in eval.c using Clojure's macroexpand-1.
 */

#ifndef TINY_CLJ_MACRO_H
#define TINY_CLJ_MACRO_H

#include "symbol.h"
#include "function.h"
#include "namespace.h"

/**
 * @brief Register a macro in a namespace
 * @param ns Namespace to register macro in
 * @param name Symbol name of the macro
 * @param macro_fn Closure function (with :macro true metadata)
 */
void register_macro(CljNamespace *ns, CljSymbol *name, CljFunction *macro_fn);

/**
 * @brief Look up a macro in current namespace, clojure.core, and user namespace
 * @param st EvalState with current namespace
 * @param name Symbol name to look up
 * @return Macro function or NULL if not found
 */
CljFunction* lookup_macro_resolve(EvalState *st, CljSymbol *name);

#endif // TINY_CLJ_MACRO_H
