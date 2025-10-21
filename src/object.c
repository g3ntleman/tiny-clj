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
#include "memory.h"
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

// Global exception stack (independent of EvalState)
GlobalExceptionStack global_exception_stack = {0};

// Exception throwing (compatible with try/catch system)
// Ownership/RC:
// - create_exception returns rc=1 (no autorelease)
// - Ownership is transferred to exception stack before longjmp
// - Catch handler automatically releases exception in END_TRY
/** @brief Throw an exception with type, message, and location */
void throw_exception(const char *type, const char *message, const char *file, int line, int col) {
    if (!global_exception_stack.top) {
        // No handler - unhandled exception
        printf("UNHANDLED EXCEPTION: %s: %s at %s:%d:%d\n", 
               type, message, file ? file : "<unknown>", line, col);
        exit(1);
    }
    
    CLJException *exception = make_exception(type, message, file, line, col);
    if (!exception) {
        printf("FAILED TO CREATE EXCEPTION: %s: %s at %s:%d:%d\n", 
               type, message, file ? file : "<unknown>", line, col);
        exit(1);
    }
    
    // Store exception in thread-local variable
    g_current_exception = exception;
    longjmp(global_exception_stack.top->jump_state, 1);
}


// Note: set_global_eval_state() removed - Exception handling now independent of EvalState


// Exception management with reference counting (analogous to CljVector)
/** @brief Create exception with reference counting */
CLJException* make_exception(const char *type, const char *message, const char *file, int line, int col) {
    if (!type || !message) return NULL;
    
    CLJException *exc = ALLOC(CLJException, 1);
    if (!exc) return NULL;
    
    // Initialize base object
    exc->base.type = CLJ_EXCEPTION;
    exc->base.rc = 1;  // Start with reference count 1
    // exc->base.as.data = NULL;  // Not used for exceptions - Union removed
    
    // Copy strings directly into the structure (no strdup needed)
    strncpy(exc->type, type, sizeof(exc->type) - 1);
    exc->type[sizeof(exc->type) - 1] = '\0';  // Ensure null termination
    
    strncpy(exc->message, message, sizeof(exc->message) - 1);
    exc->message[sizeof(exc->message) - 1] = '\0';  // Ensure null termination
    
    if (file) {
        strncpy(exc->file, file, sizeof(exc->file) - 1);
        exc->file[sizeof(exc->file) - 1] = '\0';  // Ensure null termination
    } else {
        exc->file[0] = '\0';  // Empty string
    }
    
    exc->line = line;
    exc->col = col;
    
    return exc;
}



/** @brief Create integer object */
// make_int() and make_float() removed - use fixnum() and make_fixed() instead

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

// make_nil() and make_bool() functions removed - use NULL, make_special() instead

// vector functions moved to vector.c

// Forward declarations for early use
// empty map singleton moved to map.c

// map functions moved to map.c

// make_symbol_old function removed - use make_symbol from value.h instead

CljObject* make_error(const char *message, const char *file, int line, int col) {
    return make_exception_wrapper("Error", message, file, line, col);
}

CljObject* make_exception_wrapper(const char *type, const char *message, const char *file, int line, int col) {
    if (!type || !message) return NULL;
    
    CLJException *exc = make_exception(type, message, file, line, col);
    
    return (CljObject*)exc;
}

CljObject* make_function(CljObject **params, int param_count, CljObject *body, CljObject *closure_env, const char *name) {
    if (param_count < 0 || param_count > MAX_FUNCTION_PARAMS) return NULL;
    
    CljFunction *func = (CljFunction*)alloc(sizeof(CljFunction), 1, CLJ_CLOSURE);
    if (!func) return NULL;
    
    func->base.type = CLJ_CLOSURE;  // Interpreted functions use CLJ_CLOSURE type
    func->base.rc = 1;
    func->param_count = param_count;
    func->body = RETAIN(body);
    func->closure_env = RETAIN(closure_env);
    func->name = name ? strdup(name) : NULL;
    
    // Parameter-Array kopieren
    if (param_count > 0 && params) {
        func->params = (CljObject**)malloc(sizeof(CljObject*) * param_count);
        if (!func->params) {
            DEALLOC(func);
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

CljObject* make_list(CljObject *first, CljObject *rest) {
    CljList *list = ALLOC(CljList, 1);
    if (!list) return NULL;
    
    list->base.type = CLJ_LIST;
    list->base.rc = 1;
    list->first = RETAIN(first);
    list->rest = RETAIN(rest);
    
    return (CljObject*)list;
}

char* to_string(CljObject *v) {
    // Handle nil (represented as NULL)
    if (!v) {
        return strdup("nil");
    }

    // Handle immediates (CljValue tagged pointers)
    if (is_immediate((CljValue)v)) {
        if (is_fixnum((CljValue)v)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", as_fixnum((CljValue)v));
            return strdup(buf);
        }
        if (is_fixed((CljValue)v)) {
            char buf[32];
            double val = as_fixed((CljValue)v);
            snprintf(buf, sizeof(buf), "%.4g", val);
            return strdup(buf);
        }
        if (is_special((CljValue)v)) {
            uint8_t special = as_special((CljValue)v);
            switch (special) {
                case SPECIAL_TRUE: return strdup("true");
                case SPECIAL_FALSE: return strdup("false");
                default: return strdup("unknown");
            }
        }
        if (is_char((CljValue)v)) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%c", (char)as_char((CljValue)v));
            return strdup(buf);
        }
    }

    // char buf[64]; // Unused variable removed
    switch(v->type) {
        // CLJ_INT, CLJ_FLOAT, CLJ_BOOL removed - handled as immediates

        case CLJ_STRING:
            {
                // Return raw string WITHOUT quotes (for str function)
                // String data is stored directly after CljObject header
                char **str_ptr = (char**)((char*)v + sizeof(CljObject));
                char *str = *str_ptr;
                return strdup(str);
            }

        case CLJ_SYMBOL:
            {
                CljSymbol *sym = as_symbol(v);
                if (!sym) return strdup("nil");
                
                // Handle namespace-qualified symbols
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
        case CLJ_TRANSIENT_VECTOR:
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
                
                // Mark transient vectors for debugging
                if (v->type == CLJ_TRANSIENT_VECTOR) {
                    char *result = ALLOC(char, strlen(s) + 20);
                    sprintf(result, "<transient %s>", s);
                    free(s);
                    return result;
                }
                
                return s;
            }

        case CLJ_LIST:
            {
                CljList *list = as_list(v);
                
                // Sammle alle Elemente in einem Array
                CljObject *elements[1000]; // Max 1000 Elemente
                int count = 0;
                
                // Head hinzufügen
                if (list->first) {
                    elements[count++] = list->first;
                }
                
                // Tail-Elemente hinzufügen
                CljObject *current = LIST_REST(list);
                while (current && is_type(current, CLJ_LIST) && count < 1000) {
                    CljList *current_list = as_list(current);
                    if (current_list && current_list->first) {
                        elements[count++] = current_list->first;
                    }
                    current = current_list ? current_list->rest : NULL;
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
        case CLJ_TRANSIENT_MAP:
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
                    cap += strlen(ks) + strlen(vs) + 3; // +1 space, +1 comma, +1 space = +3
                    free(ks); free(vs);
                }
                char *s = ALLOC(char, cap+1);
                strcpy(s, "{");
                bool first = true;
                for (int i = 0; i < map->count; i++) {
                    CljObject *k = KV_KEY(map->data, i);
                    CljObject *val = KV_VALUE(map->data, i);
                    if (!k) continue;
                    if (!first) strcat(s, ", ");
                    char *ks = pr_str(k);
                    char *vs = pr_str(val);
                    strcat(s, ks);
                    strcat(s, " ");
                    strcat(s, vs);
                    free(ks); free(vs);
                    first = false;
                }
                strcat(s, "}");
                
                // Mark transient maps for debugging
                if (v->type == CLJ_TRANSIENT_MAP) {
                    char *result = ALLOC(char, strlen(s) + 20);
                    sprintf(result, "<transient %s>", s);
                    free(s);
                    return result;
                }
                
                return s;
            }


        case CLJ_FUNC:
            {
                // Native C function (CljFunc)
                CljFunc *native_func = (CljFunc*)v;
                if (native_func->name) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "#<native function %s>", native_func->name);
                    return strdup(buf);
                }
                return strdup("#<native function>");
            }
        
        case CLJ_CLOSURE:
            {
                // Interpreted Clojure function (CljFunction)
                CljFunction *clj_func = (CljFunction*)v;
                if (clj_func && clj_func->name) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "#<function %s>", clj_func->name);
                    return strdup(buf);
                } else {
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
                // Use the existing iterator instead of creating a new seq
                SeqIterator temp_iter = seq->iter;
                while (!seq_iter_empty(&temp_iter)) {
                    CljObject *element = seq_iter_first(&temp_iter);
                    if (!element) {
                        seq_iter_next(&temp_iter);
                        continue;
                    }
                    
                    char *el_str = pr_str(element);
                    if (!el_str) {
                        seq_iter_next(&temp_iter);
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
                        return strdup("()");
                    }
                    result = new_result;
                    
                    if (!first) {
                        strcat(result, " ");
                    }
                    strcat(result, el_str);
                    first = false;
                    
                    free(el_str);
                    seq_iter_next(&temp_iter);
                }
                
                strcat(result, ")");
                return result;
            }

        case CLJ_EXCEPTION:
            {
                CLJException *exc = (CLJException*)v;
                char *result;
                if (exc->file[0] != '\0') {
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
    // Handle nil (represented as NULL)
    if (!v) {
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

// Konsolidierte Gleichheitsprüfung mit ID-Parametern
// Unterstützt sowohl CljObject* (heap objects) als auch CljValue (immediate values)
bool clj_equal_id(ID a, ID b) {
    // Schneller == Check funktioniert für beide Typen
    if (a == b) return true;
    if (!a || !b) return false;
    
    // Beide sind immediate values (CljValue)
    if (is_immediate((CljValue)a) && is_immediate((CljValue)b)) {
        if (is_fixnum((CljValue)a) && is_fixnum((CljValue)b)) {
            return as_fixnum((CljValue)a) == as_fixnum((CljValue)b);
        }
        if (is_char((CljValue)a) && is_char((CljValue)b)) {
            return as_char((CljValue)a) == as_char((CljValue)b);
        }
        if (is_fixed((CljValue)a) && is_fixed((CljValue)b)) {
            return as_fixed((CljValue)a) == as_fixed((CljValue)b);
        }
        if (is_special((CljValue)a) && is_special((CljValue)b)) {
            return as_special((CljValue)a) == as_special((CljValue)b);
        }
        return false; // Verschiedene immediate value Typen
    }
    
    // Beide sind CljObject* (heap objects)
    if (!is_immediate((CljValue)a) && !is_immediate((CljValue)b)) {
        return clj_equal((CljObject*)a, (CljObject*)b);
    }
    
    // Gemischte Typen (ein immediate, ein heap object)
    return false;
}

// Legacy-Wrapper für CljValue-Parameter
static bool clj_equal_value(CljValue a, CljValue b) {
    return clj_equal_id((ID)a, (ID)b);
}

// Korrekte Gleichheitsprüfung mit Inhalt-Vergleich
bool clj_equal(CljObject *a, CljObject *b) {
    if (a == b) return true;  // Pointer-Gleichheit (für Singletons und Symbole)
    if (!a || !b) return false;
    
    // Check if pointers are valid CljObject* (not immediate values)
    // Immediate values (CljValue) have different memory layout
    // We can't easily distinguish between CljObject* and CljValue* at runtime
    // So we rely on the type field being valid for CljObject*
    // Note: This validation is removed as it was causing segmentation faults
    // The function should be called with valid CljObject* pointers
    
    if (!is_type(a, b->type)) return false;
    
    // Inhalt-Vergleich basierend auf Typ
    // Hinweis: CLJ_BOOL, CLJ_SYMBOL werden bereits durch Pointer-Vergleich abgefangen
    switch (a->type) {
        // CLJ_INT, CLJ_FLOAT removed - handled as immediates
            
        // Komplexe Typen - Inhalt-Vergleich
        case CLJ_STRING: {
            char **str_a_ptr = (char**)((char*)a + sizeof(CljObject));
            char **str_b_ptr = (char**)((char*)b + sizeof(CljObject));
            char *str_a = *str_a_ptr;
            char *str_b = *str_b_ptr;
            if (!str_a || !str_b) return false;
            return strcmp(str_a, str_b) == 0;
        }
        
        case CLJ_VECTOR: {
            CljPersistentVector *vec_a = (CljPersistentVector*)a;
            CljPersistentVector *vec_b = (CljPersistentVector*)b;
            if (!vec_a || !vec_b) return false;
            if (vec_a->count != vec_b->count) return false;
            for (int i = 0; i < vec_a->count; i++) {
                // Vektorelemente können CljValue (immediate values) oder CljObject* sein
                if (!clj_equal_value(vec_a->data[i], vec_b->data[i])) return false;
            }
            return true;
        }
        
        case CLJ_MAP: {
            CljMap *map_a = as_map(a);
            CljMap *map_b = as_map(b);
            if (!map_a || !map_b) return false;
            if (map_a->count != map_b->count) return false;
            for (int i = 0; i < map_a->count; i++) {
                CljValue key_a = KV_KEY(map_a->data, i);
                CljValue val_a = KV_VALUE(map_a->data, i);
                CljValue val_b = map_get((CljValue)b, key_a);
                // Map-Werte können CljValue (immediate values) oder CljObject* sein
                if (!clj_equal_value(val_a, val_b)) return false;
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
        case CLJ_CLOSURE:
            // Functions are only equal if they're the same instance
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
    SymbolEntry *entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
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
        meta_registry = (CljObject*)make_map(32); // Initial capacity for metadata entries
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
    map_assoc((CljValue)meta_registry, (CljValue)v, (CljValue)meta);
}

CljObject* meta_get(CljObject *v) {
    if (!v || !meta_registry) return NULL;
    
    return (CljObject*)map_get((CljValue)meta_registry, (CljValue)v);
}

void meta_clear(CljObject *v) {
    if (!v || !meta_registry) return;
    
    // Find the entry and remove it using KV macros
    CljMap *map = (CljMap*)meta_registry;
    int index = KV_FIND_INDEX(map->data, map->count, v);
    if (index >= 0) {
        // Entry found; remove it
        CljObject *old_value = KV_VALUE(map->data, index);
        RELEASE(old_value);
        
        // Shift following elements to the left
        for (int j = index; j < map->count - 1; j++) {
            KV_SET_PAIR(map->data, j,
                       KV_KEY(map->data, j + 1),
                       KV_VALUE(map->data, j + 1));
        }
        
        map->count--;
    }
}


#endif

// Static singletons - these live forever and are never freed
// Note: nil is now represented as NULL, true/false as immediate values
// empty_map, empty_vector, and empty_string singletons are now statically initialized
// in their respective source files (map.c, vector.c, string.c)

// Singleton access functions
// Function wrappers moved to object.h as macros

// clj_false() removed - use make_special(SPECIAL_FALSE) instead

// clj_empty_vector moved to vector.c

// clj_empty_map() no longer part of public API; make_map_old(0) returns singleton

#define id CljObject*

// Stack-based environment helpers (outside of #ifdef)
CljObject* env_extend_stack(CljObject *parent_env, CljObject **params, CljObject **values, int count) {
    if (count > MAX_FUNCTION_PARAMS) return NULL;
    (void)parent_env; (void)params; (void)values;
    
    // Simplified implementation: just return an empty map
    // Parameter binding skipped for this stage
    CljObject *new_env = make_map(4);
    
    return (id)new_env;
}

CljObject* env_get_stack(CljObject *env, CljObject *key) {
    if (!env || !key) return NULL;
    
    // Direct map lookup
    return (CljObject*)map_get((CljValue)env, (CljValue)key);
}

void env_set_stack(CljObject *env, CljObject *key, CljObject *value) {
    if (!env || !key) return;
    
    map_assoc((CljValue)env, (CljValue)key, (CljValue)value);
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
    CljObject **heap_params = (CljObject**)malloc(sizeof(CljObject*) * argc);
    for (int i = 0; i < argc; i++) {
        heap_params[i] = RETAIN(argv[i]);
    }
    
    // Extend environment with parameters
    CljObject *call_env = env_extend_stack(func->closure_env, func->params, heap_params, argc);
    if (!call_env) {
        DEALLOC(heap_params);
        return make_error("Failed to create function environment", NULL, 0, 0);
    }
    
    // Evaluate function body (simplified; would normally call eval())
    CljObject *result = func->body ? RETAIN(func->body) : NULL;
    
    // Release environment and parameter array
    RELEASE(call_env);
    DEALLOC(heap_params);
    
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
    CljObject *obj = ALLOC_SIMPLE(type);
    if (!obj) return NULL;
    
    obj->type = type;
    obj->rc = 1;
    
    // Initialisiere je nach Typ
    // Note: nil is now represented as NULL, no special handling needed
    
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
            {
                char **str_ptr = (char**)((char*)obj + sizeof(CljObject));
                if (*str_ptr) free(*str_ptr);
            }
            DEALLOC(obj);
            break;
        case CLJ_SYMBOL:
            {
                CljSymbol *sym = (CljSymbol*)obj;
                // Note: CljNamespace doesn't have reference counting yet
                // TODO: Implement reference counting for CljNamespace
                // CljSymbol is a CljObject subtype, use DEALLOC for final cleanup
                DEALLOC(sym);
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
                DEALLOC(vec);
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
                DEALLOC(map);
            }
            break;
        case CLJ_LIST:
            {
                CljList *list = (CljList*)obj;
                if (list->first) release_object(list->first);
                if (list->rest) release_object(list->rest);
                DEALLOC(list);
            }
            break;
        case CLJ_FUNC:
            {
                // Native C function (CljFunc)
                CljFunc *native_func = (CljFunc*)obj;
                if (native_func && native_func->name) {
                    free((void*)native_func->name);
                }
                DEALLOC(obj);
            }
            break;
        
        case CLJ_CLOSURE:
            {
                // Interpreted Clojure function (CljFunction)
                CljFunction *clj_func = (CljFunction*)obj;
                if (clj_func->params) {
                    for (int i = 0; i < clj_func->param_count; i++) {
                        if (clj_func->params[i]) release_object(clj_func->params[i]);
                    }
                    free(clj_func->params);
                }
                if (clj_func->body) release_object(clj_func->body);
                if (clj_func->closure_env) release_object(clj_func->closure_env);
                if (clj_func->name) free((void*)clj_func->name);
                DEALLOC(obj);
            }
            break;
        case CLJ_EXCEPTION:
            {
                CLJException *exc = (CLJException*)obj;
                // Strings are now embedded, no need to free them
                // data field removed
                DEALLOC(exc);
            }
            break;
        default:
            // Primitive Typen brauchen keine Freigabe
            DEALLOC(obj);
            break;
    }
}

// Type-safe Casting now provided as static inline in header
