#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "common.h"  // For CLJ_ASSERT
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "map.h"
#include "list.h"
#include "exception.h"
#include "runtime.h"
#include "tiny_clj.h"
#include "memory.h"
#include "meta.h"
#include "builtins.h"
#include "parser.h"  // For eval_parsed
#include "vector.h"

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

// Known approximate size of clojure.core mappings (defs/defns) to avoid resize during load
#define CLOJURE_CORE_INITIAL_MAPPINGS_CAPACITY 256

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

static bool namespace_lookup_is_private_from(CljNamespace *current_ns,
                                             CljNamespace *target_ns,
                                             CljSymbol *symbol);
static INLINE bool namespace_lookup_key_is_private(CljPersistentMap *private_mappings,
                                                   CljSymbol *symbol);

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
    throw_exception_formatted(NULL, __FILE__, __LINE__, 0,
                              "Unable to resolve symbol: %s in this context, perhaps you meant: %s/%s or %s/%s",
                              sym_name, ns1_name, sym_name, ns2_name, sym_name);
    return NULL;
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

    CljPersistentMap *ns_map = make_map(16);
    CljTransientMap *transient_map = map_transient(ns_map);
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

    // Ensure namespace starts as a valid retained object.
    // Zombie mode uses rc==0 to detect freed objects, so rc must not be 0 here.
    ns->base.type = CLJ_NAMESPACE;

    // Get or intern the namespace name symbol
    CljSymbol *name_symbol = intern_symbol_global(cname);
    if (!name_symbol) {
        // If intern_symbol fails, we need to free the namespace
        // But ALLOC doesn't allocate memory that needs freeing, so we just return NULL
        // Actually, ALLOC uses malloc, so we need to free it
        free(ns);
        return NULL;
    }

    ns->name = name_symbol; // Use the interned symbol
    // clojure.core: pre-allocate known size to reduce fragmentation and startup cost
    const int mappings_cap = (SYM_CLOJURE_CORE && name_symbol == SYM_CLOJURE_CORE)
        ? CLOJURE_CORE_INITIAL_MAPPINGS_CAPACITY : 4;
    ns->mappings = make_map(mappings_cap);
    ns->private_mappings = NULL;
    ns->macro_mappings = NULL;  // Lazy initialization in register_macro
    ns->aliases = NULL;  // Lazy initialization in ns_set_alias()

    ns->loaded = false;
    ns->loading = false;


    ns->filename = file ? clj_strdup(file) : NULL;
    if (file && !ns->filename) {
        // strdup failed - OOM
        RELEASE(ns->mappings);
        free(ns);
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

    map_conj(g_runtime.ns_registry, name_symbol, ns);

    if (ns->name == SYM_CLOJURE_CORE) {
        map_conj(g_runtime.ns_registry, NULL, ns);
    }

    // ns_registry owns the created namespace entries; return a borrowed pointer.
    RELEASE(ns);

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
            const bool privacy_check_needed =
                (current_ns != target_ns && target_ns->private_mappings != NULL);
            // OPTIMIZATION: Fast path - try direct lookup with existing symbol pointer first
            // The symbol from AST canonicalization is already interned, so pointer equality
            // should work if the mapping was created with the same symbol pointer.
            ID resolved = map_get(target_ns->mappings, sym);
            if (resolved != NOT_FOUND) {
                if (privacy_check_needed &&
                    namespace_lookup_key_is_private(target_ns->private_mappings, sym)) {
                    return NOT_FOUND;
                }
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
                if (privacy_check_needed &&
                    namespace_lookup_key_is_private(target_ns->private_mappings, interned_sym)) {
                    return NOT_FOUND;
                }
                return resolved;  // Found (can be NULL/nil, which is valid)
            }

            // Fallback: if the namespace was populated before qualification
            // became mandatory, the keys might still be unqualified. Try the
            // unqualified version to keep lookups robust.
            CljSymbol *unqualified_sym = intern_symbol_global(sym->cname);
            if (unqualified_sym) {
                resolved = map_get(target_ns->mappings, unqualified_sym);
                if (resolved != NOT_FOUND) {
                    if (privacy_check_needed &&
                        namespace_lookup_key_is_private(target_ns->private_mappings, unqualified_sym)) {
                        return NOT_FOUND;
                    }
                    return resolved;
                }
            }
        }
        // Qualified symbol not found in target namespace
        return NOT_FOUND;
    }

    // Unqualified symbol - check current namespace first.
    // Try qualified lookup first so def/ns_define (e.g. user/inc) shadow referred unqualified (e.g. inc from core).
    // Important: do not intern here. Lookup-only avoids creating symbols like user/+ when they are not defined.
    CljSymbol *qualified_sym = sym;
    if (current_ns && current_ns->name && current_ns->name->cname) {
        CljSymbol *existing_qualified = symbol_table_lookup(current_ns->name, sym->cname);
        if (existing_qualified) {
            qualified_sym = existing_qualified;
        }
    }
    if (current_ns && current_ns->mappings) {
        ID v = map_get(current_ns->mappings, qualified_sym);
        if (v != NOT_FOUND) return v;
        if (qualified_sym != sym) {
            v = map_get(current_ns->mappings, sym);
            if (v != NOT_FOUND) return v;
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
    map_conj(g_runtime.ns_registry, ns->name, ns);

    if (ns->name == SYM_CLOJURE_CORE) {
        RETAIN(ns);
        map_conj(g_runtime.ns_registry, NULL, ns);
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
        CljTransientMap *registry_to_free = g_runtime.ns_registry;
        g_runtime.ns_registry = NULL;
        MAP_FOR_EACH(registry_to_free, key, val) {
            (void)key;
            if (val && !IS_IMMEDIATE(val) && TAG(val) == CLJ_NAMESPACE) {
                ns_release_owned_state((CljNamespace *)val, false);
            }
        }
        RELEASE(registry_to_free);
    }
}

// Thread-local global EvalState (zero-initialized)
_Thread_local EvalState g_eval_state = {0};
_Thread_local bool g_eval_state_initialized = false;
_Thread_local EvalState *g_eval_state_override = NULL;

// Get the global EvalState (lazy init)
EvalState* get_global_eval_state(void) {
    if (g_eval_state_override) {
        return g_eval_state_override;
    }
    if (!g_eval_state_initialized) {
        g_eval_state.current_ns = ns_get_or_create("user", NULL);
        if (!g_eval_state.current_ns) {
            throw_exception(EXCEPTION_RUNTIME, "Failed to create user namespace", NULL, 0, 0);
            return NULL;
        }
        g_eval_state.resolve_ns = g_eval_state.current_ns;

        // Dynamic binding stack: transient vector used as push/pop stack.
        // Frames stored are maps (Symbol -> value), where value may be NULL (nil).
        g_eval_state.dynamic_bindings = make_vector_transient(empty_vector());
        if (!g_eval_state.dynamic_bindings) {
            throw_exception(EXCEPTION_RUNTIME, "Failed to create dynamic binding stack", NULL, 0, 0);
            return NULL;
        }
        g_eval_state_initialized = true;
    }
    return &g_eval_state;
}

EvalState* evalstate_set_global_override(EvalState *override_state) {
    EvalState *previous = g_eval_state_override;
    g_eval_state_override = override_state;
    return previous;
}

// Reset for test isolation
void reset_eval_state(void) {
    if (g_eval_state.stack) {
        free(g_eval_state.stack);
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

    g_eval_state.dynamic_bindings = make_vector_transient(empty_vector());
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
void evalstate_ensure_builtins_ready(void) {
    if (g_runtime.builtins_registered) {
        return;
    }

    meta_registry_init();
    register_builtins();
    g_runtime.builtins_registered = true;
}

static inline void ns_release_map_slot(CljPersistentMap **slot, bool replace_with_empty_map) {
    if (!slot || !*slot) {
        return;
    }

    CljPersistentMap *owned = *slot;
    *slot = NULL;
    RELEASE(owned);

    if (replace_with_empty_map) {
        *slot = make_map(0);
    }
}

void ns_release_owned_state(CljNamespace *ns, bool keep_empty_shell) {
    if (!ns) {
        return;
    }

    ns->loaded = false;
    ns->loading = false;

    ns_release_map_slot(&ns->mappings, keep_empty_shell);
    ns_release_map_slot(&ns->private_mappings, false);
    ns_release_map_slot(&ns->macro_mappings, false);
    ns_release_map_slot(&ns->aliases, keep_empty_shell);
}

EvalState* evalstate_new(bool load_core) {
    EvalState *st = get_global_eval_state();
    if (!st) return NULL;

    // Load clojure.core automatically if requested (functions available via ns_resolve)
    if (load_core) {
        evalstate_ensure_builtins_ready();
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
        evalstate_ensure_builtins_ready();
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
        CljPersistentMap *fresh = make_map(16);
        ASSIGN(user_ns->mappings, fresh);
        RELEASE(fresh);
        // Refer clojure.core into user so (get ...) etc. resolve (Clojure default).
        if (load_core && clojure_core && clojure_core->mappings) {
            MAP_FOR_EACH(clojure_core->mappings, k, v) {
                if (k && TAG(k) == CLJ_SYMBOL)
                    map_assoc_inplace(&user_ns->mappings, k, v);
            }
        }
    }

    // Ensure current_ns is set to "user" (with clean mappings)
    // The user_ns we reset above should be the same one that evalstate_set_ns will find
    evalstate_set_ns(st, "user");
}

void evalstate_pop_dynamic_bindings_to(EvalState *st, unsigned int depth) {
    if (!st) return;
    while (st->dynamic_bindings && st->dynamic_bindings->backing &&
           vector_count(st->dynamic_bindings->backing) > depth) {
        vector_pop(st->dynamic_bindings);
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

/**
 * @brief Define a symbol in the current namespace
 * @param st Evaluation state
 * @param symbol Symbol to define
 * @param value Value to bind to symbol
 */
// Coalesce repeated namespace mutations (bootstrap/require) into one epoch bump.
static uint16_t g_resolve_cache_batch_depth = 0;
static bool g_resolve_cache_batch_dirty = false;

void ns_begin_resolve_cache_batch(void) {
    if (g_resolve_cache_batch_depth < UINT16_MAX) {
        g_resolve_cache_batch_depth++;
    }
}

void ns_end_resolve_cache_batch(void) {
    if (g_resolve_cache_batch_depth == 0) {
        return;
    }
    g_resolve_cache_batch_depth--;
    if (g_resolve_cache_batch_depth == 0 && g_resolve_cache_batch_dirty) {
        g_resolve_cache_batch_dirty = false;
#if !MEMORY_PROFILING_ENABLED
        g_runtime.resolve_cache_epoch = runtime_next_resolve_epoch(&g_runtime.resolve_cache_generation);
#endif
    }
}

/**
 * @brief Invalidate resolution callsite caches by bumping the global epoch.
 */
void ns_invalidate_resolve_cache(void) {
    if (g_resolve_cache_batch_depth > 0) {
        g_resolve_cache_batch_dirty = true;
        return;
    }
#if !MEMORY_PROFILING_ENABLED
    // Invalidate callsite caches by bumping the global epoch counter.
    g_runtime.resolve_cache_epoch = runtime_next_resolve_epoch(&g_runtime.resolve_cache_generation);
#endif
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

static INLINE bool namespace_lookup_key_is_private(CljPersistentMap *private_mappings,
                                                   CljSymbol *symbol) {
    if (!private_mappings || !symbol) {
        return false;
    }
    return map_get_sentinel(private_mappings, symbol, NOT_FOUND) != NOT_FOUND;
}

static bool namespace_lookup_is_private_from(CljNamespace *current_ns,
                                             CljNamespace *target_ns,
                                             CljSymbol *symbol) {
    if (!target_ns || !target_ns->private_mappings || !symbol) {
        return false;
    }
    if (current_ns == target_ns) {
        return false;
    }

    if (namespace_lookup_key_is_private(target_ns->private_mappings, symbol)) {
        return true;
    }

    CljSymbol *canonical = get_namespace_mapping_key(target_ns, symbol);
    if (canonical && canonical != symbol) {
        if (namespace_lookup_key_is_private(target_ns->private_mappings, canonical)) {
            return true;
        }
    }

    return false;
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

    bool changed = false;

    // Store symbol-value binding (overwrites existing)
    // For def: store qualified symbol (e.g., user/my-var)
    // IMPORTANT: Use owned/in-place update to keep rc==1 during core load (COW hot path).
    ID prev = map_get_sentinel(ns->mappings, qualified_symbol, NOT_FOUND);
    bool had_qualified_binding = (prev != NOT_FOUND);
    if (had_qualified_binding && prev != value) {
        changed = true;
    }
    map_assoc_inplace(&ns->mappings, qualified_symbol, value);

    // Defining a qualified key for the first time can still shadow an existing
    // unqualified binding in the same namespace (e.g. referred clojure.core symbol).
    if (!had_qualified_binding && ns->name != SYM_CLOJURE_CORE && sym->ns_name == NULL) {
        ID prev_unqualified = map_get_sentinel(ns->mappings, sym, NOT_FOUND);
        if (prev_unqualified != NOT_FOUND && prev_unqualified != value) {
            changed = true;
        }
    }

    // For clojure.core, also keep an unqualified binding for compatibility.
    if (ns->name == SYM_CLOJURE_CORE && sym->ns_name == NULL) {
        map_assoc_inplace(&ns->mappings, sym, value);
    }

    if (changed) {
        // OPTIMIZATION: Invalidate resolve cache completely instead of removing individual symbols.
        // The cache will be automatically rebuilt on the next ns_resolve() call.
        ns_invalidate_resolve_cache();
    }
}

void ns_mark_private(CljNamespace *ns, ID symbol) {
    CLJ_ASSERT(ns != NULL);
    CLJ_ASSERT(symbol != NULL);

    CljSymbol *sym = as_symbol(symbol);
    if (!sym) {
        return;
    }

    CljSymbol *private_symbol = get_namespace_mapping_key(ns, sym);
    if (!private_symbol) {
        private_symbol = sym;
    }

    if (!ns->private_mappings) {
        ns->private_mappings = make_map(4);
        if (!ns->private_mappings) {
            return;
        }
    }

    map_assoc_inplace(&ns->private_mappings, private_symbol, clj_true);
    ns_invalidate_resolve_cache();
}

bool ns_is_private(CljNamespace *ns, CljSymbol *symbol) {
    return namespace_lookup_is_private_from(NULL, ns, symbol);
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

    bool changed = false;
    // DRY: Use helper function for qualified symbol (for consistency with def/defn entries)
    // Store qualified symbol for consistency with def/defn entries
    if (ns->name && ns->name->cname) {
        CljSymbol *qualified_sym = get_namespace_mapping_key(ns, sym);
        if (qualified_sym) {
            ID prev = map_get_sentinel(ns->mappings, qualified_sym, NOT_FOUND);
            if (prev != NOT_FOUND && prev != value) {
                changed = true;
            }
            map_assoc_inplace(&ns->mappings, qualified_sym, value);
        }
    }

    if (changed) {
        ns_invalidate_resolve_cache();
    }
}

/**
 * @brief Get namespace name for an alias
 * @param ns Namespace to search in
 * @param alias Alias symbol to look up
 * @return Namespace name symbol or NULL if not found
 */
ID ns_get_alias(CljNamespace *ns, CljSymbol *alias) {
    if (!ns || !alias || !ns->aliases) return NULL;
    if (!alias->cname) return NULL;

    // Normalize alias to an unqualified symbol so lookups work regardless of ns qualification.
    ID key = (ID)intern_symbol_global(alias->cname);

    // Look up alias in aliases map
    ID ns_name = map_get_sentinel(ns->aliases, key, NULL);
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
    CLJ_ASSERT(TAG(alias) == CLJ_SYMBOL);
    CljSymbol *alias_sym = as_symbol(alias);
    CLJ_ASSERT(alias_sym && alias_sym->cname);

    // Create or update aliases map
    if (!ns->aliases) {
        ns->aliases = make_map(4);
    }

    // Store alias-namespace binding (overwrites existing). Key = unqualified symbol for lookup.
    ID key = (ID)intern_symbol_global(alias_sym->cname);
    map_assoc_inplace(&ns->aliases, key, ns_name);
}
