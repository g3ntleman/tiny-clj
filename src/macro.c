/**
 * @file macro.c
 * @brief Macro registry and lookup
 *
 * Minimal C implementation for macro storage/lookup in namespaces.
 * Macro expansion is done at compile-time in ast_canon.c.
 */

#include "macro.h"
#include "map.h"
#include "memory.h"

// Internal: Look up a macro by symbol in the given namespace
static CljFunction* lookup_macro(CljNamespace *ns, CljSymbol *name) {
    if (!ns || !name || !ns->macro_mappings) return NULL;

    CljObject *found = map_get(ns->macro_mappings, (CljObject*)name, NULL);
    // Macros are CLJ_CLOSURE (interpreted functions), not CLJ_FUNC (native)
    return (found && TAG(found) == CLJ_CLOSURE) ? as_function(found) : NULL;
}

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
        ASSIGN(ns->macro_mappings, new_mappings);
    }
}

CljFunction* lookup_macro_resolve(EvalState *st, CljSymbol *name) {
    if (!st || !name) return NULL;

    // Check current namespace first
    CljFunction *macro = lookup_macro(st->current_ns, name);
    if (macro) return macro;

    // Then check clojure.core
    CljNamespace *core_ns = ns_find("clojure.core");
    if (core_ns && core_ns != st->current_ns) {
        macro = lookup_macro(core_ns, name);
        if (macro) return macro;
    }

    // Also check user namespace (workaround for *ns* not being dynamically bound)
    // TODO: Remove when dynamic vars are implemented
    CljNamespace *user_ns = ns_find("user");
    if (user_ns && user_ns != st->current_ns && user_ns != core_ns) {
        macro = lookup_macro(user_ns, name);
        if (macro) return macro;
    }

    return NULL;
}
