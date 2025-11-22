#include <stdlib.h>
#include <string.h>
#include <stdio.h>  // For fprintf in DEBUG mode
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
#include "kv_macros.h"  // For KV_KEY, KV_VALUE

// Symbol resolution cache - uses array-map for DRY principle
// Cache size: 16 entries (good balance between hit rate and memory usage)
#define RESOLVE_CACHE_SIZE 16

// Helper context for namespace search in ns_resolve()
struct ns_search_ctx {
    CljSymbol *sym;
    CljObject *result;
    CljNamespace *clojure_core_cache;
};

// Global context pointer for callback (thread-local would be better, but this works for single-threaded)
static struct ns_search_ctx *g_ns_search_ctx = NULL;

// Helper function for map_foreach to search namespaces
static void search_namespace_callback(ID key, ID value) {
    (void)key; // Unused - we only need the value (namespace)
    if (!g_ns_search_ctx) return;
    
    CljNamespace *ns = (CljNamespace*)value;
    if (ns && ns != g_ns_search_ctx->clojure_core_cache && ns->mappings) {
        CljObject *found = (CljObject*)map_get(ns->mappings, g_ns_search_ctx->sym, NULL);
        if (found && !g_ns_search_ctx->result) {
            g_ns_search_ctx->result = found;
        }
    }
}

// Helper context for namespace cleanup in ns_cleanup()
// (no context needed, just release each namespace)
static void release_namespace_callback(ID key, ID value) {
    (void)key; // Unused - we only need the value (namespace)
    CljNamespace *ns = (CljNamespace*)value;
    if (ns) {
        // Release namespace object (CljObject subtype)
        // release_object_deep() will handle freeing filename, mappings, and aliases
        RELEASE((CljObject*)ns);
    }
}

// Helper function to grow transient map when capacity is exceeded
// Returns new transient map with doubled capacity, or NULL on error
static CljMap* grow_transient_map(CljMap *old_map) {
    if (!old_map) return NULL;
    
    // Calculate new capacity (double the old capacity, minimum 4)
    int new_capacity = old_map->capacity * 2;
    if (new_capacity < 4) new_capacity = 4;
    
    // Allocate new map with larger capacity
    size_t struct_size = sizeof(CljMap);
    size_t data_size = (size_t)new_capacity * 2 * sizeof(CljObject*);
    CljMap *new_map = (CljMap*)malloc(struct_size + data_size);
    if (!new_map) return NULL;
    
    // Initialize new transient map
    new_map->base.type = CLJ_MAP_TRANSIENT;
    new_map->base.rc = 1;
    new_map->count = 0;
    new_map->capacity = new_capacity;
    
    // Initialize data array
    for (int i = 0; i < new_capacity * 2; i++) {
        new_map->data[i] = NULL;
    }
    
    // Copy all existing entries using map_conj (handles RETAIN automatically)
    for (int i = 0; i < old_map->count; i++) {
        CljObject *key = KV_KEY(old_map->data, i);
        CljObject *value = KV_VALUE(old_map->data, i);
        if (key) {
            map_conj(new_map, key, value);
        }
    }
    
    return new_map;
}


/** Initialize namespace registry if not already initialized.
 * This is a helper function to ensure the registry exists before use.
 * Follows DRY principle - used by ns_get_or_create(), ns_register(), and runtime_init().
 * 
 * @return 0 on success, -1 on error
 */
static int ns_init_registry(void) {
    if (g_runtime.ns_registry) {
        return 0; // Already initialized
    }
    
    CljMap *ns_map = make_map(16);
    if (!ns_map) {
        return -1; // OOM
    }
    
    CljMap *transient_map = map_transient(ns_map);
    RELEASE(ns_map); // map_transient() retains the result
    g_runtime.ns_registry = transient_map;
    
    return 0;
}

/** Reset namespace registry (cleanup and reinitialize).
 * This is used by runtime_init() to reset the registry.
 * 
 * @return 0 on success, -1 on error
 */
int ns_reset_registry(void) {
    // Cleanup existing registry if present
    if (g_runtime.ns_registry) {
        RELEASE(g_runtime.ns_registry);
        g_runtime.ns_registry = NULL;
    }
    
    // Reset cache (will be automatically rebuilt when needed)
    g_runtime.clojure_core_cache = NULL;
    
    // Initialize new registry
    return ns_init_registry();
}

/** Create a new namespace object (does not register it in the registry).
 * This is the low-level constructor for namespaces, following the pattern
 * of make_map(), make_symbol(), etc.
 * 
 * @param name Namespace name (must not be NULL)
 * @param file Optional filename associated with the namespace (can be NULL)
 * @return New namespace object with rc=0, or NULL on error
 */
CljNamespace* make_namespace(const char *name, const char *file) {
    if (!name) return NULL;
    
    // Create a new namespace using ALLOC (initializes base.type and base.rc)
    CljNamespace *ns = ALLOC(CljNamespace, 1);
    if (!ns) return NULL;
    
    // ALLOC already sets base.type = CLJ_NAMESPACE and base.rc = 0
    
    // Get or intern the namespace name symbol
    CljSymbol *name_symbol = intern_symbol(NULL, name);
    if (!name_symbol) {
        // If intern_symbol fails, we need to free the namespace
        // But ALLOC doesn't allocate memory that needs freeing, so we just return NULL
        // Actually, ALLOC uses malloc, so we need to free it
        free(ns);
        return NULL;
    }
    
    ns->name = name_symbol; // Use the interned symbol
    ns->mappings = make_map(64); // Increased capacity for clojure.core
    if (!ns->mappings) {
        free(ns);
        return NULL;
    }
    
    ns->aliases = make_map(16);
    if (!ns->aliases) {
        RELEASE(ns->mappings);
        free(ns);
        return NULL;
    }
    
    ns->filename = file ? strdup(file) : NULL;
    if (file && !ns->filename) {
        // strdup failed - OOM
        RELEASE(ns->mappings);
        RELEASE(ns->aliases);
        free(ns);
        return NULL;
    }
    
    return ns;
}

CljNamespace* ns_get_or_create(const char *name, const char *file) {
    if (!name) return NULL;
    
    // Ensure registry is initialized (DRY: use helper function)
    if (ns_init_registry() != 0) {
        return NULL; // Failed to initialize registry
    }
    
    // Look up namespace by name in the map
    CljSymbol *name_symbol = intern_symbol(NULL, name);
    if (!name_symbol) return NULL;
    
    CljObject *ns_obj = map_get(g_runtime.ns_registry, name_symbol, NULL);
    if (ns_obj) {
        return (CljNamespace*)ns_obj;
    }

    // Create a new namespace using make_namespace (DRY principle)
    CljNamespace *ns = make_namespace(name, file);
    if (!ns) return NULL;
    
    // Add namespace to registry map (Key: name_symbol, Value: ns)
    // map_conj() retains the value, so we need to retain ns before adding
    RETAIN((CljObject*)ns);
    CljMap *new_registry = map_conj(g_runtime.ns_registry, name_symbol, ns);
    if (!new_registry) {
        // Capacity exceeded - grow the map
        CljMap *grown_map = grow_transient_map(g_runtime.ns_registry);
        if (grown_map) {
            RELEASE(g_runtime.ns_registry);
            g_runtime.ns_registry = grown_map;
            // Try again with the grown map
            new_registry = map_conj(g_runtime.ns_registry, name_symbol, ns);
            if (!new_registry) {
                // Still failed - this should not happen, but handle gracefully
                RELEASE((CljObject*)ns);
                return NULL;
            }
        } else {
            // Failed to grow map - OOM
            RELEASE((CljObject*)ns);
            return NULL;
        }
    }
    g_runtime.ns_registry = new_registry;
    
    // Cache clojure.core for priority lookup (fast symbol pointer comparison)
    // Note: CljNamespace is now a CljObject subtype, but we just store a pointer here
    // The namespace is retained when added to the registry, so no additional retain needed
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
    
    // Always check current namespace first (before cache)
    // This ensures that redefined symbols in current namespace take precedence over cached values
    // Use sentinel to distinguish "key not found" from "value is nil"
    // In Clojure, nil is a valid value, so we need to distinguish between
    // "symbol not found" (should search other namespaces) and "symbol found with nil value" (should return nil)
    static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID v = map_get(current_ns->mappings, sym, (ID)&not_found_sentinel);
    if (v != (ID)&not_found_sentinel) {
        // Found in current namespace (value can be NULL/nil, which is valid)
        // Update cache (map_assoc may return a new map due to COW, so we must use the result)
        if (g_runtime.resolve_cache) {
            ID updated_cache = map_assoc(g_runtime.resolve_cache, sym, v);
            ASSIGN(g_runtime.resolve_cache, updated_cache);
        }
        return v;
    }
    
    // Not in current namespace - check cache (fast path for repeated lookups)
    if (g_runtime.resolve_cache) {
        ID cached = map_get(g_runtime.resolve_cache, sym, NULL);
        if (cached) {
            return cached;
        }
    }

    // Search clojure.core first (most common)
    if (g_runtime.clojure_core_cache && g_runtime.clojure_core_cache->mappings) {
        v = (CljObject*)map_get(g_runtime.clojure_core_cache->mappings, sym, NULL);
        if (v) {
            // Cache the result (map_assoc may return a new map due to COW, so we must use the result)
            if (g_runtime.resolve_cache) {
                ID updated_cache = map_assoc(g_runtime.resolve_cache, sym, v);
                ASSIGN(g_runtime.resolve_cache, updated_cache);
            }
            return v;
        }
    }
    
    // Search other namespaces (excluding clojure.core to avoid double search)
    // Iterate over all namespaces in the registry map
    if (g_runtime.ns_registry) {
        // Use static context for callback (thread-local would be better, but this works for single-threaded)
        static struct ns_search_ctx search_ctx;
        search_ctx.sym = sym;
        search_ctx.result = NULL;
        search_ctx.clojure_core_cache = g_runtime.clojure_core_cache;
        
        // Temporarily set global context pointer for callback
        struct ns_search_ctx *old_ctx = g_ns_search_ctx;
        g_ns_search_ctx = &search_ctx;
        
        map_foreach(g_runtime.ns_registry, search_namespace_callback);
        
        // Restore old context
        g_ns_search_ctx = old_ctx;
        
        if (search_ctx.result) {
            v = search_ctx.result;
            // Cache the result
            if (g_runtime.resolve_cache) {
                ID updated_cache = map_assoc(g_runtime.resolve_cache, sym, v);
                ASSIGN(g_runtime.resolve_cache, updated_cache);
            }
            return v;
        }
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
    if (!ns || !ns->name) return;
    
    // Ensure registry is initialized (DRY: use helper function)
    if (ns_init_registry() != 0) {
        return; // Failed to initialize registry
    }
    
    // Check if namespace is already registered
    CljObject *existing = map_get(g_runtime.ns_registry, ns->name, NULL);
    if (existing == (CljObject*)ns) {
        return; // Already registered
    }
    
    // Add namespace to registry map (Key: ns->name, Value: ns)
    // map_conj() retains the value, so we need to retain ns before adding
    RETAIN((CljObject*)ns);
    CljMap *new_registry = map_conj(g_runtime.ns_registry, ns->name, ns);
    if (!new_registry) {
        // Capacity exceeded - grow the map
        CljMap *grown_map = grow_transient_map(g_runtime.ns_registry);
        if (grown_map) {
            RELEASE(g_runtime.ns_registry);
            g_runtime.ns_registry = grown_map;
            // Try again with the grown map
            new_registry = map_conj(g_runtime.ns_registry, ns->name, ns);
            if (!new_registry) {
                // Still failed - this should not happen, but handle gracefully
                RELEASE((CljObject*)ns);
                return;
            }
        } else {
            // Failed to grow map - OOM
            RELEASE((CljObject*)ns);
            return;
        }
    }
    g_runtime.ns_registry = new_registry;
}

CljNamespace* ns_find(const char *name) {
    if (!name || !g_runtime.ns_registry) return NULL;
    
    // Look up namespace by name in the map
    CljSymbol *name_symbol = intern_symbol(NULL, name);
    if (!name_symbol) return NULL;
    
    CljObject *ns_obj = map_get(g_runtime.ns_registry, name_symbol, NULL);
    return (CljNamespace*)ns_obj;
}

void ns_cleanup() {
    // Cleanup all namespaces (caches will be automatically rebuilt when needed)
    if (g_runtime.ns_registry) {
        // Iterate over all namespaces and release them
        map_foreach(g_runtime.ns_registry, release_namespace_callback);
        
        // Release the registry map itself
        RELEASE(g_runtime.ns_registry);
        g_runtime.ns_registry = NULL;
    }
    
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
    if (st->expr && !IS_IMMEDIATE(st->expr)) {
        RELEASE(st->expr);
        st->expr = NULL;
    }
    if (st->result && !IS_IMMEDIATE(st->result)) {
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
                    CljObject *inc_value = (CljObject*)map_get(clojure_core->mappings, inc_sym, NULL);
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
    
    // Reset user namespace for isolation
    // evalstate_new already created a new user namespace, but we need to ensure it's clean
    // (no definitions from previous tests). We must reset it AFTER evalstate_new creates it.
    // Get user namespace - it should exist because evalstate_new creates it
    // But load_clojure_core may have changed current_ns to clojure.core, so we need to find it explicitly
    CljNamespace *user_ns = ns_find("user");
    if (!user_ns) {
        // If not found, create it explicitly (shouldn't happen, but handle it)
        user_ns = ns_get_or_create("user", NULL);
    }
    // Always reset user namespace mappings to ensure test isolation
    // This clears any definitions from previous tests (e.g., helper, main from test_recur)
    if (user_ns && user_ns != g_runtime.clojure_core_cache) {
        if (user_ns->mappings) {
            RELEASE(user_ns->mappings);
            user_ns->mappings = make_map(16);
        }
    }
    
    // Ensure current_ns is set to "user" (with clean mappings)
    // The user_ns we reset above should be the same one that evalstate_set_ns will find
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
                CljMap *updated_mappings = map_assoc((CljMap*)st->current_ns->mappings, sym, ex);
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
    CljMap *new_mappings = map_assoc(ns->mappings, symbol, value);
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
    if (g_runtime.resolve_cache) {
        // Remove the symbol from cache to force re-resolution
        ID updated_cache = map_assoc(g_runtime.resolve_cache, symbol, NULL);
        ASSIGN(g_runtime.resolve_cache, updated_cache);
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
    CljObject *ns_name = (CljObject*)map_get(ns->aliases, alias, NULL);
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
    CljMap *new_aliases = map_assoc(ns->aliases, alias, ns_name);
    if (new_aliases != ns->aliases) {
        // Map was copied (COW) - update reference
        RELEASE(ns->aliases);
        ns->aliases = new_aliases;
        // new_aliases is already retained by map_assoc
    }
    // If new_aliases == ns->aliases, it was in-place mutation (RC=1), no update needed
}
