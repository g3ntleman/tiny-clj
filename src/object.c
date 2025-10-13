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
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>
#include "object.h"
#include "seq.h"
#include "runtime.h"
#include "map.h"
#include "kv_macros.h"
#include "namespace.h"
#include "exception.h"  // For ExceptionHandler definition

// release_object_deep() function moved to memory.c


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
    
    // Shorten file path to show only from /src/ onwards
    const char *short_file = file;
    const char *src_pos = strstr(file, "/src/");
    if (src_pos) {
        short_file = src_pos + 1; // Skip the leading "/"
    }
    
    // Use generic RuntimeException if type is NULL
    const char *exception_type = (type != NULL) ? type : "RuntimeException";
    throw_exception(exception_type, message, short_file, line, code);
}

// Memory management functions moved to memory.c

// Helper functions for optimized structure
// Note: is_primitive_type function replaced by IS_PRIMITIVE_TYPE macro in header

// Global exception for runtime errors
static CLJException *global_exception = NULL;

// Global exception stack (independent of EvalState)
GlobalExceptionStack global_exception_stack = {0};

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
    
    if (global_exception_stack.top) {
        // Use global exception stack
        global_exception_stack.top->exception = global_exception;
        longjmp(global_exception_stack.top->jump_state, 1);
    } else {
        // No handler - unhandled exception
        printf("UNHANDLED EXCEPTION: %s: %s at %s:%d:%d\n", 
               type, message, file ? file : "<unknown>", line, col);
        release_exception(global_exception);
        global_exception = NULL;
        exit(1);
    }
}

// Note: set_global_eval_state() removed - Exception handling now independent of EvalState


// Exception management with reference counting (analogous to CljVector)
/** @brief Create exception with reference counting */
CLJException* create_exception(const char *type, const char *message, const char *file, int line, int col, CljObject *data) {
    if (!type || !message) return NULL;
    
    CLJException *exc = ALLOC(CLJException, 1);
    if (!exc) return NULL;
    
    // Initialize base object
    exc->base.type = CLJ_EXCEPTION;
    exc->base.rc = 1;  // Start with reference count 1
    exc->base.as.data = NULL;  // Not used for exceptions
    
    exc->type = strdup(type);
    exc->message = strdup(message);
    exc->file = file ? strdup(file) : NULL;
    exc->line = line;
    exc->col = col;
    exc->data = RETAIN(data);
    
    return exc;
}

void retain_exception(CLJException *exception) {
    if (!exception) return;
    retain((CljObject*)exception);
}

void release_exception(CLJException *exception) {
    if (!exception) return;
    release((CljObject*)exception);
}

/** @brief Create integer object */
CljObject* make_int(int x) {
    CljObject *v = ALLOC(CljObject, 1);
    if (!v) return NULL;
    v->type = CLJ_INT;
    v->rc = 1;
    v->as.i = x;
    
    return v;
}

CljObject* make_float(double x) {
    CljObject *v = ALLOC(CljObject, 1);
    if (!v) return NULL;
    v->type = CLJ_FLOAT;
    v->rc = 1;
    v->as.f = x;
    
    return v;
}

// retain() function moved to memory.c

// release() function moved to memory.c

// is_autorelease_pool_active() function moved to memory.c

// autorelease() function moved to memory.c

// autorelease_pool_push() function moved to memory.c

// autorelease_pool_pop_internal() function moved to memory.c

// autorelease_pool_pop() function moved to memory.c

// autorelease_pool_pop_specific() function moved to memory.c

// autorelease_pool_pop_legacy() function moved to memory.c

// autorelease_pool_cleanup_all() function moved to memory.c
// release_object_deep() function moved to memory.c

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
    if (!name) {
        throw_exception_formatted("ArgumentError", __FILE__, __LINE__, 0,
                "make_symbol: name cannot be NULL");
    }
    
    // Range check for name length
    if (strlen(name) >= SYMBOL_NAME_MAX_LEN) {
        throw_exception_formatted("ArgumentError", __FILE__, __LINE__, 0,
                "Symbol name '%s' exceeds maximum length of %d characters", 
                name, SYMBOL_NAME_MAX_LEN - 1);
    }
    
    CljSymbol *sym = ALLOC(CljSymbol, 1);
    if (!sym) {
        throw_exception_formatted("OutOfMemoryError", __FILE__, __LINE__, 0,
                "Failed to allocate memory for symbol '%s'", name);
    }
    
    sym->base.type = CLJ_SYMBOL;
    sym->base.rc = 1;
    
    // Copy name to fixed buffer
    strncpy(sym->name, name, SYMBOL_NAME_MAX_LEN - 1);
    sym->name[SYMBOL_NAME_MAX_LEN - 1] = '\0';  // Ensure null termination
    
    // Get or create namespace object
    if (ns) {
        sym->ns = ns_get_or_create(ns, NULL);  // NULL for file parameter
        if (!sym->ns) {
            RELEASE((CljObject*)sym);
            throw_exception_formatted("NamespaceError", __FILE__, __LINE__, 0,
                    "Failed to create namespace '%s' for symbol '%s'", ns, name);
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
    
    CLJException *exc = create_exception(type, message, file, line, col, data);
    if (!exc) return NULL;
    
    return (CljObject*)exc;
}

CljObject* make_function(CljObject **params, int param_count, CljObject *body, CljObject *closure_env, const char *name) {
    if (param_count < 0 || param_count > MAX_FUNCTION_PARAMS) return NULL;
    
    CljFunction *func = ALLOC(CljFunction, 1);
    if (!func) return NULL;
    
    func->base.type = CLJ_FUNC;  // Both CljFunc and CljFunction use CLJ_FUNC type
    func->base.rc = 1;
    func->param_count = param_count;
    func->body = RETAIN(body);
    func->closure_env = RETAIN(closure_env);
    func->name = name ? strdup(name) : NULL;
    
    // Parameter-Array kopieren
    if (param_count > 0 && params) {
        func->params = ALLOC(CljObject*, param_count);
        if (!func->params) {
            free(func);
            return NULL;
        }
        for (int i = 0; i < param_count; i++) {
            func->params[i] = RETAIN(params[i]);
        }
    } else {
        func->params = NULL;
    }
    
    return (CljObject*)func;
}

CljList* make_list(CljObject *first, CljList *rest) {
    CljList *list = ALLOC(CljList, 1);
    if (!list) return NULL;
    
    list->base.type = CLJ_LIST;
    list->base.rc = 1;
    list->head = RETAIN(first);
    list->tail = (CljList*)RETAIN(rest);
    
    return list;
}

char* to_string(CljObject *v) {
    // For nil: return empty string (Clojure str behavior)
    if (is_type(v, CLJ_NIL)) {
        return strdup("");
    }

    char buf[64];
    switch(v->type) {
        case CLJ_NIL:
            return strdup("");  // Empty string for str function

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
                // Return raw string WITHOUT quotes (for str function)
                char *str = (char*)v->as.data;
                return strdup(str);
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
                CljList *current = LIST_REST(list);
                while (current && count < 1000) {
                    if (current->head) {
                        elements[count++] = current->head;
                    }
                    current = current->tail;
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
                // Check if it's a CljFunction (Clojure function) or CljFunc (native function)
                CljFunction *clj_func = (CljFunction*)v;
                CljFunc *native_func = (CljFunc*)v;
                
                // First check if it's a native function (has fn pointer and no params/body)
                if (native_func && native_func->fn && !clj_func->params && !clj_func->body) {
                    // It's a native function (CljFunc)
                    if (native_func->name) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "#<native function %s>", native_func->name);
                        return strdup(buf);
                    }
                    return strdup("#<native function>");
                } else if (clj_func && (clj_func->params != NULL || clj_func->body != NULL || clj_func->closure_env != NULL)) {
                    // It's a Clojure function (CljFunction)
                    if (clj_func->name) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "#<function %s>", clj_func->name);
                        return strdup(buf);
                    } else {
                        return strdup("#<function>");
                    }
                } else {
                    // Fallback: unknown function type
                    return strdup("#<function>");
                }
            }

        case CLJ_SEQ:
            {
                CljSeqIterator *seq = as_seq(v);
                if (!seq) return strdup("()");
                
                // Direktes Drucken ohne Umkopieren
                char *result = strdup("(");
                if (!result) return strdup("()");
                
                bool first = true;
                CljObject *temp_seq = seq_create(seq->iter.container);
                if (!temp_seq) {
                    free(result);
                    return strdup("()");
                }
                
                while (!seq_empty(temp_seq)) {
                    CljObject *element = seq_first(temp_seq);
                    if (!element) {
                        seq_next(temp_seq);
                        continue;
                    }
                    
                    char *el_str = pr_str(element);
                    if (!el_str) {
                        seq_next(temp_seq);
                        continue;
                    }
                    
                    // String erweitern
                    size_t old_len = strlen(result);
                    size_t el_len = strlen(el_str);
                    size_t new_len = old_len + el_len + (first ? 0 : 1) + 1; // +1 für Leerzeichen oder \0
                    
                    char *new_result = realloc(result, new_len + 1); // +1 für \0
                    if (!new_result) {
                        free(result);
                        free(el_str);
                        RELEASE(temp_seq);
                        return strdup("()");
                    }
                    result = new_result;
                    
                    if (!first) {
                        strcat(result, " ");
                    }
                    strcat(result, el_str);
                    first = false;
                    
                    free(el_str);
                    seq_next(temp_seq);
                }
                
                strcat(result, ")");
                RELEASE(temp_seq);
                return result;
            }

        case CLJ_EXCEPTION:
            {
                CLJException *exc = (CLJException*)v;
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

char* pr_str(CljObject *v) {
    // pr_str shows nil as "nil" (not empty string)
    if (is_type(v, CLJ_NIL)) {
        return strdup("nil");
    }
    
    // pr_str adds quotes around strings
    if (is_type(v, CLJ_STRING)) {
        char *raw = to_string(v);
        if (!raw) return strdup("\"\"");
        
        size_t len = strlen(raw) + 3;  // +2 for quotes, +1 for \0
        char *result = ALLOC(char, len);
        snprintf(result, len, "\"%s\"", raw);
        free(raw);
        return result;
    }
    
    // For all other types: delegate to to_string
    return to_string(v);
}

// Korrekte Gleichheitsprüfung mit Inhalt-Vergleich
bool clj_equal(CljObject *a, CljObject *b) {
    if (a == b) return true;  // Pointer-Gleichheit (für Singletons und Symbole)
    if (!a || !b) return false;
    if (!is_type(a, b->type)) return false;
    
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
        if (old_value) RELEASE(old_value);
        
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
    if (!is_type(fn, CLJ_FUNC)) return NULL;
    
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
        heap_params[i] = RETAIN(argv[i]);
    }
    
    // Extend environment with parameters
    CljObject *call_env = env_extend_stack(func->closure_env, func->params, heap_params, argc);
    if (!call_env) {
        free(heap_params);
        return make_error("Failed to create function environment", NULL, 0, 0);
    }
    
    // Evaluate function body (simplified; would normally call eval())
    CljObject *result = func->body ? RETAIN(func->body) : clj_nil();
    
    // Release environment and parameter array
    RELEASE(call_env);
    free(heap_params);
    
    return result;
}

CljObject* clj_apply_function(CljObject *fn, CljObject **args, int argc, CljObject *env) {
    if (!is_type(fn, CLJ_FUNC)) return NULL;
    (void)env;
    
    // Evaluate arguments (simplified; would normally call eval())
    CljObject **eval_args = STACK_ALLOC(CljObject*, argc);
    for (int i = 0; i < argc; i++) {
        eval_args[i] = RETAIN(args[i]);
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
                // CljSymbol is a CljObject subtype, use standard free() for final cleanup
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
                if (list->tail) release_object((CljObject*)LIST_REST(list));
                free(list);
            }
            break;
        case CLJ_FUNC:
            {
                // Check if it's a CljFunction or CljFunc
                CljFunction *clj_func = (CljFunction*)obj;
                if (clj_func && (clj_func->params != NULL || clj_func->body != NULL || clj_func->closure_env != NULL)) {
                    // It's a CljFunction
                    if (clj_func->params) {
                        for (int i = 0; i < clj_func->param_count; i++) {
                            if (clj_func->params[i]) release_object(clj_func->params[i]);
                        }
                        free(clj_func->params);
                    }
                    if (clj_func->body) release_object(clj_func->body);
                    if (clj_func->closure_env) release_object(clj_func->closure_env);
                    if (clj_func->name) free((void*)clj_func->name);
                } else {
                    // It's a CljFunc (native function)
                    CljFunc *native_func = (CljFunc*)obj;
                    (void)native_func; // Suppress unused variable warning
#ifdef DEBUG
                    if (native_func && native_func->name) {
                        // In Debug-Builds ist name ein String-Literal, kein malloc'd String
                        // Daher kein free() nötig
                    }
#endif
                }
                free(obj);
            }
            break;
        case CLJ_EXCEPTION:
            {
                CLJException *exc = (CLJException*)obj;
                // Free exception-specific data
                if (exc->type) free((void*)exc->type);
                if (exc->message) free((void*)exc->message);
                if (exc->file) free((void*)exc->file);
                if (exc->data) RELEASE(exc->data);
                free(exc);
            }
            break;
        default:
            // Primitive Typen brauchen keine Freigabe
            free(obj);
            break;
    }
}

// Type-safe Casting now provided as static inline in header
