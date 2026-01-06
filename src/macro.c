/**
 * @file macro.c
 * @brief Macro registry and lookup
 *
 * Minimal C implementation for macro storage/lookup in namespaces.
 * Macro expansion is done at compile-time in ast_canon.c.
 */

#include "macro.h"
#include <subjective-c/map.h>
#include "memory.h"

// Internal: Look up a macro by symbol in the given namespace
static CljFunction* lookup_macro(CljNamespace *ns, CljSymbol *name) {
    if (!ns || !name || !ns->macro_mappings) return NULL;

    CljObject *found = map_get_sentinel(ns->macro_mappings, (CljObject*)name, NULL);
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
    ASSIGN(ns->macro_mappings, map_assoc(ns->macro_mappings, (CljObject*)name, (CljObject*)macro_fn));
}

// Cached namespace pointers for faster lookup
static CljNamespace *g_cached_core_ns = NULL;
static CljNamespace *g_cached_user_ns = NULL;

// Reset cached namespace pointers (called by runtime_reset)
void macro_cache_reset(void) {
    g_cached_core_ns = NULL;
    g_cached_user_ns = NULL;
}

CljFunction* lookup_macro_resolve(EvalState *st, CljSymbol *name) {
    if (!st || !name) return NULL;

    // Check current namespace first (fast path - no lookup needed)
    CljFunction *macro = lookup_macro(st->current_ns, name);
    if (macro) return macro;

    // Then check clojure.core (use cached pointer)
    if (!g_cached_core_ns) {
        g_cached_core_ns = ns_find_by_symbol(SYM_CLOJURE_CORE);
    }
    if (g_cached_core_ns && g_cached_core_ns != st->current_ns) {
        macro = lookup_macro(g_cached_core_ns, name);
        if (macro) return macro;
    }

    // Also check user namespace (use cached pointer)
    // TODO: Remove when dynamic vars are implemented
    if (!g_cached_user_ns) {
        g_cached_user_ns = ns_find("user");
    }
    if (g_cached_user_ns && g_cached_user_ns != st->current_ns && g_cached_user_ns != g_cached_core_ns) {
        macro = lookup_macro(g_cached_user_ns, name);
        if (macro) return macro;
    }

    return NULL;
}
