/**
 * @file macro.c
 * @brief Macro registry and expansion infrastructure
 *
 * Minimal C implementation for:
 * - Macro storage/lookup in namespaces
 * - Delegation to Clojure macroexpand after bootstrap
 */

#include "macro.h"
#include "namespace.h"
#include "map.h"
#include "list.h"
#include "memory.h"
#include "eval.h"

// Cached reference to Clojure macroexpand function (set after bootstrap)
static CljFunction *g_macroexpand_fn = NULL;

void register_macro(CljNamespace *ns, CljSymbol *name, CljFunction *macro_fn) {
    if (!ns || !name || !macro_fn) return;

    // Initialize macro_mappings if NULL
    if (!ns->macro_mappings) {
        ns->macro_mappings = make_map(16);
        RETAIN(ns->macro_mappings);
    }

    // Store macro in namespace's macro registry
    CljMap *new_mappings = map_assoc(ns->macro_mappings, (CljObject*)name, (CljObject*)macro_fn);
    if (new_mappings != ns->macro_mappings) {
        RELEASE(ns->macro_mappings);
        ns->macro_mappings = new_mappings;
        RETAIN(ns->macro_mappings);
    }
}

CljFunction* lookup_macro(CljNamespace *ns, CljSymbol *name) {
    if (!ns || !name || !ns->macro_mappings) return NULL;

    CljObject *found = map_get(ns->macro_mappings, (CljObject*)name, NULL);
    // Macros are CLJ_CLOSURE (interpreted functions), not CLJ_FUNC (native functions)
    if (found && TAG(found) == CLJ_CLOSURE) {
        return as_function(found);
    }
    return NULL;
}

CljFunction* lookup_macro_resolve(EvalState *st, CljSymbol *name) {
    if (!st || !name) return NULL;

    // First check current namespace
    if (st->current_ns) {
        CljFunction *macro = lookup_macro(st->current_ns, name);
        if (macro) return macro;
    }

    // Then check clojure.core
    CljNamespace *core_ns = ns_find("clojure.core");
    if (core_ns && core_ns != st->current_ns) {
        CljFunction *macro = lookup_macro(core_ns, name);
        if (macro) return macro;
    }

    // Also check user namespace (for macroexpand-1 called from clojure.core)
    // TODO: This is a workaround - proper fix needs dynamic binding of *ns*
    CljNamespace *user_ns = ns_find("user");
    if (user_ns && user_ns != st->current_ns && user_ns != core_ns) {
        CljFunction *macro = lookup_macro(user_ns, name);
        if (macro) return macro;
    }

    return NULL;
}

bool is_macro_call(CljObject *form, EvalState *st) {
    if (!form || !list_type_matches(TAG(form))) return false;

    CljList *list = as_list(form);
    if (!list || !list->first) return false;

    if (TAG(list->first) != CLJ_SYMBOL) return false;

    CljSymbol *sym = as_symbol(list->first);
    return lookup_macro_resolve(st, sym) != NULL;
}

CljObject* macro_expand(CljObject *form, EvalState *st) {
    if (!form || !st) return form;

    // During bootstrap, macroexpand is not yet available - return form unchanged
    if (!g_macroexpand_fn) {
        // Try to resolve macroexpand from clojure.core
        CljSymbol *macroexpand_sym = intern_symbol_global("macroexpand");
        if (macroexpand_sym) {
            CljObject *resolved = ns_resolve(st, macroexpand_sym);
            if (resolved && TAG(resolved) == CLJ_FUNC) {
                g_macroexpand_fn = as_function(resolved);
            }
        }

        // Still not available - bootstrap mode, no expansion
        if (!g_macroexpand_fn) {
            return form;
        }
    }

    // Delegate to Clojure macroexpand
    ID args[] = { form };
    ID result = eval_function_call((CljObject*)g_macroexpand_fn, args, 1, NULL, st);
    return result ? result : form;
}

