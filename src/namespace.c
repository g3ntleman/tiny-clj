#include <stdlib.h>
#include <string.h>
#include "namespace.h"
#include "object.h"
#include "map.h"
#include "list_operations.h"
#include "function_call.h"
#include "memory.h"
#include "exception.h"

// Global namespace registry
CljNamespace *ns_registry = NULL;

// Cached reference to clojure.core for priority lookup
static CljNamespace *clojure_core_cache = NULL;

CljNamespace* ns_get_or_create(const char *name, const char *file) {
    if (!name) return NULL;
    
    // First, look for an existing namespace
    CljNamespace *cur = ns_registry;
    while (cur) {
        if (cur->name) {
            CljSymbol *name_sym = as_symbol(cur->name);
            if (name_sym && strcmp(name_sym->name, name) == 0) {
                return cur;
            }
        }
        cur = cur->next;
    }

    // Create a new namespace
    CljNamespace *ns = (CljNamespace*)malloc(sizeof(CljNamespace));
    if (!ns) return NULL;
    
    ns->name = intern_symbol(NULL, name);
    ns->mappings = (CljMap*)make_map(64); // Increased capacity for clojure.core // Initial capacity
    ns->filename = file ? strdup(file) : NULL;
    ns->next = ns_registry;
    ns_registry = ns;
    
    // Cache clojure.core for priority lookup
    if (strcmp(name, "clojure.core") == 0) {
        clojure_core_cache = ns;
    }
    
    return ns;
}

ID ns_resolve(EvalState *st, CljObject *sym) {
    if (!st || !sym || !st->current_ns) {
        return NULL;
    }
    
    // First search in the current namespace
    CljObject *v = (CljObject*)map_get((CljValue)st->current_ns->mappings, (CljValue)sym);
    if (v) {
        return (ID)v;
    }

    // OPTIMIZATION: Priority-based namespace search
    // 1. Search clojure.core first (most common) - use cache for O(1) lookup
    if (!clojure_core_cache) {
        // Find and cache clojure.core namespace
        CljNamespace *cur = ns_registry;
        while (cur) {
            if (cur->name && is_type(cur->name, CLJ_SYMBOL)) {
                CljSymbol *name_sym = as_symbol(cur->name);
                if (name_sym && strcmp(name_sym->name, "clojure.core") == 0) {
                    clojure_core_cache = cur;
                    break;
                }
            }
            cur = cur->next;
        }
    }
    
    // Search clojure.core first if cached
    if (clojure_core_cache && clojure_core_cache->mappings) {
        v = (CljObject*)map_get((CljValue)clojure_core_cache->mappings, (CljValue)sym);
        if (v) return (ID)v;
    }
    
    // 2. Search other namespaces (excluding clojure.core to avoid double search)
    CljNamespace *cur = ns_registry;
    while (cur) {
        if (cur != clojure_core_cache && cur->mappings) {
            v = (CljObject*)map_get((CljValue)cur->mappings, (CljValue)sym);
            if (v) return (ID)v;
        }
        cur = cur->next;
    }
    
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
    CljNamespace *cur = ns_registry;
    while (cur) {
        if (cur == ns) return; // Already registered
        cur = cur->next;
    }
    
    // Add namespace to registry
    ns->next = ns_registry;
    ns_registry = ns;
}

CljNamespace* ns_find(const char *name) {
    if (!name) return NULL;
    
    CljNamespace *cur = ns_registry;
    while (cur) {
        if (cur->name && is_type(cur->name, CLJ_SYMBOL)) {
            CljSymbol *sym = as_symbol(cur->name);
            if (strcmp(sym->name, name) == 0) {
                return cur;
            }
        }
        cur = cur->next;
    }
    return NULL;
}

void ns_cleanup() {
    // Namespaces should live until program end!
    // Free only the namespace structs, NOT names and mappings
    CljNamespace *cur = ns_registry;
    while (cur) {
        CljNamespace *next = cur->next;
        
        // DO NOT: if (cur->name) release(cur->name);
        // DO NOT: if (cur->mappings) release(cur->mappings);
        // Names and mappings remain allocated until program end
        if (cur->filename) free((void*)cur->filename);
        free(cur);
        
        cur = next;
    }
    ns_registry = NULL;
}

// EvalState functions
EvalState* evalstate() {
    EvalState *st = (EvalState*)malloc(sizeof(EvalState));
    if (!st) {
        return NULL;
    }
    
    memset(st, 0, sizeof(EvalState));
    st->pool = autorelease_pool_push();
    if (!st->pool) {
        free(st);
        return NULL;
    }
    
    st->current_ns = ns_get_or_create("user", NULL); // Default namespace
    if (!st->current_ns) {
        autorelease_pool_pop_specific(st->pool);
        free(st);
        return NULL;
    }
    
    // Note: Exception handling now uses global exception stack
    st->file = NULL;
    st->line = 0;
    st->col = 0;
    
    return st;
}

EvalState* evalstate_new() {
    return evalstate();
}

void evalstate_free(EvalState *st) {
    if (!st) return;
    
    // Don't pop the pool here - it's already been popped by the global autorelease pool
    // if (st->pool) autorelease_pool_pop_specific(st->pool);
    if (st->stack) free(st->stack);
    free(st);
}

void evalstate_set_ns(EvalState *st, const char *ns_name) {
    if (!st || !ns_name) return;
    
    CljNamespace *ns = ns_find(ns_name);
    if (!ns) {
        ns = ns_get_or_create(ns_name, NULL);
    }
    
    if (ns) {
        st->current_ns = ns;
    }
}

// Exception handling
void eval_error(const char *msg, EvalState *st) {
    if (!st) return;
    
    // Use throw_exception which handles the exception_stack correctly
    throw_exception("RuntimeException", msg, st->file, st->line, st->col);
}

void parse_error(const char *msg, EvalState *st) {
    if (!st) return;
    
    // Use throw_exception which handles the exception_stack correctly
    throw_exception("ParseError", msg, st->file, st->line, st->col);
}


// Try/Catch-Implementierung using TRY/CATCH macros
CljObject* eval_try(CljObject *form, EvalState *st) {
    if (!form || form->type != CLJ_LIST) return NULL;
    
    CljObject *result = NULL;
    
    TRY {
        // normaler Body (zweites Element)
        CljObject *body = (CljObject*)list_nth(as_list(form), 1);
        result = eval_expr_simple(body, st);
    } CATCH(ex) {
        // We arrived here via eval_error
        // Search for catch clauses
        for (int i = 2; i < list_count(as_list(form)); i++) {
            CljObject *clause = (CljObject*)list_nth(as_list(form), i);
            if (is_list(clause) && is_symbol((CljObject*)list_first(as_list(clause)), "catch")) {
                CljObject *sym = (CljObject*)list_nth(as_list(clause), 1);
                CljObject *body = (CljObject*)list_nth(as_list(clause), 2);
                
                // Bind variable (sym = err) - simplified
                map_assoc((CljObject*)st->current_ns->mappings, sym, (CljObject*)ex);
                result = eval_expr_simple(body, st);
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

// Simplified evaluation for demonstration purposes
CljObject* eval_expr_simple(CljObject *expr, EvalState *st) {
    if (!expr) return NULL;
    
    CljObject *result = NULL;
    
    // Use TRY/CATCH to handle exceptions
    TRY {
        if (is_type(expr, CLJ_SYMBOL)) {
            result = eval_symbol(expr, st);
            if (result) result = AUTORELEASE(result);
        } else if (is_type(expr, CLJ_LIST)) {
            CljObject *env = (st && st->current_ns) ? (CljObject*)st->current_ns->mappings : NULL;
            result = eval_list(as_list(expr), (CljMap*)env, st);
            if (result) result = AUTORELEASE(result);
        } else {
            result = AUTORELEASE(expr);
        }
    } CATCH(ex) {
        // Exception caught - re-throw to let caller handle it
        throw_exception_formatted(ex->type, ex->file, ex->line, ex->col, "%s", ex->message);
        result = NULL;
    } END_TRY
    
    return result;
}

/**
 * @brief Define a symbol in the current namespace
 * @param st Evaluation state
 * @param symbol Symbol to define
 * @param value Value to bind to symbol
 */
void ns_define(EvalState *st, CljObject *symbol, CljObject *value) {
    if (!st || !symbol || !value) return;
    
    // Get current namespace
    CljNamespace *ns = st->current_ns;
    if (!ns) {
        ns = ns_get_or_create("user", NULL);
        st->current_ns = ns;
    }
    
    // Create or update mappings
    if (!ns->mappings) {
        ns->mappings = (CljMap*)make_map(16);  // Initial capacity of 16
    }
    
    // Store symbol-value binding (overwrites existing)
    map_assoc((CljObject*)ns->mappings, symbol, value);
}
