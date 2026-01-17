#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "common.h"  // For CLJ_ASSERT
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "object.h"
#include "map.h"
#include "list.h"
#include "exception.h"
#include "runtime.h"
#include "tiny_clj.h"
#include "memory.h"
#include "parser.h"  // For eval_parsed
#include "vector.h"
#include "kv_macros.h"  // For KV_KEY, KV_VALUE
#include "function.h"  // For CljFunction (to break retain cycles on unload)

// Helper context for namespace search in ns_resolve()
struct ns_search_ctx {
    CljSymbol *sym;
    ID result;
    CljNamespace *result_ns;
    CljNamespace *second_ns;  // Second namespace found (for error message)
    CljNamespace *current_ns;  // Current namespace to skip in search
    bool ambiguous;  // True if ambiguous symbol found
};

// Global context pointer for callback (thread-local would be better, but this works for single-threaded)
static struct ns_search_ctx *g_ns_search_ctx = NULL;

static bool namespace_is_clojure_core(const CljNamespace *ns) {
    if (!ns || !ns->name) {
        return false;
    }
    if (SYM_CLOJURE_CORE && ns->name == SYM_CLOJURE_CORE) {
        return true;
    }
    const char *ns_name = ns->name->cname;
    return ns_name && strcmp(ns_name, "clojure.core") == 0;
}

static bool ambiguity_should_throw(const struct ns_search_ctx *ctx) {
    if (!ctx || !ctx->ambiguous || !ctx->result_ns || !ctx->second_ns) {
        return false;
    }

    return true;
}

// Helper function for map_foreach to search namespaces
static void search_namespace_callback(ID key, ID value) {
    (void)key; // Unused - we only need the value (namespace)
    if (!g_ns_search_ctx) return;
    
    // OPTIMIZATION: If already ambiguous, we can stop searching
    // (We need second_ns for the error message, so we continue until we find it)
    // Actually, we already have second_ns when ambiguous is true, so we can stop
    if (g_ns_search_ctx->ambiguous) {
        return; // Already found ambiguity, no need to search further
    }
    
    CljNamespace *ns = (CljNamespace*)value;
    // Skip current namespace - we already know the symbol exists there
    if (ns == g_ns_search_ctx->current_ns) {
        return;  // Skip current namespace
    }
    
    if (ns && ns->mappings && ns->name && ns->name->cname && 
        g_ns_search_ctx->sym && g_ns_search_ctx->sym->cname) {
        // CRITICAL: For clojure.core, use unqualified symbol (ns_name = NULL)
        // Other namespaces use fully qualified symbols as keys
        CljSymbol *lookup_sym;
        if (ns->name == SYM_CLOJURE_CORE) {
            // clojure.core mappings use unqualified symbols as keys
            lookup_sym = g_ns_search_ctx->sym;
        } else {
            // Other namespaces: qualify the symbol with the namespace name before searching
            // NOTE: intern_symbol does linear search in symbol table (not hash lookup)
            // This is necessary because mappings use qualified symbols as keys
            lookup_sym = intern_symbol(ns->name, g_ns_search_ctx->sym->cname);
            if (!lookup_sym) {
                return; // Failed to qualify - skip this namespace
            }
        }
        
        // IMPORTANT: mapping values may legitimately be nil (NULL), so we must
        // not use pointer-truthiness to detect presence.
        if (map_contains(ns->mappings, lookup_sym)) {
            ID found = map_get(ns->mappings, lookup_sym);
            if (!g_ns_search_ctx->result_ns) {
                // First namespace found - store it and continue searching
                g_ns_search_ctx->result = (found == NOT_FOUND) ? NULL : found;
                g_ns_search_ctx->result_ns = ns;
            } else {
                // Second namespace found - mark as ambiguous and stop searching
                g_ns_search_ctx->ambiguous = true;
                g_ns_search_ctx->second_ns = ns;
                // Note: We can't stop map_foreach early, but this prevents further processing
            }
        }
    }
}

static ID throw_ambiguous_symbol_error(CljSymbol *sym,
                                       CljNamespace *first_ns,
                                       CljNamespace *second_ns) {
    const char *sym_name = (sym && sym->cname) ? sym->cname : "unknown";
    const char *ns1_name = (first_ns && first_ns->name && first_ns->name->cname)
        ? first_ns->name->cname : "unknown";
    const char *ns2_name = (second_ns && second_ns->name && second_ns->name->cname)
        ? second_ns->name->cname : "unknown";
    return throw_exception_formatted(NULL, __FILE__, __LINE__, 0,
        "Unable to resolve symbol: %s in this context, perhaps you meant: %s/%s or %s/%s",
        sym_name, ns1_name, sym_name, ns2_name, sym_name);
}

// Helper context for namespace cleanup in ns_cleanup()
// (no context needed, just release each namespace)
__attribute__((unused)) static void release_namespace_callback(ID key, ID value) {
    (void)key; // Unused - we only need the value (namespace)
    CljNamespace *ns = (CljNamespace*)value;
    // Release namespace object (CljObject subtype)
    // release_object_deep() will handle freeing filename, mappings, and aliases
    RELEASE(ns);
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
    CljMap *new_map = (CljMap*)ALLOC_BYTES(CLJ_MAP, struct_size + data_size);
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
    
    for (int i = 0; i < old_map->count; i++) {
        map_conj(new_map, KV_KEY(old_map->data, i), KV_VALUE(old_map->data, i));
    }
    
    return new_map;
}

// Remove one key from the transient ns_registry while preserving transient-ness.
// Returns a NEW transient map (rc=1) and RELEASEs the old map via caller.
// NOTE: We must not use map_remove() here because it returns a persistent map (CLJ_MAP),
// but ns_registry is expected to remain transient (CLJ_MAP_TRANSIENT).
static CljMap* transient_map_without_key(CljMap *old_map, ID remove_key) {
    if (!old_map) return NULL;

    // Allocate new transient map with same capacity
    int cap = old_map->capacity;
    size_t struct_size = sizeof(CljMap);
    size_t data_size = (size_t)cap * 2 * sizeof(CljObject*);
    CljMap *new_map = (CljMap*)ALLOC_BYTES(CLJ_MAP, struct_size + data_size);
    if (!new_map) return NULL;

    new_map->base.type = CLJ_MAP_TRANSIENT;
    new_map->base.rc = 1;
    new_map->count = 0;
    new_map->capacity = cap;
    for (int i = 0; i < cap * 2; i++) {
        new_map->data[i] = NULL;
    }

    CljObject *remove_key_obj = (CljObject*)remove_key; // may be NULL (valid)
    MAP_FOR_EACH(old_map, k, v) {
        // key compare: pointer equality first; structural equality fallback.
        bool match = (k == remove_key_obj) || (k && remove_key_obj && clj_equal(k, remove_key_obj));
        if (match) {
            continue;
        }
        (void)map_conj(new_map, k, v);
    }

    return new_map;
}

// Break retain cycles: closures created in a namespace retain the namespace pointer.
// If we try to free the namespace via refcounting, we must detach these pointers first.
// For each closure whose func->ns == ns, we set func->ns=NULL and RELEASE(ns) once to
// balance the retain done in make_function().
static void break_function_self_bindings(CljFunction *fn) {
    if (!fn || !fn->env_stack) return;

    int frames = vector_count(fn->env_stack);
    for (int i = 0; i < frames; i++) {
        ID frame = vector_nth(fn->env_stack, i);
        if (!frame || !is_map(frame)) continue;
        CljMap *m = as_map(frame);
        if (!m) continue;

        // Only mutate in-place when uniquely owned (avoid mutating shared env frames).
        if (((CljObject*)m)->rc != 1) {
            continue;
        }

        MAP_FOR_EACH(m, k, v) {
            (void)k;
            if (v == (ID)fn) {
                // Clear value to nil, releasing the function reference.
                ASSIGN(KV_VALUE(m->data, _i), NULL);
            }
        }
    }
}

static void ns_break_closure_cycles(CljNamespace *ns) {
    if (!ns) return;

    CljMap *maps[2] = { ns->mappings, ns->macro_mappings };
    for (int mi = 0; mi < 2; mi++) {
        CljMap *m = maps[mi];
        if (!m) continue;

        MAP_FOR_EACH(m, k, v) {
            (void)k;
            if (!v) continue;
            if (TAG(v) != CLJ_CLOSURE) continue;
            CljFunction *fn = (CljFunction*)v;
            break_function_self_bindings(fn);
            if (fn->ns == ns) {
                fn->ns = NULL;
                // Balance make_function() retaining ns
                RELEASE(ns);
            }
        }
    }
}

bool ns_unload(EvalState *st, const char *ns_name) {
    if (!ns_name || !*ns_name) {
        return false;
    }

    // Disallow unloading clojure.core and user (safety).
    if (strcmp(ns_name, "clojure.core") == 0 || strcmp(ns_name, "user") == 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "ns-unload: refusing to unload %s", ns_name);
        return false;
    }

    if (!g_runtime.ns_registry) {
        return false;
    }

    CljSymbol *name_sym = intern_symbol_global(ns_name);
    if (!name_sym) {
        return false;
    }

    CljNamespace *ns = (CljNamespace*)map_get_sentinel(g_runtime.ns_registry, name_sym, NULL);
    if (!ns) {
        return false;
    }

    // Keep ns alive while we rebuild registry; breaking cycles will RELEASE it multiple times.
    RETAIN(ns);

    // If eval state points at this namespace, switch away before unloading.
    if (st && (st->current_ns == ns || st->resolve_ns == ns)) {
        evalstate_set_ns(st, "user");
    }

    // Rebuild registry without this namespace (keep transient type).
    CljMap *old_registry = g_runtime.ns_registry;
    CljMap *new_registry = transient_map_without_key(old_registry, name_sym);
    if (!new_registry) {
        RELEASE(ns);
        return false;
    }

    g_runtime.ns_registry = new_registry;
    RELEASE(old_registry);

    // Registry changed -> invalidate resolve cache
    ns_invalidate_resolve_cache();

    // Break retain cycles between closures and this namespace, then release namespace contents.
    ns_break_closure_cycles(ns);
    ASSIGN(ns->mappings, NULL);
    ASSIGN(ns->macro_mappings, NULL);
    ASSIGN(ns->aliases, NULL);
    ns->loaded = false;

    // Release our extra retain; after cycles are broken, this should free the namespace.
    RELEASE(ns);

    return true;
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
    RELEASE(g_runtime.ns_registry);
    g_runtime.ns_registry = NULL;
    
    // Cache will be automatically rebuilt when needed via ns_find_by_symbol
    
    // Initialize new registry
    return ns_init_registry();
}

/** Create a new namespace object (does not register it in the registry).
 * This is the low-level constructor for namespaces, following the pattern
 * of make_map(), make_symbol(), etc.
 * 
 * @param name Namespace name (must not be NULL)
 * @param file Optional filename associated with the namespace (can be NULL)
 * @return New namespace object with rc=1, or NULL on error
 */
CljNamespace* make_namespace(const char *cname, const char *file) {
    if (!cname) return NULL;
    
    // Create a new namespace using ALLOC (initializes base.type and base.rc)
    CljNamespace *ns = ALLOC(CljNamespace, 1);
    if (!ns) return NULL;

    // Ensure namespace starts as a valid retained object.
    // Zombie mode uses rc==0 to detect freed objects, so rc must not be 0 here.
    ns->base.type = CLJ_NAMESPACE;
    ns->base.rc = 1;
    
    // Get or intern the namespace name symbol
    CljSymbol *name_symbol = intern_symbol_global(cname);
    if (!name_symbol) {
        // If intern_symbol fails, we need to free the namespace
        // But ALLOC doesn't allocate memory that needs freeing, so we just return NULL
        // Actually, ALLOC uses malloc, so we need to free it
        DEALLOC(ns);
        return NULL;
    }
    
    ns->name = name_symbol; // Use the interned symbol
    ns->mappings = make_map(32); // Increased capacity for clojure.core
    
    ns->macro_mappings = NULL;  // Lazy initialization in register_macro
    
    ns->aliases = make_map(16);

    ns->loaded = false;

    
    ns->filename = file ? strdup(file) : NULL;
    if (file && !ns->filename) {
        // strdup failed - OOM
        RELEASE(ns->mappings);
        RELEASE(ns->aliases);
        DEALLOC(ns);
        return NULL;
    }
    
    return ns;
}

CljNamespace* ns_get_or_create(const char *cname, const char *file) {
    if (!cname) return NULL;
    
    // Ensure registry is initialized (DRY: use helper function)
    if (ns_init_registry() != 0) {
        return NULL; // Failed to initialize registry
    }
    
    // Look up namespace by name in the map
    CljSymbol *name_symbol = intern_symbol_global(cname);
    if (!name_symbol) return NULL;
    
    CljObject *ns_obj = map_get_sentinel(g_runtime.ns_registry, name_symbol, NULL);
    if (ns_obj) {
        return (CljNamespace*)ns_obj;
    }

    // Create a new namespace using make_namespace (DRY principle)
    CljNamespace *ns = make_namespace(cname, file);
    if (!ns) return NULL;

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
                RELEASE(ns);
                return NULL;
            }
        } else {
            // Failed to grow map - OOM
            RELEASE(ns);
            return NULL;
        }
    }
    g_runtime.ns_registry = new_registry;
    
    if (ns->name == SYM_CLOJURE_CORE) {
        CljMap *new_registry_with_null = map_conj(g_runtime.ns_registry, NULL, ns);
        if (new_registry_with_null) {
            g_runtime.ns_registry = new_registry_with_null;
        }
    }
    
    return ns;
}

ID ns_resolve(EvalState *st, CljSymbol *sym) {
    CLJ_ASSERT(sym != NULL);
    
    // Keywords evaluate to themselves - no namespace resolution needed
    if (IS_KEYWORD(sym)) {
        return sym;
    }
    
    // Use default namespace if st is NULL (eliminates need for temporary EvalState instances)
    CljNamespace *current_ns = st ? st->current_ns : ns_get_or_create("user", NULL);
    if (!current_ns) {
        return NOT_FOUND;
    }
    
    // Handle qualified symbols (symbol->ns_name is set during parsing or interning)
    // For qualified symbols like clojure.string/trim, we need to:
    // 1. Find the target namespace (clojure.string)
    // 2. Create an unqualified symbol (trim)
    // 3. Look up the unqualified symbol in the target namespace
    if (sym->ns_name && sym->ns_name->cname) {
        // Qualified symbol - look up in target namespace
        // CRITICAL: Registry uses interned namespace name symbols as keys
        // We must intern the namespace name to get the same pointer as stored in registry
        CljSymbol *interned_ns_name = sym->ns_name;  // Already a CljSymbol*
        if (!interned_ns_name) {
            return NOT_FOUND; // Failed to determine namespace name
        }
        CljNamespace *target_ns = ns_find_by_symbol(interned_ns_name);
        // Fallback to ns_find if ns_find_by_symbol didn't find it (handles cases where symbol pointers differ)
        if (!target_ns && interned_ns_name && interned_ns_name->cname) {
            target_ns = ns_find(interned_ns_name->cname);
        }
        if (target_ns && target_ns->mappings && sym->cname) {
            // OPTIMIZATION: Fast path - try direct lookup with existing symbol pointer first
            // The symbol from AST canonicalization is already interned, so pointer equality
            // should work if the mapping was created with the same symbol pointer.
            ID resolved = map_get(target_ns->mappings, sym);
            if (resolved != NOT_FOUND) {
                return resolved;  // Found (can be NULL/nil, which is valid)
            }

            // Fallback: Intern the qualified symbol to get the same pointer as stored in mappings
            // This handles cases where symbol pointers differ (e.g., from different parsing sessions)
            CljSymbol *interned_sym = intern_symbol(sym->ns_name, sym->cname);
            if (!interned_sym) {
                return NOT_FOUND; // Failed to intern
            }
            // CRITICAL: Namespace mappings now use fully qualified symbols as keys
            // Use the interned qualified symbol for lookup (ensures pointer equality)
            // Use sentinel to distinguish "key not found" from "value is nil"
            resolved = map_get(target_ns->mappings, interned_sym);
            if (resolved != NOT_FOUND) {
                return resolved;  // Found (can be NULL/nil, which is valid)
            }

            // Fallback: if the namespace was populated before qualification
            // became mandatory, the keys might still be unqualified. Try the
            // unqualified version to keep lookups robust.
            CljSymbol *unqualified_sym = intern_symbol_global(sym->cname);
            if (unqualified_sym) {
                resolved = map_get(target_ns->mappings, unqualified_sym);
                if (resolved != NOT_FOUND) {
                    return resolved;
                }
            }
        }
        // Qualified symbol not found in target namespace
        return NOT_FOUND;
    }
    
    // Unqualified symbol - check current namespace first (before cache)
    // This ensures that redefined symbols in current namespace take precedence over cached values
    // OPTIMIZATION: Fast path - try direct lookup with existing symbol pointer first
    // ns_define() stores unqualified aliases for non-core namespaces, so this should work
    // for most cases without needing to qualify+intern.
    if (current_ns && current_ns->mappings) {
        ID v = map_get(current_ns->mappings, sym);
        if (v != NOT_FOUND) {
            // Symbol found in current namespace - return it immediately
            // No ambiguity check needed: current namespace always takes precedence
            return v;
        }
    }
    
    // CRITICAL: Namespace mappings now use fully qualified symbols as keys
    // For unqualified symbols, we need to qualify them with the current namespace
    // Use sentinel to distinguish "key not found" from "value is nil"
    // In Clojure, nil is a valid value, so we need to distinguish between
    // "symbol not found" (should search other namespaces) and "symbol found with nil value" (should return nil)
    // Qualify symbol with current namespace for lookup
    CljSymbol *qualified_sym = sym;
    if (current_ns && current_ns->name && current_ns->name->cname) {
        qualified_sym = intern_symbol(current_ns->name, sym->cname);
        if (!qualified_sym) {
            // Failed to qualify - fall through to search other namespaces
            qualified_sym = sym;
        }
    }
    
    // CRITICAL: For def, symbols are stored qualified (e.g., user/my-var)
    // For :refer :all, symbols are also stored qualified (clojure.core is handled separately)
    if (current_ns && current_ns->mappings) {
        ID v = map_get(current_ns->mappings, qualified_sym);
        if (v != NOT_FOUND) {
            // Symbol found in current namespace - return it immediately
            // No ambiguity check needed: current namespace always takes precedence
            return v;
        }
    }
    
    // CRITICAL: In Clojure, unqualified symbols are only resolved in the current namespace
    // The only exception is clojure.core, which is automatically available
    // Other namespaces must be explicitly referred with :refer or :refer :all
    // Search clojure.core only (not all namespaces)
    // NOTE: SYM_CLOJURE_CORE may be NULL if special symbols are not initialized yet
    CljNamespace *clojure_core = NULL;
    if (SYM_CLOJURE_CORE) {
        clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
    }
    if (clojure_core && clojure_core->mappings && sym->cname) {
        // CRITICAL: clojure.core mappings use unqualified symbols as keys (ns_name = NULL)
        // OPTIMIZATION: Use symbol directly - map_get uses structural equality as fallback
        // This avoids expensive intern_symbol_global call in hot path
        // map_get will use clj_equal if pointer equality fails
        ID resolved = map_get(clojure_core->mappings, sym);
        if (resolved != NOT_FOUND) {
            return resolved;
        }

        // Fallback: some symbols (e.g. static symbol instances) may not be pointer-equal
        // to the canonical global interned symbol used as the clojure.core mapping key.
        // Use a globally interned key as a second attempt to keep unqualified core lookups robust.
        CljSymbol *interned = intern_symbol_global(sym->cname);
        if (interned && interned != sym) {
            resolved = map_get(clojure_core->mappings, interned);
            if (resolved != NOT_FOUND) {
                return resolved;
            }
        }
    }
    
    // Symbol not found in current namespace or clojure.core
    // In Clojure/JVM, unqualified symbols are ONLY resolved in:
    // 1. Current namespace
    // 2. clojure.core (automatically available)
    // Other namespaces must be explicitly referred with :refer or :refer :all
    // 
    // However, we need to check for ambiguity to provide helpful error messages.
    // If a symbol exists in multiple namespaces (including clojure.core), we should
    // throw an error suggesting the user to qualify the symbol.
    if (g_runtime.ns_registry) {
        // Initialize search context
        struct ns_search_ctx search_ctx = {
            .sym = sym,
            .result = NULL,
            .result_ns = NULL,
            .second_ns = NULL,
            .current_ns = current_ns,
            .ambiguous = false
        };
        g_ns_search_ctx = &search_ctx;
        
        // Search all namespaces to check for ambiguity
        // This includes clojure.core and other namespaces
        map_foreach(g_runtime.ns_registry, search_namespace_callback);
        
        g_ns_search_ctx = NULL;
        
        // If ambiguous (found in 2+ namespaces), throw error with helpful message
        if (ambiguity_should_throw(&search_ctx)) {
            return throw_ambiguous_symbol_error(sym, search_ctx.result_ns, search_ctx.second_ns);
        }
        
        // CRITICAL: Even if found in exactly one namespace, we do NOT return it.
        // In Clojure/JVM, unqualified symbols must be in current namespace or clojure.core.
        // Other namespaces require explicit qualification or :refer.
        // This ensures Clojure-compatible behavior.
    }
    
    // Symbol not found - don't cache NULL values (would waste cache space)
    return NOT_FOUND;
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
    CljObject *existing = map_get_sentinel(g_runtime.ns_registry, ns->name, NULL);
    if (existing == (CljObject*)ns) {
        return; // Already registered
    }
    
    // Add namespace to registry map (Key: ns->name, Value: ns)
    // map_conj() retains the value, so we need to retain ns before adding
    RETAIN(ns);
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
                RELEASE(ns);
                return;
            }
        } else {
            // Failed to grow map - OOM
            RELEASE(ns);
            return;
        }
    }
    g_runtime.ns_registry = new_registry;
    
    if (ns->name == SYM_CLOJURE_CORE) {
        RETAIN(ns);
        CljMap *new_registry_with_null = map_conj(g_runtime.ns_registry, NULL, ns);
        if (new_registry_with_null) {
            g_runtime.ns_registry = new_registry_with_null;
        } else {
            RELEASE(ns);
        }
    }
}

// Helper function to find namespace containing a given object
CljNamespace* ns_find_for_object(CljObject *obj) {
    if (!obj || !g_runtime.ns_registry) return NULL;
    
    MAP_FOR_EACH(g_runtime.ns_registry, ns_key, ns_val) {
        if (ns_val && TAG(ns_val) == CLJ_NAMESPACE) {
            CljNamespace *ns = (CljNamespace*)ns_val;
            if (ns->mappings) {
                MAP_FOR_EACH(ns->mappings, key, value) {
                    if (value == obj) {
                        return ns;
                    }
                }
            }
        }
    }
    return NULL;
}

// Fast lookup with symbol (avoids intern_symbol call - DRY principle)
// NOTE: name_symbol can be NULL (nil) - that's a valid key in Clojure maps
CljNamespace* ns_find_by_symbol(CljSymbol *name_symbol) {
    // Programming error: ns_registry must be initialized
    CLJ_ASSERT(g_runtime.ns_registry != NULL);
    // map_get accepts NULL as a valid key (nil can be used as key in Clojure)
    ID ns_obj = map_get_sentinel(g_runtime.ns_registry, name_symbol, NULL);
    return ns_obj;
}

// Lookup with string (for convenience - delegates to symbol version)
// NOTE: cname cannot be NULL for namespace names, but if intern_symbol_global returns NULL,
// it will be passed to ns_find_by_symbol which accepts NULL as a valid key
CljNamespace* ns_find(const char *cname) {    
    // OPTIMIZATION: Use SYM_CLOJURE_CORE directly for "clojure.core" (no intern_symbol call needed)
    CljSymbol *name_symbol;
    if (strcmp(cname, "clojure.core") == 0) {
        name_symbol = SYM_CLOJURE_CORE;
    } else {
        // Intern symbol and delegate to fast symbol-based lookup
        // NOTE: intern_symbol_global may return NULL, which is a valid key for map_get
        name_symbol = intern_symbol_global(cname);
    }
    
    // ns_find_by_symbol accepts NULL as a valid key (nil can be used as key in Clojure)
    return ns_find_by_symbol(name_symbol);
}

void ns_cleanup() {
    if (g_runtime.ns_registry) {
        CljMap *registry_to_free = g_runtime.ns_registry;
        g_runtime.ns_registry = NULL;
        RELEASE(registry_to_free);
    }
}

// Thread-local global EvalState (zero-initialized)
_Thread_local EvalState g_eval_state = {0};
_Thread_local bool g_eval_state_initialized = false;

// Get the global EvalState (lazy init)
EvalState* get_global_eval_state(void) {
    if (!g_eval_state_initialized) {
        g_eval_state.current_ns = ns_get_or_create("user", NULL);
        if (!g_eval_state.current_ns) {
            throw_exception(EXCEPTION_RUNTIME, "Failed to create user namespace", NULL, 0, 0);
            return NULL;
        }
        g_eval_state.resolve_ns = g_eval_state.current_ns;

        // Dynamic binding stack: transient vector used as push/pop stack.
        // Frames stored are maps (Symbol -> value), where value may be NULL (nil).
        g_eval_state.dynamic_bindings = vector_transient(empty_vector());
        if (!g_eval_state.dynamic_bindings) {
            throw_exception(EXCEPTION_RUNTIME, "Failed to create dynamic binding stack", NULL, 0, 0);
            return NULL;
        }
        g_eval_state_initialized = true;
    }
    return &g_eval_state;
}

// Reset for test isolation
void reset_eval_state(void) {
    if (g_eval_state.stack) {
        CLJ_FREE(g_eval_state.stack);
        g_eval_state.stack = NULL;
    }
    if (g_eval_state.dynamic_bindings && !IS_IMMEDIATE(g_eval_state.dynamic_bindings)) {
        RELEASE(g_eval_state.dynamic_bindings);
        g_eval_state.dynamic_bindings = NULL;
    }
    if (g_eval_state.expr && !IS_IMMEDIATE(g_eval_state.expr)) {
        RELEASE(g_eval_state.expr);
        g_eval_state.expr = NULL;
    }
    if (g_eval_state.result && !IS_IMMEDIATE(g_eval_state.result)) {
        RELEASE(g_eval_state.result);
        g_eval_state.result = NULL;
    }
    memset(&g_eval_state, 0, sizeof(EvalState));
    g_eval_state.current_ns = ns_get_or_create("user", NULL);
    g_eval_state.resolve_ns = g_eval_state.current_ns;

    g_eval_state.dynamic_bindings = vector_transient(empty_vector());
    if (!g_eval_state.dynamic_bindings) {
        throw_exception(EXCEPTION_RUNTIME, "Failed to create dynamic binding stack", NULL, 0, 0);
        return;
    }
    g_eval_state_initialized = true;
}

// Clear current_ns pointer without creating a new namespace
// Used during cleanup to prevent dangling pointers
void reset_eval_state_current_ns(void) {
    g_eval_state.current_ns = NULL;
}

// EvalState functions
// OPTIMIZATION: Now returns global thread-local state instead of heap allocation
EvalState* evalstate_new(bool load_core) {
    EvalState *st = get_global_eval_state();
    if (!st) return NULL;
    
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

// OPTIMIZATION: Now a complete no-op since we use global thread-local state
// Kept for compatibility with existing code (tests, REPL cleanup)
// The global state is never freed - it's thread-local and lives for the thread lifetime
// Cleanup between tests is handled by reset_eval_state() in test setup
void evalstate_free(EvalState *st) {
    // No-op: global state is never freed
    (void)st; // Suppress unused parameter warning
}

void evalstate_set_ns(EvalState *st, const char *ns_name) {
    if (!st || !ns_name) return;
    
    // Get or create namespace
    CljNamespace *ns = ns_find(ns_name);
    if (!ns) {
        ns = ns_get_or_create(ns_name, NULL);
    }
    
    if (ns) {
        CljNamespace *old_current_ns = st->current_ns;
        st->current_ns = ns;
        // Keep resolve_ns tracking current_ns unless it was intentionally overridden
        // (e.g., during closure invocation).
        if (st->resolve_ns == NULL || st->resolve_ns == old_current_ns) {
            st->resolve_ns = ns;
        }
    }
}

void evalstate_reset(EvalState **st_ptr, bool load_core) {
    if (!st_ptr) return;
    
    // Reset global state
    reset_eval_state();
    EvalState *st = get_global_eval_state();
    if (!st) return;
    
    // Always load clojure.core if requested (for test isolation)
    if (load_core) {
        evalstate_set_ns(st, "clojure.core");
        load_clojure_core(st);
    }
    
    // Reset all fields
    st->expr = NULL;
    st->result = NULL;
    st->pc = 0;
    st->step_budget = 0;
    st->sp = 0;
    st->finished = 0;
    
    // Set pointer to global state
    *st_ptr = st;
    
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
    CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
    if (user_ns && user_ns != clojure_core) {
        // Replace mappings with a fresh map for test isolation.
        // make_map() returns an owned object (rc=1). ASSIGN() retains the new value,
        // so we balance that retain to keep the adopted map at rc=1.
        CljMap *fresh = make_map(16);
        ASSIGN(user_ns->mappings, fresh);
        RELEASE(fresh);
    }
    
    // Ensure current_ns is set to "user" (with clean mappings)
    // The user_ns we reset above should be the same one that evalstate_set_ns will find
    evalstate_set_ns(st, "user");
}

void evalstate_pop_dynamic_bindings_to(EvalState *st, unsigned int depth) {
    if (!st) return;
    while (st->dynamic_bindings && vector_count(st->dynamic_bindings) > depth) {
        vector_pop_inplace(&st->dynamic_bindings);
    }
}

// Exception handling
void eval_error(const char *msg, EvalState *st) {
    if (!st) return;
    
    // Use throw_exception which handles the exception_stack correctly
    throw_exception(EXCEPTION_RUNTIME, msg, NULL, 0, 0);
}

void parse_error(const char *msg, EvalState *st) {
    if (!st) return;
    
    // Use throw_exception which handles the exception_stack correctly
    throw_exception(EXCEPTION_PARSE, msg, NULL, 0, 0);
}

static INLINE bool sym_cname_eq(ID obj, const char *name) {
    if (!obj || TAG(obj) != CLJ_SYMBOL) return false;
    CljSymbol *sym = (CljSymbol*)obj;
    if (!sym || !sym->cname) return false;
    const char *slash = strrchr(sym->cname, '/');
    const char *simple = slash ? (slash + 1) : sym->cname;
    return strcmp(simple, name) == 0;
}


// Try/Catch-Implementierung using TRY/CATCH macros
CljObject* eval_try(CljObject *form, EvalState *st) {
    if (!form || form->type != CLJ_LIST) return NULL;
    
    ID result = NULL;
    
    TRY {
        // normaler Body (zweites Element)
        CljObject *body = list_nth(as_list(form), 1);
        result = eval_parsed(body, st, NULL);
    } CATCH(ex) {
        // We arrived here via eval_error
        // Search for catch clauses
        CljList *form_list = as_list(form);
        CljList *args = list_or_null(as_list(LIST_REST(form_list)));
        CljList *clause_node = args ? list_or_null(as_list(LIST_REST(args))) : NULL;

        for (CljList *node = clause_node; node; node = list_or_null(as_list(LIST_REST(node)))) {
            CljObject *clause = LIST_FIRST(node);
            if (!is_list(clause)) continue;

            CljList *clause_list = as_list(clause);
            if (!clause_list) continue;

            ID first_elem = LIST_FIRST(clause_list);
            if (first_elem != SYM_CATCH && !sym_cname_eq(first_elem, "catch")) {
                continue;
            }

            // Supported catch clause shapes:
            // - (catch sym body...)
            // - (catch Type sym body...)
            ID binding_sym = NULL;
            CljList *body_node = NULL;

            CljList *cargs = list_or_null(as_list(LIST_REST(clause_list)));
            if (!cargs) continue;

            ID arg1 = LIST_FIRST(cargs);
            CljList *after1 = list_or_null(as_list(LIST_REST(cargs)));
            if (arg1 && TAG(arg1) == CLJ_SYMBOL) {
                binding_sym = arg1;
                body_node = after1;
            } else if (after1) {
                ID arg2 = LIST_FIRST(after1);
                if (arg2 && TAG(arg2) == CLJ_SYMBOL) {
                    binding_sym = arg2;
                    body_node = list_or_null(as_list(LIST_REST(after1)));
                }
            }

            // Require at least one body form (even if it evaluates to nil).
            if (!binding_sym || TAG(binding_sym) != CLJ_SYMBOL || !body_node) continue;

            // Bind variable (sym = err) - simplified
            // CRITICAL: map_assoc may return a new map (COW), so we must use the result
            CljMap *updated_mappings = map_assoc(st->current_ns->mappings, binding_sym, ex);
            ASSIGN(st->current_ns->mappings, updated_mappings);

            // Evaluate catch body (support multiple expressions)
            for (CljList *b = body_node; b; b = list_or_null(as_list(LIST_REST(b)))) {
                CljObject *body_expr = LIST_FIRST(b);
                if (!body_expr) continue;
                result = eval_parsed(body_expr, st, NULL);
            }
            return result;
        }
        // No catch clause found - re-throw (handler is already popped!)
        throw_exception(ex->type[0] != '\0' ? ex->type : "Error", 
                   ex->message[0] != '\0' ? ex->message : "Unknown error",
                       ex->file, ex->line, ex->col);
    } END_TRY
    
    return result;
}

CljObject* eval_catch(CljObject *form, EvalState *st) {
    // Simplified catch implementation
    return eval_try(form, st);
}


/**
 * @brief Define a symbol in the current namespace
 * @param st Evaluation state
 * @param symbol Symbol to define
 * @param value Value to bind to symbol
 */
/**
 * @brief Invalidate the resolve cache by setting it to NULL
 * This is more efficient than removing individual symbols via map_assoc.
 * The cache will be automatically rebuilt on the next ns_resolve() call.
 */
void ns_invalidate_resolve_cache(void) {
    // Invalidate callsite caches by incrementing epoch
    // All existing callsite caches will be invalidated on next access
    uint64_t next_epoch = g_runtime.resolve_cache_epoch + 1;
    if (next_epoch == 0) {
        next_epoch = 1;
    }
    g_runtime.resolve_cache_epoch = next_epoch;

    // Release current cache map so it will be lazily rebuilt on next use
    ASSIGN(g_runtime.resolve_cache, NULL);
}

/**
 * @brief Get the canonical symbol pointer for namespace mapping lookup (DRY helper)
 * 
 * For fully qualified symbols (ns_name set), returns the symbol pointer directly
 * without re-interning. For unqualified symbols, qualifies and interns them.
 * 
 * This eliminates unnecessary strcmp calls in hot paths by avoiding re-interning
 * of already-qualified symbols.
 * 
 * @param ns Target namespace
 * @param symbol Input symbol (may be qualified or unqualified)
 * @return Canonical symbol pointer for use in map_get/map_assoc, or NULL on error
 */
static CljSymbol* get_namespace_mapping_key(CljNamespace *ns, CljSymbol *symbol) {
    if (!ns || !symbol || !symbol->cname) {
        return NULL;
    }

    // clojure.core stores symbols unqualified (ns_name = NULL) to match JVM semantics.
    // Always intern globally so map_get uses the same pointer everywhere.
    if (namespace_is_clojure_core(ns)) {
        return intern_symbol_global(symbol->cname);
    }

    // Already-qualified symbol for this namespace? keep pointer (already interned).
    if (symbol->ns_name && ns->name && symbol->ns_name == ns->name) {
        return symbol;
    }

    // Symbol is explicitly qualified to another namespace (e.g. foo/bar) – store as-is.
    if (symbol->ns_name && symbol->ns_name->cname) {
        return symbol;
    }

    // Unqualified symbol for non-core namespace: qualify & intern (ensures pointer equality).
    if (ns->name && ns->name->cname) {
        return intern_symbol(ns->name, symbol->cname);
    }

    // Fallback: return original pointer (should not happen, but avoids crashes).
    return symbol;
}

void ns_define(CljNamespace *ns, ID symbol, ID value) {
    CLJ_ASSERT(ns != NULL);
    CLJ_ASSERT(symbol != NULL);
    
    CljSymbol *sym = as_symbol(symbol);
    if (!sym) {
        return;
    }
    
    // DRY: Use helper function to get canonical symbol pointer for namespace mapping
    // This eliminates duplicate interning logic and avoids re-interning fully qualified symbols
    CljSymbol *qualified_symbol = get_namespace_mapping_key(ns, sym);
    if (!qualified_symbol) {
        qualified_symbol = sym;  // Fallback to original if helper fails
    }
    
    CLJ_ASSERT(ns->mappings != NULL);

    // Store symbol-value binding (overwrites existing)
    // For def: store qualified symbol (e.g., user/my-var)
    // IMPORTANT: Use owned/in-place update to keep rc==1 during core load (COW hot path).
    map_assoc_inplace(&ns->mappings, qualified_symbol, value);

    // // Provide unqualified alias so direct map_get on the original symbol works (useful for tests/debuggers)
    // if (sym && !sym->ns_name && sym != qualified_symbol) {
    //     map_assoc_inplace(&ns->mappings, sym, value);
    // }
    
    // OPTIMIZATION: Invalidate resolve cache completely instead of removing individual symbols
    // This avoids ~23 map_assoc() calls per require and is more efficient.
    // The cache will be automatically rebuilt on the next ns_resolve() call.
    ns_invalidate_resolve_cache();
}

// For :refer :all - stores qualified symbol (clojure.core remains special-cased)
void ns_define_refer(CljNamespace *ns, ID symbol, ID value) {
    CLJ_ASSERT(ns != NULL);
    CLJ_ASSERT(symbol != NULL);
    
    CljSymbol *sym = as_symbol(symbol);
    if (!sym || !sym->cname) {
        return;
    }
    
    CLJ_ASSERT(ns->mappings != NULL);
 

    // DRY: Use helper function for qualified symbol (for consistency with def/defn entries)
    // Store qualified symbol for consistency with def/defn entries
    if (ns->name && ns->name->cname) {
        CljSymbol *qualified_sym = get_namespace_mapping_key(ns, sym);
        if (qualified_sym) {
            map_assoc_inplace(&ns->mappings, qualified_sym, value);
        }
    }

    ns_invalidate_resolve_cache();
}

/**
 * @brief Get namespace name for an alias
 * @param ns Namespace to search in
 * @param alias Alias symbol to look up
 * @return Namespace name symbol or NULL if not found
 */
ID ns_get_alias(CljNamespace *ns, ID alias) {
    if (!ns || !alias || !ns->aliases) return NULL;
    
    // Look up alias in aliases map
    ID ns_name = map_get_sentinel(ns->aliases, alias, NULL);
    return ns_name;
}

/**
 * @brief Set namespace alias
 * @param ns Namespace to set alias in
 * @param alias Alias symbol
 * @param ns_name Namespace name symbol
 */
void ns_set_alias(CljNamespace *ns, ID alias, ID ns_name) {
    if (!ns || !alias || !ns_name) return;
    
    // Create or update aliases map
    if (!ns->aliases) {
        ns->aliases = make_map(16);
    }
    
    // Store alias-namespace binding (overwrites existing)
    map_assoc_inplace(&ns->aliases, alias, ns_name);
}
