#include <stdlib.h>
#include <string.h>
#include "common.h"  // For CLJ_ASSERT
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "object.h"
#include "map.h"
#include "list.h"
#include "function_call.h"
#include "exception.h"
#include "runtime.h"
#include "tiny_clj.h"
#include "memory.h"
#include "parser.h"  // For eval_parsed

// Symbol resolution cache - uses array-map for DRY principle
// Cache size: 16 entries (good balance between hit rate and memory usage)
#define RESOLVE_CACHE_SIZE 16
static CljMap *g_resolve_cache = NULL;

// Function to reset resolve cache (for test isolation)
void ns_reset_resolve_cache(void) {
    if (g_resolve_cache) {
        RELEASE(g_resolve_cache);
        g_resolve_cache = NULL;
    }
}

CljNamespace* ns_get_or_create(const char *name, const char *file) {
    if (!name) return NULL;
    
    // First, look for an existing namespace
    CljNamespace *cur = g_runtime.ns_registry;
    while (cur) {
        if (cur->name && cur->name->name && strcmp(cur->name->name, name) == 0) {
            return cur;
        }
        cur = cur->next;
    }

    // Create a new namespace
    CljNamespace *ns = (CljNamespace*)malloc(sizeof(CljNamespace));
    if (!ns) return NULL;
    
    ns->name = intern_symbol(NULL, name);
    ns->mappings = make_map(64); // Increased capacity for clojure.core
    ns->aliases = make_map(16);
    ns->filename = file ? strdup(file) : NULL;
    ns->next = g_runtime.ns_registry;
    g_runtime.ns_registry = ns;
    
    // Cache clojure.core for priority lookup (fast symbol pointer comparison)
    // Note: CljNamespace is a plain C struct (not CljObject), so direct assignment is correct
    if (ns->name == SYM_CLOJURE_CORE) {
        g_runtime.clojure_core_cache = ns;
    }
    
    return ns;
}

ID ns_resolve(EvalState *st, CljSymbol *sym) {
    CLJ_ASSERT(sym != NULL);
    
    // Use default namespace if st is NULL (eliminates need for temporary EvalState instances)
    CljNamespace *current_ns = st ? st->current_ns : ns_get_or_create("user", NULL);
    if (!current_ns) {
        return NULL;
    }
    
    // Initialize cache on first use (DRY: reuse array-map)
    if (!g_resolve_cache) {
        g_resolve_cache = make_map(RESOLVE_CACHE_SIZE);
    }
    
    // CRITICAL: Always check current namespace first (before cache)
    // This ensures that redefined symbols in current namespace take precedence over cached values
    ID v = map_get((CljValue)current_ns->mappings, sym, NULL);
    if (v) {
        // Found in current namespace - update cache and return
        // CRITICAL: map_assoc may return a new map (COW), so we must use the result
        if (g_resolve_cache) {
            ID updated_cache = map_assoc((CljValue)g_resolve_cache, (CljValue)sym, (CljValue)v);
            ASSIGN(g_resolve_cache, updated_cache);
        }
        return v;
    }
    
    // Not in current namespace - check cache (fast path for repeated lookups)
    if (g_resolve_cache) {
        ID cached = map_get((CljValue)g_resolve_cache, (CljValue)sym, NULL);
        if (cached) {
            return cached;
        }
    }

    // Search clojure.core first (most common)
    if (g_runtime.clojure_core_cache && g_runtime.clojure_core_cache->mappings) {
        v = (CljObject*)map_get((CljValue)g_runtime.clojure_core_cache->mappings, (CljValue)sym, NULL);
        if (v) {
            // Cache the result
            // CRITICAL: map_assoc may return a new map (COW), so we must use the result
            ID updated_cache = map_assoc((CljValue)g_resolve_cache, (CljValue)sym, (CljValue)v);
            ASSIGN(g_resolve_cache, updated_cache);
            return v;
        }
    }
    
    // Search other namespaces (excluding clojure.core to avoid double search)
    CljNamespace *cur = g_runtime.ns_registry;
    while (cur) {
        if (cur != g_runtime.clojure_core_cache && cur->mappings) {
            v = (CljObject*)map_get((CljValue)cur->mappings, (CljValue)sym, NULL);
            if (v) {
                // Cache the result
                (void)map_assoc((CljValue)g_resolve_cache, (CljValue)sym, (CljValue)v);
                return (ID)v;
            }
        }
        cur = cur->next;
    }
    
    // Symbol not found - don't cache NULL values (would waste cache space)
    return NULL;
}

CljNamespace* ns_load_file(EvalState *st, const char *ns_name, const char *filename) {
    (void)st;
    if (!ns_name) return NULL;
    
    CljNamespace *ns = ns_get_or_create(ns_name, filename);
    if (!ns) return NULL;
    
    // TODO: Parse file and add definitions to namespace mappings
    
    return ns;
}

void ns_register(CljNamespace *ns) {
    if (!ns) return;
    
    // Check if namespace is already registered
    CljNamespace *cur = g_runtime.ns_registry;
    while (cur) {
        if (cur == ns) return; // Already registered
        cur = cur->next;
    }
    
    // Add namespace to registry
    ns->next = g_runtime.ns_registry;
    g_runtime.ns_registry = ns;
}

CljNamespace* ns_find(const char *name) {
    if (!name) return NULL;
    
    CljNamespace *cur = g_runtime.ns_registry;
    while (cur) {
        if (cur->name && cur->name->name && strcmp(cur->name->name, name) == 0) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

void ns_cleanup() {
    // Cleanup all namespaces (caches will be automatically rebuilt when needed)
    CljNamespace *cur = g_runtime.ns_registry;
    while (cur) {
        CljNamespace *next = cur->next;
        if (cur->filename) free((void*)cur->filename);
        if (cur->mappings) RELEASE(cur->mappings);
        if (cur->aliases) RELEASE(cur->aliases);
        free(cur);
        cur = next;
    }
    g_runtime.ns_registry = NULL;
    
    // Reset cache (will be automatically rebuilt when needed)
    g_runtime.clojure_core_cache = NULL;
}

// EvalState functions
EvalState* evalstate_new(bool load_core) {
    EvalState *st = (EvalState*)malloc(sizeof(EvalState));
    if (!st) {
        throw_exception(EXCEPTION_RUNTIME, "Failed to allocate EvalState", NULL, 0, 0);
        return NULL; // Never reached, but compiler doesn't know
    }
    
    memset(st, 0, sizeof(EvalState));
    st->pool = NULL; // No longer needed - use global pools instead
    
    st->current_ns = ns_get_or_create("user", NULL); // Default namespace
    if (!st->current_ns) {
        free(st);
        throw_exception(EXCEPTION_RUNTIME, "Failed to create user namespace", NULL, 0, 0);
        return NULL; // Never reached, but compiler doesn't know
    }
    
    st->file = NULL;
    st->line = 0;
    st->col = 0;
    
    // Load clojure.core automatically if requested (functions available via ns_resolve)
    if (load_core) {
        WITH_AUTORELEASE_POOL({
            load_clojure_core(st);
        });
        
        // Ensure current_ns is "user" (not clojure.core)
        evalstate_set_ns(st, "user");
    }
    
    return st;
}

void evalstate_free(EvalState *st) {
    if (!st) return;
    
    // Release any objects stored in EvalState
    // Note: expr and result are typically autoreleased, but we should clean them up
    // to prevent objects from leaking between tests
    if (st->expr && !IS_IMMEDIATE((CljValue)st->expr)) {
        RELEASE(st->expr);
        st->expr = NULL;
    }
    if (st->result && !IS_IMMEDIATE((CljValue)st->result)) {
        RELEASE(st->result);
        st->result = NULL;
    }
    
    if (st->stack) free(st->stack);
    free(st);
}

void evalstate_set_ns(EvalState *st, const char *ns_name) {
    if (!st || !ns_name) return;
    
    // CRITICAL: Check cache first for clojure.core (fast path)
    // This ensures we always use the cached namespace if it exists
    if (strcmp(ns_name, "clojure.core") == 0 && g_runtime.clojure_core_cache) {
        st->current_ns = g_runtime.clojure_core_cache;
        return;
    }
    
    // Get or create namespace
    CljNamespace *ns = ns_find(ns_name);
    if (!ns) {
        ns = ns_get_or_create(ns_name, NULL);
    }
    
    if (ns) {
        st->current_ns = ns;
        // CRITICAL: If this is clojure.core, ensure cache is set
        // This handles the case where ns_get_or_create just created it
        if (ns->name == SYM_CLOJURE_CORE && !g_runtime.clojure_core_cache) {
            g_runtime.clojure_core_cache = ns;
        }
    }
}

void evalstate_reset(EvalState **st_ptr, bool load_core) {
    if (!st_ptr) return;
    
    // Free existing eval state if present
    if (*st_ptr) {
        evalstate_free(*st_ptr);
        *st_ptr = NULL;
    }
    
    // Create new eval state
    *st_ptr = evalstate_new(load_core);
    
    if (!*st_ptr) return;
    
    // Ensure clojure.core is loaded if requested
    if (load_core) {
        bool needs_reload = false;
        if (!g_runtime.clojure_core_cache) {
            needs_reload = true;
        } else {
            CljNamespace *clojure_core = g_runtime.clojure_core_cache;
            if (!clojure_core || !clojure_core->mappings) {
                needs_reload = true;
            } else {
                CljSymbol *inc_sym = intern_symbol_global("inc");
                if (inc_sym) {
                    CljObject *inc_value = (CljObject*)map_get((CljMap*)clojure_core->mappings, (CljValue)inc_sym, NULL);
                    if (!inc_value) {
                        needs_reload = true;
                    }
                }
            }
        }
        
        if (needs_reload) {
            evalstate_set_ns(*st_ptr, "clojure.core");
            load_clojure_core(*st_ptr);
        }
    }
    
    // Reset all fields
    (*st_ptr)->expr = NULL;
    (*st_ptr)->result = NULL;
    (*st_ptr)->pc = 0;
    (*st_ptr)->step_budget = 0;
    (*st_ptr)->sp = 0;
    (*st_ptr)->finished = 0;
    (*st_ptr)->file = NULL;
    (*st_ptr)->line = 0;
    (*st_ptr)->col = 0;
    (*st_ptr)->current_ns = NULL;
    
    // Reset user namespace for isolation
    CljNamespace *user_ns = ns_find("user");
    if (user_ns && user_ns != g_runtime.clojure_core_cache) {
        if (user_ns->mappings) {
            RELEASE(user_ns->mappings);
            user_ns->mappings = make_map(16);
        }
    }
    
    // Reset symbol resolution cache
    ns_reset_resolve_cache();
    
    // Set current_ns to "user"
    evalstate_set_ns(*st_ptr, "user");
}

// Exception handling
void eval_error(const char *msg, EvalState *st) {
    if (!st) return;
    
    // Use throw_exception which handles the exception_stack correctly
    throw_exception(EXCEPTION_RUNTIME, msg, st->file, st->line, st->col);
}

void parse_error(const char *msg, EvalState *st) {
    if (!st) return;
    
    // Use throw_exception which handles the exception_stack correctly
    throw_exception(EXCEPTION_PARSE, msg, st->file, st->line, st->col);
}


// Try/Catch-Implementierung using TRY/CATCH macros
CljObject* eval_try(CljObject *form, EvalState *st) {
    if (!form || form->type != CLJ_LIST) return NULL;
    
    CljObject *result = NULL;
    
    TRY {
        // normaler Body (zweites Element)
        CljObject *body = (CljObject*)list_nth(as_list(form), 1);
        result = (CljObject*)eval_parsed(body, st, NULL);
    } CATCH(ex) {
        // We arrived here via eval_error
        // Search for catch clauses
        for (int i = 2; i < list_count(as_list(form)); i++) {
            CljObject *clause = (CljObject*)list_nth(as_list(form), i);
            if (is_list(clause) && is_symbol(LIST_FIRST(as_list(clause)), "catch")) {
                CljObject *sym = (CljObject*)list_nth(as_list(clause), 1);
                CljObject *body = (CljObject*)list_nth(as_list(clause), 2);
                
                // Bind variable (sym = err) - simplified
                // CRITICAL: map_assoc may return a new map (COW), so we must use the result
                CljMap *updated_mappings = map_assoc((CljMap*)st->current_ns->mappings, (ID)sym, (ID)ex);
                ASSIGN(st->current_ns->mappings, (CljObject*)updated_mappings);
                result = (CljObject*)eval_parsed(body, st, NULL);
                return result;
            }
        }
        // No catch clause found - re-throw (handler is already popped!)
        throw_exception(strlen(ex->type) > 0 ? ex->type : "Error", 
                       strlen(ex->message) > 0 ? ex->message : "Unknown error",
                       ex->file, ex->line, ex->col);
    } END_TRY
    
    return result;
}

CljObject* eval_catch(CljObject *form, EvalState *st) {
    // Vereinfachte catch-Implementierung
    return eval_try(form, st);
}


/**
 * @brief Define a symbol in the current namespace
 * @param st Evaluation state
 * @param symbol Symbol to define
 * @param value Value to bind to symbol
 */
void ns_define(CljNamespace *ns, ID symbol, ID value) {
    // Allow NULL value (nil) - it's a legitimate case
    // Only check for NULL ns and symbol
    if (!ns || !symbol) return;
    
    // Create or update mappings
    if (!ns->mappings) {
        ns->mappings = make_map(16);
    }
    
    // Store symbol-value binding (overwrites existing)
    // NOTE: map_assoc() already does RETAIN(value) and RETAIN(symbol) internally
    // See src/map.c:98 and src/map.c:106-107
    // CRITICAL: map_assoc may return a new map (COW), so we must update ns->mappings
    CljMap *new_mappings = map_assoc(ns->mappings, (ID)symbol, (ID)value);
    if (new_mappings != ns->mappings) {
        // Map was copied (COW) - update reference
        RELEASE(ns->mappings);
        ns->mappings = new_mappings;
        // new_mappings is already retained by map_assoc
    }
    // If new_mappings == ns->mappings, it was in-place mutation (RC=1), no update needed
    
    // CRITICAL: Invalidate resolve cache when a symbol is redefined in the current namespace
    // This ensures that ns_resolve will find the new definition instead of returning cached value
    // CRITICAL: map_assoc may return a new map (COW), so we must use the result
    if (g_resolve_cache) {
        // Remove the symbol from cache to force re-resolution
        ID updated_cache = map_assoc((CljValue)g_resolve_cache, (CljValue)symbol, NULL);
        ASSIGN(g_resolve_cache, updated_cache);
    }
}

/**
 * @brief Get namespace name for an alias
 * @param ns Namespace to search in
 * @param alias Alias symbol to look up
 * @return Namespace name symbol or NULL if not found
 */
CljObject* ns_get_alias(CljNamespace *ns, CljObject *alias) {
    if (!ns || !alias || !ns->aliases) return NULL;
    
    // Look up alias in aliases map
    CljObject *ns_name = (CljObject*)map_get((CljValue)ns->aliases, (CljValue)alias, NULL);
    return ns_name;
}

/**
 * @brief Set namespace alias
 * @param ns Namespace to set alias in
 * @param alias Alias symbol
 * @param ns_name Namespace name symbol
 */
void ns_set_alias(CljNamespace *ns, CljObject *alias, CljObject *ns_name) {
    if (!ns || !alias || !ns_name) return;
    
    // Create or update aliases map
    if (!ns->aliases) {
        ns->aliases = make_map(16);
    }
    
    // Store alias-namespace binding (overwrites existing)
    // NOTE: map_assoc() already does RETAIN(ns_name) and RETAIN(alias) internally
    // See src/map.c:98 and src/map.c:106-107
    // CRITICAL: map_assoc may return a new map (COW), so we must update ns->aliases
    CljMap *new_aliases = map_assoc(ns->aliases, (ID)alias, (ID)ns_name);
    if (new_aliases != ns->aliases) {
        // Map was copied (COW) - update reference
        RELEASE((ID)ns->aliases);
        ns->aliases = new_aliases;
        // new_aliases is already retained by map_assoc
    }
    // If new_aliases == ns->aliases, it was in-place mutation (RC=1), no update needed
}
