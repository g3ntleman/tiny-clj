/*
 * CljObject Implementation
 * 
 * Core data structure for Tiny-Clj representing all Clojure values:
 * - Basic types: symbols, keywords, numbers, strings, booleans
 * - Data structures: lists, vectors, maps, sets
 * - Functions: user-defined and built-in functions
 * - Meta-data support with global registry
 * - Reference counting for memory management
 * - Stack-allocated function call system
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include "CljObject.h"
#include <string.h>
#include "vector.h"
#include "memory_hooks.h"
#include "runtime.h"
#include "map.h"
#include "kv_macros.h"
#include "namespace.h"
#include "memory_profiler.h"

static void release_object_deep(CljObject *v);

/**
 * Convenience function for throwing exceptions with printf-style formatting
 * 
 * @param type Exception type (NULL for generic "RuntimeException")
 * @param file Source file name (use __FILE__)
 * @param line Line number (use __LINE__)
 * @param code Error code (use 0 for most cases)
 * @param format printf-style format string
 * @param ... Variable arguments for formatting
 */
void throw_exception_formatted(const char *type, const char *file, int line, int code, 
                              const char *format, ...) {
    char message[512];  // Increased buffer size for longer messages
    va_list args;
    
    va_start(args, format);
    int result = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // Additional safety: ensure null termination if message was truncated
    if (result >= (int)sizeof(message)) {
        // Message was truncated - ensure null termination
        message[sizeof(message)-1] = '\0';
    }
    
    // Use generic RuntimeException if type is NULL
    const char *exception_type = (type != NULL) ? type : "RuntimeException";
    throw_exception(exception_type, message, file, line, code);
}

// Autorelease pool backed by a weak vector for locality and fewer allocations
struct CljObjectPool { CljObject *backing; struct CljObjectPool *prev; };
static CljObjectPool *g_cv_pool_top = NULL;
static int g_pool_push_count = 0;  // Track push/pop balance

// Helper functions for optimized structure
// Note: is_primitive_type function replaced by IS_PRIMITIVE_TYPE macro in header

// Global exception for runtime errors
static CLJException *global_exception = NULL;
static EvalState *global_eval_state = NULL;

// Exception throwing (compatible with try/catch system)
// Ownership/RC:
// - create_exception returns rc=1 (no autorelease)
// - Ownership is transferred to EvalState->last_error before longjmp
// - Catch handler must set last_error=NULL and release_exception(ex)
/** @brief Throw an exception with type, message, and location */
void throw_exception(const char *type, const char *message, const char *file, int line, int col) {
    if (global_exception) {
        release_exception(global_exception);
    }
    global_exception = create_exception(type, message, file, line, col, NULL);
    
    if (global_eval_state) {
        // Use existing try/catch system
        // Transfer ownership to runtime slot before unwinding
        global_eval_state->last_error = (CljObject*)global_exception;
        longjmp(global_eval_state->jmp_env, 1);
    } else {
        // Fallback: printf and exit if no EvalState available
        printf("EXCEPTION: %s: %s at %s:%d:%d\n", type, message, file ? file : "<unknown>", line, col);
        // No catch → avoid leak
        release_exception(global_exception);
        global_exception = NULL;
        exit(1);
    }
}

// Set EvalState for try/catch
/** @brief Set global evaluation state */
void set_global_eval_state(void *state) {
    global_eval_state = (EvalState*)state;
}

// Exception management with reference counting (analogous to CljVector)
/** @brief Create exception with reference counting */
CLJException* create_exception(const char *type, const char *message, const char *file, int line, int col, CljObject *data) {
    if (!type || !message) return NULL;
    
    CLJException *exc = ALLOC(CLJException, 1);
    if (!exc) return NULL;
    
    exc->rc = 1;  // Start with reference count 1
    exc->type = strdup(type);
    exc->message = strdup(message);
    exc->file = file ? strdup(file) : NULL;
    exc->line = line;
    exc->col = col;
    exc->data = data ? (retain(data), data) : NULL;
    
    return exc;
}

void retain_exception(CLJException *exception) {
    if (!exception) return;
    exception->rc++;
}

void release_exception(CLJException *exception) {
    if (!exception) return;
    exception->rc--;
    if (exception->rc == 0) {
        // Free exception
        if (exception->type) free((void*)exception->type);
        if (exception->message) free((void*)exception->message);
        if (exception->file) free((void*)exception->file);
        if (exception->data) release(exception->data);
        free(exception);
    }
}

/** @brief Create integer object */
CljObject* make_int(int x) {
    CljObject *v = ALLOC(CljObject, 1);
    if (!v) return NULL;
    v->type = CLJ_INT;
    v->rc = 1;
    v->as.i = x;
    
    CREATE(v);
    return v;
}

CljObject* make_float(double x) {
    CljObject *v = ALLOC(CljObject, 1);
    if (!v) return NULL;
    v->type = CLJ_FLOAT;
    v->rc = 1;
    v->as.f = x;
    
    CREATE(v);
    return v;
}

/** @brief Increment reference count */
void retain(CljObject *v) {
    if (!v) return;
    
    
    // Singletons have no reference counting
    if (IS_PRIMITIVE_TYPE(v->type)) return;
    // Guard: empty vector/map singletons must not be retained
    if (v->type == CLJ_VECTOR) {
        CljPersistentVector *vec = as_vector(v);
        if (vec && vec->base.rc == 0 && vec->data == NULL) return;
    }
    if (v->type == CLJ_MAP) {
        CljMap *map = as_map(v);
        if (map && map->base.rc == 0 && map->data == NULL) return;
    }
    v->rc++;
    
    // RETAIN(v); // Hook now called via RELEASE macro
}

/** @brief Decrement reference count and free if zero */
void release(CljObject *v) {
    if (!v) return;
    
    // Singletons have no reference counting
    if (IS_PRIMITIVE_TYPE(v->type)) {
        // Primitive types are never actually freed, so don't track as deallocation
        return;
    }
    // Guard: empty vector/map singletons must not be released
    if (v->type == CLJ_VECTOR) {
        CljPersistentVector *vec = as_vector(v);
        if (vec && vec->base.rc == 0 && vec->data == NULL) return;
    }
    if (v->type == CLJ_MAP) {
        CljMap *map = as_map(v);
        if (map && map->base.rc == 0 && map->data == NULL) return;
    }
    if (v->rc <= 0) {
        throw_exception_formatted("DoubleFreeError", __FILE__, __LINE__, 0,
                "Double free detected! Object %p (type=%d, rc=%d) was freed twice. "
                "This indicates a memory management bug.", v, v->type, v->rc);
    }
    v->rc--;
    
    // RELEASE(v); // Hook now called via RELEASE macro
    
    if (v->rc == 0) { 
        DEALLOC(v); // Hook for memory profiling
        release_object_deep(v); 
        free(v); 
    }
}

// Helper function to check if autorelease pool is active
bool is_autorelease_pool_active(void) {
    return g_cv_pool_top != NULL;
}

CljObject *autorelease(CljObject *v) {
    if (!v) return NULL;
    
    // 🚨 ASSERTION: Autorelease pool must exist
    if (!g_cv_pool_top) {
        throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0,
                "autorelease() called without active autorelease pool! Object %p (type=%d) will not be automatically freed. "
                "This indicates missing cljvalue_pool_push() or premature cljvalue_pool_pop().", 
                v, v ? v->type : -1);
    }
    
    // Weak vector push: does not retain the item
    vector_push_inplace(g_cv_pool_top->backing, v);
    
    // Memory profiling
    MEMORY_PROFILER_TRACK_AUTORELEASE(v);
    
    return v;
}

CljObjectPool *cljvalue_pool_push() {
    CljObjectPool *p = ALLOC(CljObjectPool, 1);
    if (!p) return NULL;
    p->backing = make_weak_vector(16);
    p->prev = g_cv_pool_top;
    g_cv_pool_top = p;
    g_pool_push_count++;  // Increment push counter
    return p;
}

void cljvalue_pool_pop(CljObjectPool *pool) {
    // 🚨 ASSERTION: Check for pop/push imbalance (before early return)
    if (g_pool_push_count <= 0) {
        throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0,
                "cljvalue_pool_pop() called more times than cljvalue_pool_push()! "
                "Push count: %d, attempting to pop pool %p. "
                "This indicates unbalanced pool operations.", 
                g_pool_push_count, pool);
    }
    
    if (!pool || g_cv_pool_top != pool) return;
    
    CljPersistentVector *vec = as_vector(pool->backing);
    if (vec) {
        for (int i = vec->count - 1; i >= 0; --i) {
            if (vec->data[i]) release(vec->data[i]);
            vec->data[i] = NULL;
        }
        vec->count = 0;
    }
    g_cv_pool_top = pool->prev;
    g_pool_push_count--;  // Decrement push counter
    // Release the weak vector object (will free only its backing store)
    if (pool->backing) release(pool->backing);
    free(pool);
}

// Globale Cleanup-Funktion für alle Autorelease-Pools
void cljvalue_pool_cleanup_all() {
    while (g_cv_pool_top) {
        cljvalue_pool_pop(g_cv_pool_top);
    }
}
// Central dispatcher for finalizers based on type tag
static void release_object_deep(CljObject *v) {
    if (!v) return;
    
    // Primitive values need no finalizer
    if (IS_PRIMITIVE_TYPE(v->type)) {
        return;
    }
    
    // Dispatcher: Decide based on type tag whether finalizer is needed
    switch (v->type) {
        case CLJ_STRING:
            // String finalizer: free memory
            if (v->as.data) free(v->as.data);
            break;
            
        case CLJ_SYMBOL:
            // Symbols are embedded; nothing to free here (interned / no RC)
            break;
            
        case CLJ_VECTOR:
            // Vector finalizer: embedded object; free backing store and elements only
            {
                CljPersistentVector *vec = as_vector(v);
                if (vec) {
                    for (int i = 0; i < vec->count; ++i) {
                        if (vec->data[i]) release(vec->data[i]);
                    }
                    free(vec->data);
                }
            }
            break;
        case CLJ_WEAK_VECTOR:
            // Weak vector finalizer: does not retain on push; releasing elements here is still required
            {
                CljPersistentVector *vec = as_vector(v);
                if (vec) {
                    for (int i = 0; i < vec->count; ++i) {
                        if (vec->data[i]) release(vec->data[i]);
                    }
                    free(vec->data);
                }
            }
            break;
            
        case CLJ_MAP:
            // Map finalizer: separate struct in as.data; free pairs and struct
            {
                CljMap *map = as_map(v);
                if (map) {
                    for (int i = 0; i < map->count * 2; ++i) {
                        if (map->data[i]) release(map->data[i]);
                    }
                    free(map->data);
                    free(map);
                }
            }
            break;
            
        case CLJ_LIST:
            // List finalizer: separate struct in as.data; walk and free
            {
                CljList *list = as_list(v);
                if (list) {
                    CljObject *node = list->head;
                    while (node) {
                        CljList *node_list = as_list(node);
                        CljObject *next = node_list ? node_list->tail : NULL;
                        release(node);
                        node = next;
                    }
                    free(list);
                }
            }
            break;
            
        case CLJ_FUNC:
            // Function finalizer: embedded; free internals only
            {
                CljFunction *func = as_function(v);
                if (func) {
                    if (func->params) {
                        for (int i = 0; i < func->param_count; i++) {
                            if (func->params[i]) release(func->params[i]);
                        }
                        free(func->params);
                    }
                    if (func->body) release(func->body);
                    if (func->closure_env) release(func->closure_env);
                    if (func->name) free((void*)func->name);
                }
            }
            break;
            
        case CLJ_EXCEPTION:
            // Exception finalizer: use exception RC management
            {
                CLJException *exc = (CLJException*)v->as.data;
                if (exc) {
                    release_exception(exc);
                }
            }
            break;
            
        case CLJ_INT:
        case CLJ_FLOAT:
        case CLJ_BOOL:
        case CLJ_NIL:
            // Primitive types: no finalizer needed
            break;
            
        default:
            // Unknown type: no finalizer
            break;
    }
}

// moved to string.c

// make_nil() and make_bool() functions removed - use clj_nil(), clj_true(), clj_false() instead

// vector functions moved to vector.c

// Forward declarations for early use
static void init_static_singletons(void);
// empty map singleton moved to map.c

// Forward declaration to allow early use
static void init_static_singletons(void);

// map functions moved to map.c

CljObject* make_symbol(const char *name, const char *ns) {
    if (!name) return NULL;
    
    // Range check for name length
    if (strlen(name) >= SYMBOL_NAME_MAX_LEN) {
        printf("Error: Symbol name '%s' exceeds maximum length of %d characters\n", 
               name, SYMBOL_NAME_MAX_LEN - 1);
        return NULL;
    }
    
    CljSymbol *sym = ALLOC(CljSymbol, 1);
    if (!sym) return NULL;
    
    sym->base.type = CLJ_SYMBOL;
    sym->base.rc = 1;
    
    // Copy name to fixed buffer
    strncpy(sym->name, name, SYMBOL_NAME_MAX_LEN - 1);
    sym->name[SYMBOL_NAME_MAX_LEN - 1] = '\0';  // Ensure null termination
    
    // Get or create namespace object
    if (ns) {
        sym->ns = ns_get_or_create(ns, NULL);  // NULL for file parameter
        if (!sym->ns) {
            free(sym);
            return NULL;
        }
        // Namespace is already retained by ns_get_or_create
    } else {
        sym->ns = NULL;  // No namespace
    }
    
    return (CljObject*)sym;
}

CljObject* make_error(const char *message, const char *file, int line, int col) {
    return make_exception("Error", message, file, line, col, NULL);
}

CljObject* make_exception(const char *type, const char *message, const char *file, int line, int col, CljObject *data) {
    if (!type || !message) return NULL;
    CljObject *v = ALLOC(CljObject, 1);
    if (!v) return NULL;
    
    CLJException *exc = create_exception(type, message, file, line, col, data);
    if (!exc) { free(v); return NULL; }
    
    v->type = CLJ_EXCEPTION;
    v->rc = 1;
    v->as.data = exc;
    
    return v;
}

CljObject* make_function(CljObject **params, int param_count, CljObject *body, CljObject *closure_env, const char *name) {
    if (param_count < 0 || param_count > MAX_FUNCTION_PARAMS) return NULL;
    
    CljFunction *func = ALLOC(CljFunction, 1);
    if (!func) return NULL;
    
    func->base.type = CLJ_FUNC;
    func->base.rc = 1;
    func->param_count = param_count;
    func->body = body ? (retain(body), body) : NULL;
    func->closure_env = closure_env ? (retain(closure_env), closure_env) : NULL;
    func->name = name ? strdup(name) : NULL;
    
    // Parameter-Array kopieren
    if (param_count > 0 && params) {
        func->params = ALLOC(CljObject*, param_count);
        if (!func->params) {
            free(func);
            return NULL;
        }
        for (int i = 0; i < param_count; i++) {
            func->params[i] = params[i] ? (retain(params[i]), params[i]) : NULL;
        }
    } else {
        func->params = NULL;
    }
    
    return (CljObject*)func;
}

CljObject* make_list() {
    CljList *list = ALLOC(CljList, 1);
    if (!list) return NULL;
    
    list->base.type = CLJ_LIST;
    list->base.rc = 1;
    list->head = NULL;
    list->tail = NULL;
    
    CljObject *obj = ALLOC(CljObject, 1);
    if (!obj) {
        free(list);
        return NULL;
    }
    
    obj->type = CLJ_LIST;
    obj->rc = 1;
    obj->as.data = (void*)list;
    
    return obj;
}

char* pr_str(CljObject *v) {
    if (!v) return strdup("nil");

    char buf[64];
    switch(v->type) {
        case CLJ_NIL:
            return strdup("nil");

        case CLJ_INT:
            snprintf(buf, sizeof(buf), "%d", v->as.i);
            return strdup(buf);

        case CLJ_FLOAT:
            snprintf(buf, sizeof(buf), "%g", v->as.f);
            return strdup(buf);

        case CLJ_BOOL:
            return strdup(v->as.b ? "true" : "false");

        case CLJ_STRING:
            {
                char *str = (char*)v->as.data;
                size_t len = strlen(str) + 3;
                char *s = ALLOC(char, len);
                snprintf(s, len, "\"%s\"", str);
                return s;
            }

        case CLJ_SYMBOL:
            {
                CljSymbol *sym = as_symbol(v);
                if (!sym) return strdup("nil");
                if (sym->ns) {  // Check if namespace exists
                    // Get namespace name from the namespace object
                    CljSymbol *ns_sym = as_symbol(sym->ns->name);
                    if (ns_sym) {
                        size_t len = strlen(ns_sym->name) + 1 + strlen(sym->name) + 1;
                        char *s = ALLOC(char, len);
                        snprintf(s, len, "%s/%s", ns_sym->name, sym->name);
                        return s;
                    }
                }
                return strdup(sym->name);
            }

        case CLJ_VECTOR:
            {
                CljPersistentVector *vec = as_vector(v);
                if (!vec) return strdup("[]");
                size_t cap = 2; // [ ]
                for (int i = 0; i < vec->count; i++) {
                    char *el = pr_str(vec->data[i]);
                    cap += strlen(el) + 1;
                    free(el);
                }
                char *s = ALLOC(char, cap+1);
                strcpy(s, "[");
                for (int i = 0; i < vec->count; i++) {
                    char *el = pr_str(vec->data[i]);
                    strcat(s, el);
                    if (i < vec->count-1) strcat(s, " ");
                    free(el);
                }
                strcat(s, "]");
                return s;
            }

        case CLJ_LIST:
            {
                CljList *list = as_list(v);
                if (!list) return strdup("()");
                
                // Sammle alle Elemente in einem Array
                CljObject *elements[1000]; // Max 1000 Elemente
                int count = 0;
                
                // Head hinzufügen
                if (list->head) {
                    elements[count++] = list->head;
                }
                
                // Tail-Elemente hinzufügen
                CljObject *current = list->tail;
                while (current && count < 1000) {
                    CljList *current_list = as_list(current);
                    if (current_list && current_list->head) {
                        elements[count++] = current_list->head;
                    }
                    current = current_list ? current_list->tail : NULL;
                }
                
                // Berechne benötigte Kapazität
                size_t cap = 2; // ( )
                for (int i = 0; i < count; i++) {
                    char *el = pr_str(elements[i]);
                    cap += strlen(el) + 1;
                    free(el);
                }
                
                // Erstelle String
                char *s = ALLOC(char, cap+1);
                strcpy(s, "(");
                for (int i = 0; i < count; i++) {
                    char *el = pr_str(elements[i]);
                    strcat(s, el);
                    if (i < count-1) strcat(s, " ");
                    free(el);
                }
                strcat(s, ")");
                return s;
            }

        case CLJ_MAP:
            {
                CljMap *map = as_map(v);
                if (!map) return strdup("{}");
                size_t cap = 2; // { }
                for (int i = 0; i < map->count; i++) {
                    CljObject *k = KV_KEY(map->data, i);
                    CljObject *val = KV_VALUE(map->data, i);
                    if (!k) continue;
                    char *ks = pr_str(k);
                    char *vs = pr_str(val);
                    cap += strlen(ks) + strlen(vs) + 2;
                    free(ks); free(vs);
                }
                char *s = ALLOC(char, cap+1);
                strcpy(s, "{");
                bool first = true;
                for (int i = 0; i < map->count; i++) {
                    CljObject *k = KV_KEY(map->data, i);
                    CljObject *val = KV_VALUE(map->data, i);
                    if (!k) continue;
                    if (!first) strcat(s, " ");
                    char *ks = pr_str(k);
                    char *vs = pr_str(val);
                    strcat(s, ks);
                    strcat(s, " ");
                    strcat(s, vs);
                    free(ks); free(vs);
                    first = false;
                }
                strcat(s, "}");
                return s;
            }

        case CLJ_FUNC:
            {
                CljFunction *func = as_function(v);
                if (!func) return strdup("#<function>");
                if (func->name) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "#<function %s>", func->name);
                    return strdup(buf);
                } else {
                    return strdup("#<function>");
                }
            }

        case CLJ_EXCEPTION:
            {
                CLJException *exc = (CLJException*)v->as.data;
                char *result;
                if (exc->file) {
                    char buf[1024];
                    snprintf(buf, sizeof(buf), "%s: %s at %s:%d:%d", 
                            exc->type, exc->message, 
                            exc->file, exc->line, exc->col);
                    result = strdup(buf);
                } else {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "%s: %s at line %d, col %d", 
                            exc->type, exc->message, 
                            exc->line, exc->col);
                    result = strdup(buf);
                }
                return result;
            }

        default:
            return strdup("#<unknown>");
    }
}

// Korrekte Gleichheitsprüfung mit Inhalt-Vergleich
bool clj_equal(CljObject *a, CljObject *b) {
    if (a == b) return true;  // Pointer-Gleichheit (für Singletons und Symbole)
    if (!a || !b) return false;
    if (a->type != b->type) return false;
    
    // Inhalt-Vergleich basierend auf Typ
    // Hinweis: CLJ_NIL, CLJ_BOOL, CLJ_SYMBOL werden bereits durch Pointer-Vergleich abgefangen
    switch (a->type) {
        // Primitive Typen - direkter Vergleich
        case CLJ_INT:
            return a->as.i == b->as.i;
            
        case CLJ_FLOAT:
            return a->as.f == b->as.f;
            
        // Komplexe Typen - Inhalt-Vergleich
        case CLJ_STRING: {
            char *str_a = (char*)a->as.data;
            char *str_b = (char*)b->as.data;
            if (!str_a || !str_b) return false;
            return strcmp(str_a, str_b) == 0;
        }
        
        case CLJ_VECTOR: {
            CljPersistentVector *vec_a = (CljPersistentVector*)a->as.data;
            CljPersistentVector *vec_b = (CljPersistentVector*)b->as.data;
            if (!vec_a || !vec_b) return false;
            if (vec_a->count != vec_b->count) return false;
            for (int i = 0; i < vec_a->count; i++) {
                if (!clj_equal(vec_a->data[i], vec_b->data[i])) return false;
            }
            return true;
        }
        
        case CLJ_MAP: {
            CljMap *map_a = as_map(a);
            CljMap *map_b = as_map(b);
            if (!map_a || !map_b) return false;
            if (map_a->count != map_b->count) return false;
            for (int i = 0; i < map_a->count; i++) {
                CljObject *key_a = KV_KEY(map_a->data, i);
                CljObject *val_a = KV_VALUE(map_a->data, i);
                CljObject *val_b = map_get(b, key_a);
                if (!clj_equal(val_a, val_b)) return false;
            }
            return true;
        }
        
        case CLJ_SYMBOL: {
            // Symbol comparison: name and namespace must match
            CljSymbol *sym_a = as_symbol(a);
            CljSymbol *sym_b = as_symbol(b);
            if (strcmp(sym_a->name, sym_b->name) != 0) return false;
            
            // Compare namespaces (both NULL or both same object)
            if (sym_a->ns == sym_b->ns) return true;
            if (!sym_a->ns || !sym_b->ns) return false;
            
            // Both have namespaces - compare their names
            CljSymbol *ns_a = as_symbol(sym_a->ns->name);
            CljSymbol *ns_b = as_symbol(sym_b->ns->name);
            if (!ns_a || !ns_b) return false;
            return strcmp(ns_a->name, ns_b->name) == 0;
        }
        
        // Referenz-Typen - nur Pointer-Vergleich (bereits durch a == b abgefangen)
        case CLJ_LIST:
        case CLJ_FUNC:
            // Diese Cases sollten nie erreicht werden, da sie bereits durch Pointer-Vergleich abgefangen werden
            // Aber falls doch, sind sie nur bei gleicher Instanz gleich
            return a == b;
        
        // Unbekannte oder nicht unterstützte Typen
        default:
            return false;
    }
}

// Map functions are implemented in map.c

// Symbol table for real interning
SymbolEntry *symbol_table = NULL;

// Hash function for symbol names (unused)
// Removed to silence unused-function warnings

// Find symbol in the table
static SymbolEntry* symbol_table_find(const char *ns, const char *name) {
    SymbolEntry *entry = symbol_table;
    while (entry) {
        if (entry->ns && ns) {
            if (strcmp(entry->ns, ns) == 0 && strcmp(entry->name, name) == 0) {
                return entry;
            }
        } else if (!entry->ns && !ns) {
            if (strcmp(entry->name, name) == 0) {
                return entry;
            }
        }
        entry = entry->next;
    }
    return NULL;
}

// Add symbol to the table
static SymbolEntry* symbol_table_add(const char *ns, const char *name, CljObject *symbol) {
    SymbolEntry *entry = ALLOC(SymbolEntry, 1);
    if (!entry) return NULL;
    
    entry->ns = ns ? strdup(ns) : NULL;
    entry->name = strdup(name);
    entry->symbol = symbol;
    entry->next = symbol_table;
    symbol_table = entry;
    
    return entry;
}

// Actual symbol interning
CljObject* intern_symbol(const char *ns, const char *name) {
    if (!name) return NULL;
    
    // Suche zuerst in der Symbol-Table
    SymbolEntry *existing = symbol_table_find(ns, name);
    if (existing) {
        return existing->symbol;  // Gleicher Pointer!
    }
    
    // Symbol nicht gefunden, erstelle neues
    CljObject *symbol = make_symbol(name, ns);
    if (!symbol) return NULL;
    
    // Füge zur Symbol-Table hinzu
    symbol_table_add(ns, name, symbol);
    
    return symbol;
}

// Global symbols (without namespace)
CljObject* intern_symbol_global(const char *name) {
    return intern_symbol(NULL, name);
}

// Clean up symbol table (ONLY for test cleanup, not regular symbols)
void symbol_table_cleanup() {
// Symbols should live until program end!
// Free only SymbolEntry structures, NOT the symbols themselves
    SymbolEntry *entry = symbol_table;
    while (entry) {
        SymbolEntry *next = entry->next;
        
        if (entry->ns) free(entry->ns);
        if (entry->name) free(entry->name);
        // NICHT: if (entry->symbol) release(entry->symbol);
        // Symbole bleiben im Speicher bis zum Programmende
        free(entry);
        
        entry = next;
    }
    symbol_table = NULL;
}

// Number of symbols in the table
int symbol_count() {
    int count = 0;
    SymbolEntry *entry = symbol_table;
    while (entry) {
        count++;
        entry = entry->next;
    }
    return count;
}

#ifdef ENABLE_META
// Meta registry for metadata
CljObject *meta_registry = NULL;

void meta_registry_init() {
    {
        meta_registry = make_map(32); // Initial capacity for metadata entries
    }
}

void meta_registry_cleanup() {
    // Meta registry should live until program end!
    // DO NOT: release(meta_registry);
    // Meta registry stays allocated until program end
    meta_registry = NULL;
}

void meta_set(CljObject *v, CljObject *meta) {
    if (!v) return;
    
    meta_registry_init();
    if (!meta_registry) return;
    
    // Use the pointer as key (simple implementation)
    // A real implementation would use a hash of the pointer
    map_assoc(meta_registry, v, meta);
}

CljObject* meta_get(CljObject *v) {
    if (!v || !meta_registry) return NULL;
    
    return map_get(meta_registry, v);
}

void meta_clear(CljObject *v) {
    if (!v || !meta_registry) return;
    
    // Find the entry and remove it using KV macros
    int index = KV_FIND_INDEX(((CljMap*)meta_registry->as.data)->data, ((CljMap*)meta_registry->as.data)->count, v);
    if (index >= 0) {
        // Entry found; remove it
        CljObject *old_value = KV_VALUE(((CljMap*)meta_registry->as.data)->data, index);
        if (old_value) release(old_value);
        
        // Shift following elements to the left
        for (int j = index; j < ((CljMap*)meta_registry->as.data)->count - 1; j++) {
            KV_SET_PAIR(((CljMap*)meta_registry->as.data)->data, j,
                       KV_KEY(((CljMap*)meta_registry->as.data)->data, j + 1),
                       KV_VALUE(((CljMap*)meta_registry->as.data)->data, j + 1));
        }
        
        ((CljMap*)meta_registry->as.data)->count--;
    }
}


#endif

// Static singletons - these live forever and are never freed
static CljObject clj_nil_singleton;
static CljObject clj_true_singleton;
static CljObject clj_false_singleton;
static CljObject clj_empty_map_singleton;

// Initialize static singletons
static void init_static_singletons() {
    // Initialize NIL singleton
    clj_nil_singleton.type = CLJ_NIL;
    clj_nil_singleton.rc = 0; // Singletons do not use reference counting
    clj_nil_singleton.as.i = 0;
    
    // Initialize TRUE singleton
    clj_true_singleton.type = CLJ_BOOL;
    clj_true_singleton.rc = 0; // Singletons do not use reference counting
    clj_true_singleton.as.b = true;
    
    // Initialize FALSE singleton
    clj_false_singleton.type = CLJ_BOOL;
    clj_false_singleton.rc = 0; // Singletons do not use reference counting
    clj_false_singleton.as.b = false;
    

    // Initialize empty map singleton
    clj_empty_map_singleton.type = CLJ_MAP;
    clj_empty_map_singleton.rc = 0; // Singletons do not use reference counting
    clj_empty_map_singleton.as.data = NULL; // No pairs = empty map
}

// Singleton access functions
CljObject* clj_nil() {
    static bool initialized = false;
    if (!initialized) {
        init_static_singletons();
        initialized = true;
    }
    return &clj_nil_singleton;
}

CljObject* clj_true() {
    static bool initialized = false;
    if (!initialized) {
        init_static_singletons();
        initialized = true;
    }
    return &clj_true_singleton;
}

CljObject* clj_false() {
    static bool initialized = false;
    if (!initialized) {
        init_static_singletons();
        initialized = true;
    }
    return &clj_false_singleton;
}

// clj_empty_vector moved to vector.c

// clj_empty_map() no longer part of public API; make_map(0) returns singleton


// Stack-based environment helpers (outside of #ifdef)
CljObject* env_extend_stack(CljObject *parent_env, CljObject **params, CljObject **values, int count) {
    if (count > MAX_FUNCTION_PARAMS) return NULL;
    (void)parent_env; (void)params; (void)values;
    
    // Simplified implementation: just return an empty map
    // Parameter binding skipped for this stage
    CljObject *new_env = make_map(4);
    if (!new_env) return NULL;
    
    return new_env;
}

CljObject* env_get_stack(CljObject *env, CljObject *key) {
    if (!env || !key) return NULL;
    
    // Direct map lookup
    return map_get(env, key);
}

void env_set_stack(CljObject *env, CljObject *key, CljObject *value) {
    if (!env || !key) return;
    
    map_assoc(env, key, value);
}

// Function call implementation using stack allocation
CljObject* clj_call_function(CljObject *fn, int argc, CljObject **argv) {
    if (!fn || fn->type != CLJ_FUNC) return NULL;
    
    // Arity check
    CljFunction *func = as_function(fn);
    if (!func) {
        return make_error("Invalid function object", NULL, 0, 0);
    }
    if (argc != func->param_count) {
        return make_error("Arity mismatch in function call", NULL, 0, 0);
    }
    
    // Heap-allocated parameter array
    CljObject **heap_params = ALLOC(CljObject*, argc);
    for (int i = 0; i < argc; i++) {
        heap_params[i] = argv[i] ? (retain(argv[i]), argv[i]) : NULL;
    }
    
    // Extend environment with parameters
    CljObject *call_env = env_extend_stack(func->closure_env, func->params, heap_params, argc);
    if (!call_env) {
        free(heap_params);
        return make_error("Failed to create function environment", NULL, 0, 0);
    }
    
    // Evaluate function body (simplified; would normally call eval())
    CljObject *result = func->body ? (retain(func->body), func->body) : clj_nil();
    
    // Release environment and parameter array
    release(call_env);
    free(heap_params);
    
    return result;
}

CljObject* clj_apply_function(CljObject *fn, CljObject **args, int argc, CljObject *env) {
    if (!fn || fn->type != CLJ_FUNC) return NULL;
    (void)env;
    
    // Evaluate arguments (simplified; would normally call eval())
    CljObject **eval_args = STACK_ALLOC(CljObject*, argc);
    for (int i = 0; i < argc; i++) {
        eval_args[i] = args[i] ? (retain(args[i]), args[i]) : NULL;
    }
    
    return clj_call_function(fn, argc, eval_args);
}

// Polymorphe Funktionen für Subtyping
CljObject* create_object(CljType type) {
    CljObject *obj = ALLOC(CljObject, 1);
    if (!obj) return NULL;
    
    obj->type = type;
    obj->rc = 1;
    
    // Initialisiere je nach Typ
    switch (type) {
        case CLJ_INT:
            obj->as.i = 0;
            break;
        case CLJ_FLOAT:
            obj->as.f = 0.0;
            break;
        case CLJ_BOOL:
            obj->as.b = 0;
            break;
        case CLJ_NIL:
            obj->as.i = 0;
            break;
        default:
            obj->as.data = NULL;
            break;
    }
    
    return obj;
}

void retain_object(CljObject *obj) {
    if (!obj) return;
    obj->rc++;
}

void release_object(CljObject *obj) {
    if (!obj) return;
    obj->rc--;
    if (obj->rc == 0) {
        free_object(obj);
    }
}

void free_object(CljObject *obj) {
    if (!obj) return;

    // Type-spezifische Freigabe
    switch (obj->type) {
        case CLJ_STRING:
            if (obj->as.data) free(obj->as.data);
            free(obj);
            break;
        case CLJ_SYMBOL:
            {
                CljSymbol *sym = (CljSymbol*)obj;
                // Note: CljNamespace doesn't have reference counting yet
                // TODO: Implement reference counting for CljNamespace
                free(sym);
            }
            break;
        case CLJ_VECTOR:
            {
                CljPersistentVector *vec = (CljPersistentVector*)obj;
                if (vec->data) {
                    for (int i = 0; i < vec->count; i++) {
                        if (vec->data[i]) release_object(vec->data[i]);
                    }
                    free(vec->data);
                }
                free(vec);
            }
            break;
        case CLJ_MAP:
            {
                CljMap *map = (CljMap*)obj;
                if (map->data) {
                    for (int i = 0; i < map->count * 2; i++) {
                        if (map->data[i]) release_object(map->data[i]);
                    }
                    free(map->data);
                }
                free(map);
            }
            break;
        case CLJ_LIST:
            {
                CljList *list = (CljList*)obj;
                if (list->head) release_object(list->head);
                if (list->tail) release_object(list->tail);
                free(list);
            }
            break;
        case CLJ_FUNC:
            {
                CljFunction *func = (CljFunction*)obj;
                if (func->params) {
                    for (int i = 0; i < func->param_count; i++) {
                        if (func->params[i]) release_object(func->params[i]);
                    }
                    free(func->params);
                }
                if (func->body) release_object(func->body);
                if (func->closure_env) release_object(func->closure_env);
                if (func->name) free((void*)func->name);
                free(func);
            }
            break;
        case CLJ_EXCEPTION:
            {
                CLJException *exc = (CLJException*)obj->as.data;
                if (exc) {
                    release_exception(exc);
                }
                free(obj);
            }
            break;
        default:
            // Primitive Typen brauchen keine Freigabe
            free(obj);
            break;
    }
}

// Type-safe Casting now provided as static inline in header
