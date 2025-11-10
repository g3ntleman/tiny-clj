#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "object.h"
#include "strings.h"
#include "clj_strings.h"
#include "value.h"
#include "symbol.h"
#include "vector.h"
#include "list.h"
#include "map.h"
#include "function.h"
#include "seq.h"
#include "exception.h"
#include "atom.h"
#include "byte_array.h"
#include "memory.h"
#include "kv_macros.h"

// Empty string singleton with CljString layout
static struct {
    CljObject base;
    uint16_t length;
    char data[1];  // Just the null terminator
} empty_string_data = {
    .base = { .type = CLJ_STRING, .rc = 0 },
    .length = 0,
    .data = ""
};

CljString* empty_string_singleton = (CljString*)&empty_string_data;

/**
 * @brief Create a string value
 * @param str String to create
 * @return CljString object (caller must release)
 */
struct CljString* make_string(const char *str) {
    if (!str || str[0] == '\0') {
        return empty_string_singleton;
    }
    
    // Allocate CljString + space for string data + null terminator
    size_t len = strlen(str);
    
    // Assert that string length fits in 16-bit field (max 65,535 characters)
    assert(len <= UINT16_MAX && "String length exceeds 16-bit limit (65,535 chars)");
    
    CljString *s = (CljString*)alloc(sizeof(CljString) + len + 1, 1, CLJ_STRING);
    if (!s) throw_oom(CLJ_STRING);
    
    s->base.type = CLJ_STRING;
    s->base.rc = 1;
    s->length = (uint16_t)len;
    memcpy(s->data, str, len + 1);  // includes null terminator
    
    return s;
}

const char* to_string(CljObject *v) {
    // Handle nil (represented as NULL)
    if (!v) {
        return strdup("nil");
    }

    // Handle immediates (CljValue tagged pointers)
    if (is_immediate(v)) {
        if (is_fixnum(v)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", as_fixnum(v));
            return strdup(buf);
        }
        if (is_fixed(v)) {
            char buf[32];
            double val = as_fixed(v);
            snprintf(buf, sizeof(buf), "%.4g", val);
            return strdup(buf);
        }
        if (is_special(v)) {
            uint8_t special = as_special(v);
            switch (special) {
                case SPECIAL_TRUE: return strdup("true");
                case SPECIAL_FALSE: return strdup("false");
                default: return strdup("unknown");
            }
        }
        if (is_character(v)) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%c", (char)as_character(v));
            return strdup(buf);
        }
    }

    // char buf[64]; // Unused variable removed
    switch(v->type) {
        // CLJ_INT, CLJ_FLOAT, CLJ_BOOL removed - handled as immediates

        case CLJ_STRING:
            {
                // Special handling for empty string singleton
                if (v == (CljObject*)empty_string_singleton) {
                    return strdup("");
                }
                
                // Access string data directly from CljString structure
                CljString *s = (CljString*)v;
                return strdup(s->data);
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
                    const char *el = pr_str(vec->data[i]);
                    cap += strlen(el) + 1;
                    free((void*)el);
                }
                char *s = ALLOC(char, cap+1);
                strcpy(s, "[");
                for (int i = 0; i < vec->count; i++) {
                    const char *el = pr_str(vec->data[i]);
                    strcat(s, el);
                    if (i < vec->count-1) strcat(s, " ");
                    free((void*)el);
                }
                strcat(s, "]");
                
                // Mark transient vectors for debugging
                if (v->type == CLJ_TRANSIENT_VECTOR) {
                    char *result = ALLOC(char, strlen(s) + 20);
                    snprintf(result, strlen(s) + 20, "<transient %s>", s);
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
                while (current && TAG(current) == CLJ_LIST && count < 1000) {
                    CljList *current_list = as_list(current);
                    if (current_list && current_list->first) {
                        elements[count++] = current_list->first;
                    }
                    current = current_list ? current_list->rest : NULL;
                }
                
                // Berechne benötigte Kapazität
                size_t cap = 2; // ( )
                for (int i = 0; i < count; i++) {
                    const char *el = pr_str(elements[i]);
                    cap += strlen(el) + 1;
                    free((void*)el);
                }
                
                // Erstelle String
                char *s = ALLOC(char, cap+1);
                strcpy(s, "(");
                for (int i = 0; i < count; i++) {
                    const char *el = pr_str(elements[i]);
                    strcat(s, el);
                    if (i < count-1) strcat(s, " ");
                    free((void*)el);
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
                    const char *ks = pr_str(k);
                    const char *vs = pr_str(val);
                    cap += strlen(ks) + strlen(vs) + 3; // +1 space, +1 comma, +1 space = +3
                    free((void*)ks); free((void*)vs);
                }
                char *s = ALLOC(char, cap+1);
                strcpy(s, "{");
                bool first = true;
                for (int i = 0; i < map->count; i++) {
                    CljObject *k = KV_KEY(map->data, i);
                    CljObject *val = KV_VALUE(map->data, i);
                    if (!k) continue;
                    if (!first) strcat(s, ", ");
                    const char *ks = pr_str(k);
                    const char *vs = pr_str(val);
                    strcat(s, ks);
                    strcat(s, " ");
                    strcat(s, vs);
                    free((void*)ks); free((void*)vs);
                    first = false;
                }
                strcat(s, "}");
                
                // Mark transient maps for debugging
                if (v->type == CLJ_TRANSIENT_MAP) {
                    char *result = ALLOC(char, strlen(s) + 20);
                    snprintf(result, strlen(s) + 20, "<transient %s>", s);
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
                CljSeqIterator *seq = as_seq((ID)v);
                if (!seq) return strdup("()");
                
                // Direktes Drucken ohne Umkopieren
                char *result = strdup("(");
                if (!result) return strdup("()");
                
                bool first = true;
                // Use the existing iterator instead of creating a new seq
                SeqIterator temp_iter = seq->iter;
                while (!seq_iter_empty(&temp_iter)) {
                    CljObject *element = (CljObject*)seq_iter_first(&temp_iter);
                    if (!element) {
                        seq_iter_next(&temp_iter);
                        continue;
                    }
                    
                    const char *el_str = pr_str(element);
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
                        free((void*)el_str);
                        return strdup("()");
                    }
                    result = new_result;
                    
                    if (!first) {
                        strcat(result, " ");
                    }
                    strcat(result, el_str);
                    first = false;
                    
                    free((void*)el_str);
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

        case CLJ_ATOM:
            {
                CljAtom *atom = as_atom(v);  // as_atom() already checks for NULL and aborts
                
                // Format: #<Atom@<address>: <value>>
                const char *value_str = atom->value ? pr_str(atom->value) : strdup("nil");
                char buf[256];
                snprintf(buf, sizeof(buf), "#<Atom@%p: %s>", (void*)atom, value_str);
                free((void*)value_str);
                return strdup(buf);  // strdup() required: to_string() must return allocated string
            }

        case CLJ_BYTE_ARRAY:
            {
                CljByteArray *ba = as_byte_array(v);
                if (!ba) return strdup("#<byte-array>");
                
                // Show first few bytes in hex format
                char buf[256];
                int preview_len = ba->length < 8 ? ba->length : 8;
                int offset = snprintf(buf, sizeof(buf), "#<byte-array [");
                
                for (int i = 0; i < preview_len; i++) {
                    offset += snprintf(buf + offset, sizeof(buf) - offset, 
                                      "0x%02x", ba->data[i]);
                    if (i < preview_len - 1) {
                        offset += snprintf(buf + offset, sizeof(buf) - offset, " ");
                    }
                }
                
                if (ba->length > 8) {
                    snprintf(buf + offset, sizeof(buf) - offset, " ...]>");
                } else {
                    snprintf(buf + offset, sizeof(buf) - offset, "]>");
                }
                
                return strdup(buf);
            }

        default:
            return strdup("#<unknown>");
    }
}

const char* pr_str(CljObject *v) {
    // Handle nil (represented as NULL)
    if (!v) {
        return strdup("nil");
    }
    
    // pr_str adds quotes around strings
    if (v && TAG(v) == CLJ_STRING) {
        const char *raw = to_string(v);
        if (!raw) return strdup("\"\"");
        
        size_t len = strlen(raw) + 3;  // +2 for quotes, +1 for \0
        char *result = ALLOC(char, len);
        snprintf(result, len, "\"%s\"", raw);
        free((void*)raw);
        return result;
    }
    
    // For all other types: delegate to to_string
    return to_string(v);
}

const char* print_str(CljObject *v) {
    // Handle nil (represented as NULL)
    if (!v) {
        return strdup("nil");
    }
    
    // print_str does NOT add quotes around strings (unlike pr_str)
    // For all types including strings: delegate to to_string
    return to_string(v);
}


