/**
 * @file to_string.c
 * @brief String conversion functions for Clojure values
 * 
 * This file provides functions for converting Clojure values to their
 * string representations, including pr_str (readable) and print_str
 * (human-readable) variants.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "to_string.h"
#include "subjective-c/object.h"
#include "subjective-c/strings.h"
#include "namespace.h"
#include "subjective-c/value.h"
#include "symbol.h"
#include "subjective-c/vector.h"
#include "list.h"
#include "subjective-c/map.h"
#include "function.h"
#include "seq.h"
#include "subjective-c/exception.h"
#include "atom.h"
#include "subjective-c/byte_array.h"
#include "subjective-c/memory.h"
#include "subjective-c/kv_macros.h"
#include "subjective-c/types.h"
#include "runtime.h"

// Global flag for special form rendering mode
static bool g_print_special_forms_as_tags = true;

bool strings_set_special_form_rendering(bool as_tags) {
    bool previous = g_print_special_forms_as_tags;
    g_print_special_forms_as_tags = as_tags;
    return previous;
}

bool strings_get_special_form_rendering(void) {
    return g_print_special_forms_as_tags;
}

// Forward declarations for recursive helpers
static size_t to_string_calc_length(CljObject *v, bool escape_strings);
static void to_string_build_string(CljObject *v, char *buffer, size_t *offset, bool escape_strings);

// is_special_symbol() moved to symbol.h (inline function)

void strings_clear_special_forms(void) {
    // No-op: flags are set at symbol initialization
}

void strings_register_special_form(const char *name) {
    (void)name; // No-op: flags are set at symbol initialization
}

// Recursive helper: Calculate string length without allocating
static size_t to_string_calc_length(CljObject *v, bool escape_strings) {
    if (!v) {
        return 3; // "nil"
    }

    if (is_immediate(v)) {
        if (is_fixnum(v)) {
            char buf[32];
            return (size_t)snprintf(buf, sizeof(buf), "%d", as_fixnum(v));
        }
        if (is_fixed(v)) {
            char buf[32];
            return (size_t)snprintf(buf, sizeof(buf), "%.2f", (double)as_fixed(v));
        }
        if (is_special(v)) {
            uint8_t special = as_special(v);
            switch (special) {
                case SPECIAL_TRUE: return 4; // "true"
                case SPECIAL_FALSE: return 5; // "false"
                default: return 7; // "unknown"
            }
        }
        if (is_character(v)) {
            return 1; // single character
        }
    }

    switch(v->type) {
        case CLJ_STRING: {
            CljString *s = (CljString*)v;
            if (escape_strings) {
                return escape_string_calc_length(s);
            }
            return s->length;
        }

        case CLJ_SYMBOL: {
            CljSymbol *sym = as_symbol(v);
            if (!sym) return 3; // "nil"
            if (!sym->cname) return 3; // "nil" if no name
            
            // Special forms are printed as #<special-form name> (like in Clojure)
            if (g_print_special_forms_as_tags && is_special_symbol(sym)) {
                return 17 + strlen(sym->cname); // "#<special-form name>" = 16 + name + 1
            }
            
            // Only show namespace if explicitly set (not NULL = implicit clojure.core)
            // This matches Clojure's behavior: core symbols print without namespace
            // CRITICAL: Use exact same logic as to_string_build_string to ensure consistency
            if (sym->ns_name && TAG(sym->ns_name) == CLJ_SYMBOL) {
                CljSymbol *ns_sym = as_symbol(sym->ns_name);
                if (ns_sym && ns_sym->cname) {
                    return strlen(ns_sym->cname) + 1 + strlen(sym->cname); // "ns/name"
                }
            }
            return strlen(sym->cname);
        }

        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_VECTOR_TRANSIENT_WEAK: {
            void *vec_ptr = as_vector(v);
            if (!vec_ptr) return 2; // "[]"
            CljVector *vec = (CljVector*)vec_ptr;
            int count = vector_count(vec);
            size_t len = 2; // "[ ]"
            for (int i = 0; i < count; i++) {
                ID elem = vector_nth(vec, i);
                len += to_string_calc_length((CljObject*)elem, escape_strings);
                if (i < count - 1) len += 1; // space
            }
            if (v->type == CLJ_VECTOR_TRANSIENT) {
                len += 11; // "<transient >"
            }
            return len;
        }

        case CLJ_LIST:
        case CLJ_AST_NODE: {
            size_t len = 2; // "( )"
            int count = 0;
            LIST_FOR_EACH(v, elem) {
                if (count >= 1000) break;
                // nil elements are valid - to_string_calc_length handles NULL
                if (count > 0) len += 1; // space
                len += to_string_calc_length(elem, escape_strings);
                count++;
            }
            return len;
        }

        case CLJ_MAP:
        case CLJ_MAP_TRANSIENT: {
            CljMap *map = as_map(v);
            if (!map) return 2; // "{}"
            size_t len = 2; // "{ }"
            bool first = true;
            MAP_FOR_EACH(map, k, val) {
                if (!k) continue;
                if (!first) len += 2; // ", "
                len += to_string_calc_length((CljObject*)k, escape_strings);
                len += 1; // space
                len += to_string_calc_length((CljObject*)val, escape_strings);
                first = false;
            }
            if (v->type == CLJ_MAP_TRANSIENT) {
                len += 11; // "<transient >"
            }
            return len;
        }

        case CLJ_FUNC: {
            CljCFunc *native_func = (CljCFunc*)v;
            if (native_func->name) {
                // Find namespace containing this function to get fully qualified name
                CljNamespace *ns = ns_find_for_object((CljObject*)v);
                const char *ns_name = (ns && ns->name && ns->name->cname) ? ns->name->cname : NULL;
                
                // Calculate length for fully qualified name if namespace is available
                if (ns_name) {
                    return 20 + strlen(ns_name) + strlen(native_func->name); // "#<native function NS/NAME>"
                } else {
                    return 19 + strlen(native_func->name); // "#<native function NAME>"
                }
            }
            return 20; // "#<native function>"
        }

        case CLJ_CLOSURE: {
            CljFunction *clj_func = (CljFunction*)v;
            if (clj_func && clj_func->name) {
                CljNamespace *ns = ns_find_for_object((CljObject*)v);
                const char *ns_name = ns && ns->name && ns->name->cname ? ns->name->cname : NULL;
                size_t len = 20; // "#<Clojure function "
                if (ns_name) {
                    len += strlen(ns_name) + 1; // "ns/"
                }
                len += strlen(clj_func->name) + 1; // "name>"
                return len;
            }
            return 20; // "#<Clojure function>"
        }

        case CLJ_SEQ: {
            CljSeqIterator *seq = as_seq(v);
            if (!seq) return 2; // "()"
            size_t len = 2; // "( )"
            SeqIterator temp_iter = seq->iter;
            bool first = true;
            while (!seq_iter_empty(&temp_iter)) {
                CljObject *element = (CljObject*)seq_iter_first(&temp_iter);
                // nil elements are valid - to_string_calc_length handles NULL
                if (!first) len += 1; // space
                len += to_string_calc_length(element, escape_strings);
                first = false;
                seq_iter_next(&temp_iter);
            }
            return len;
        }

        case CLJ_EXCEPTION: {
            CLJException *exc = (CLJException*)v;
            if (exc->file[0] != '\0') {
                return strlen(exc->type) + 2 + strlen(exc->message) + 5 + strlen(exc->file) + 20; // approximate
            }
            return strlen(exc->type) + 2 + strlen(exc->message) + 30; // approximate
        }

        case CLJ_ATOM: {
            CljAtom *atom = as_atom(v);
            size_t len = 12; // "#<Atom@: >"
            len += 20; // address
            if (atom->value) {
                len += to_string_calc_length((CljObject*)atom->value, escape_strings);
            } else {
                len += 3; // "nil"
            }
            return len;
        }

        case CLJ_BYTE_ARRAY: {
            CljByteArray *ba = as_byte_array(v);
            if (!ba) return 13; // "#<byte-array>"
            int preview_len = ba->length < 8 ? ba->length : 8;
            size_t len = 15; // "#<byte-array ["
            len += preview_len * 5; // "0x00 "
            if (ba->length > 8) len += 5; // " ..."
            len += 2; // "]>"
            return len;
        }

        default:
            return 9; // "#<unknown>"
    }
}

// Recursive helper: Build string into buffer
static void to_string_build_string(CljObject *v, char *buffer, size_t *offset, bool escape_strings) {
    if (!v) {
        memcpy(buffer + *offset, "nil", 3);
        *offset += 3;
        return;
    }

    if (is_immediate(v)) {
        if (is_fixnum(v)) {
            int written = snprintf(buffer + *offset, 32, "%d", as_fixnum(v));
            *offset += written;
            return;
        }
        if (is_fixed(v)) {
            int written = snprintf(buffer + *offset, 32, "%.2f", (double)as_fixed(v));
            *offset += written;
            return;
        }
        if (is_special(v)) {
            uint8_t special = as_special(v);
            switch (special) {
                case SPECIAL_TRUE:
                    memcpy(buffer + *offset, "true", 4);
                    *offset += 4;
                    return;
                case SPECIAL_FALSE:
                    memcpy(buffer + *offset, "false", 5);
                    *offset += 5;
                    return;
                default:
                    memcpy(buffer + *offset, "unknown", 7);
                    *offset += 7;
                    return;
            }
        }
        if (is_character(v)) {
            buffer[*offset] = (char)as_character(v);
            *offset += 1;
            return;
        }
    }

    switch(v->type) {
        case CLJ_STRING: {
            CljString *s = (CljString*)v;
            if (escape_strings) {
                escape_string_write(s, buffer, offset);
            } else {
                memcpy(buffer + *offset, s->data, s->length);
                *offset += s->length;
            }
            return;
        }

        case CLJ_SYMBOL: {
            CljSymbol *sym = as_symbol(v);
            if (!sym) {
                memcpy(buffer + *offset, "nil", 3);
                *offset += 3;
                return;
            }
            if (!sym->cname) {
                memcpy(buffer + *offset, "nil", 3);
                *offset += 3;
                return;
            }
            // Special forms are printed as #<special-form name> (like in Clojure)
            if (g_print_special_forms_as_tags && is_special_symbol(sym)) {
                memcpy(buffer + *offset, "#<special-form ", 15);
                *offset += 15;
                size_t name_len = strlen(sym->cname);
                memcpy(buffer + *offset, sym->cname, name_len);
                *offset += name_len;
                buffer[*offset] = '>';
                *offset += 1;
                return;
            }
            // Only show namespace if explicitly set (not NULL = implicit clojure.core)
            // This matches Clojure's behavior: core symbols print without namespace
            // CRITICAL: Use exact same logic as to_string_calc_length to ensure consistency
            if (sym->ns_name && TAG(sym->ns_name) == CLJ_SYMBOL) {
                CljSymbol *ns_sym = as_symbol(sym->ns_name);
                if (ns_sym && ns_sym->cname) {
                    size_t ns_len = strlen(ns_sym->cname);
                    memcpy(buffer + *offset, ns_sym->cname, ns_len);
                    *offset += ns_len;
                    buffer[*offset] = '/';
                    *offset += 1;
                }
            }
            size_t name_len = strlen(sym->cname);
            memcpy(buffer + *offset, sym->cname, name_len);
            *offset += name_len;
            return;
        }

        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_VECTOR_TRANSIENT_WEAK: {
            void *vec_ptr = as_vector(v);
            if (!vec_ptr) {
                memcpy(buffer + *offset, "[]", 2);
                *offset += 2;
                return;
            }
            CljVector *vec = (CljVector*)vec_ptr;
            if (v->type == CLJ_VECTOR_TRANSIENT) {
                memcpy(buffer + *offset, "<transient ", 11);
                *offset += 11;
            }
            buffer[*offset] = '[';
            *offset += 1;
            int count = vector_count(vec);
            for (int i = 0; i < count; i++) {
                ID elem = vector_nth(vec, i);
                to_string_build_string((CljObject*)elem, buffer, offset, escape_strings);
                if (i < count - 1) {
                    buffer[*offset] = ' ';
                    *offset += 1;
                }
            }
            buffer[*offset] = ']';
            *offset += 1;
            if (v->type == CLJ_VECTOR_TRANSIENT) {
                buffer[*offset] = '>';
                *offset += 1;
            }
            return;
        }

        case CLJ_LIST:
        case CLJ_AST_NODE: {
            buffer[*offset] = '(';
            *offset += 1;
            int count = 0;
            LIST_FOR_EACH(v, elem) {
                if (count >= 1000) break;
                // nil elements are valid - to_string_build_string handles NULL
                if (count > 0) {
                    buffer[*offset] = ' ';
                    *offset += 1;
                }
                to_string_build_string(elem, buffer, offset, escape_strings);
                count++;
            }
            buffer[*offset] = ')';
            *offset += 1;
            return;
        }

        case CLJ_MAP:
        case CLJ_MAP_TRANSIENT: {
            CljMap *map = as_map(v);
            if (!map) {
                memcpy(buffer + *offset, "{}", 2);
                *offset += 2;
                return;
            }
            if (v->type == CLJ_MAP_TRANSIENT) {
                memcpy(buffer + *offset, "<transient ", 11);
                *offset += 11;
            }
            buffer[*offset] = '{';
            *offset += 1;
            bool first = true;
            MAP_FOR_EACH(map, k, val) {
                if (!k) continue;
                if (!first) {
                    memcpy(buffer + *offset, ", ", 2);
                    *offset += 2;
                }
                to_string_build_string((CljObject*)k, buffer, offset, escape_strings);
                buffer[*offset] = ' ';
                *offset += 1;
                to_string_build_string((CljObject*)val, buffer, offset, escape_strings);
                first = false;
            }
            buffer[*offset] = '}';
            *offset += 1;
            if (v->type == CLJ_MAP_TRANSIENT) {
                buffer[*offset] = '>';
                *offset += 1;
            }
            return;
        }

        case CLJ_FUNC: {
            CljCFunc *native_func = (CljCFunc*)v;
            if (native_func->name) {
                // Find namespace containing this function to get fully qualified name
                CljNamespace *ns = ns_find_for_object((CljObject*)v);
                const char *ns_name = (ns && ns->name && ns->name->cname) ? ns->name->cname : NULL;
                
                // Build fully qualified name if namespace is available
                if (ns_name) {
                    int written = snprintf(buffer + *offset, 256, "#<native function %s/%s>", ns_name, native_func->name);
                    *offset += written;
                } else {
                    int written = snprintf(buffer + *offset, 256, "#<native function %s>", native_func->name);
                    *offset += written;
                }
            } else {
                memcpy(buffer + *offset, "#<native function>", 19);
                *offset += 19;
            }
            return;
        }

        case CLJ_CLOSURE: {
            CljFunction *clj_func = (CljFunction*)v;
            if (clj_func && clj_func->name) {
                CljNamespace *ns = ns_find_for_object((CljObject*)v);
                const char *ns_name = ns && ns->name && ns->name->cname ? ns->name->cname : NULL;
                int written = snprintf(buffer + *offset, 256, "#<Clojure function %s%s%s>",
                             ns_name ? ns_name : "",
                             ns_name ? "/" : "",
                             clj_func->name);
                *offset += written;
            } else {
                memcpy(buffer + *offset, "#<Clojure function>", 19);
                *offset += 19;
            }
            return;
        }

        case CLJ_SEQ: {
            CljSeqIterator *seq = as_seq(v);
            if (!seq) {
                memcpy(buffer + *offset, "()", 2);
                *offset += 2;
                return;
            }
            buffer[*offset] = '(';
            *offset += 1;
            SeqIterator temp_iter = seq->iter;
            bool first = true;
            while (!seq_iter_empty(&temp_iter)) {
                CljObject *element = (CljObject*)seq_iter_first(&temp_iter);
                // nil elements are valid - to_string_build_string handles NULL
                if (!first) {
                    buffer[*offset] = ' ';
                    *offset += 1;
                }
                to_string_build_string(element, buffer, offset, escape_strings);
                first = false;
                seq_iter_next(&temp_iter);
            }
            buffer[*offset] = ')';
            *offset += 1;
            return;
        }

        case CLJ_EXCEPTION: {
            CLJException *exc = (CLJException*)v;
            int written = exc->file[0] != '\0'
                ? snprintf(buffer + *offset, 1024, "%s: %s at %s:%d:%d",
                          exc->type, exc->message, exc->file, exc->line, exc->col)
                : snprintf(buffer + *offset, 512, "%s: %s at line %d, col %d",
                          exc->type, exc->message, exc->line, exc->col);
            *offset += written;
            return;
        }

        case CLJ_ATOM: {
            CljAtom *atom = as_atom(v);
            int written = snprintf(buffer + *offset, 256, "#<Atom@%p: ", (void*)atom);
            *offset += written;
            if (atom->value) {
                to_string_build_string((CljObject*)atom->value, buffer, offset, escape_strings);
            } else {
                memcpy(buffer + *offset, "nil", 3);
                *offset += 3;
            }
            buffer[*offset] = '>';
            *offset += 1;
            return;
        }

        case CLJ_BYTE_ARRAY: {
            CljByteArray *ba = as_byte_array(v);
            if (!ba) {
                memcpy(buffer + *offset, "#<byte-array>", 13);
                *offset += 13;
                return;
            }
            int written = snprintf(buffer + *offset, 256, "#<byte-array [");
            *offset += written;
            int preview_len = ba->length < 8 ? ba->length : 8;
            for (int i = 0; i < preview_len; i++) {
                if (i > 0) {
                    buffer[*offset] = ' ';
                    *offset += 1;
                }
                written = snprintf(buffer + *offset, 10, "0x%02x", ba->data[i]);
                *offset += written;
            }
            if (ba->length > 8) {
                memcpy(buffer + *offset, " ...", 4);
                *offset += 4;
            }
            memcpy(buffer + *offset, "]>", 2);
            *offset += 2;
            return;
        }

        case CLJ_NAMESPACE: {
            CljNamespace *ns = (CljNamespace*)v;
            if (!ns || !ns->name || !ns->name->cname) {
                memcpy(buffer + *offset, "#<namespace>", 12);
                *offset += 12;
                return;
            }
            // Write namespace name to buffer
            const char *ns_name = ns->name->cname;
            size_t ns_name_len = strlen(ns_name);
            memcpy(buffer + *offset, ns_name, ns_name_len);
            *offset += ns_name_len;
            return;
        }

        default:
            memcpy(buffer + *offset, "#<unknown>", 10);
            *offset += 10;
            return;
    }
}

CljString* to_string(ID v) {
    return to_string_with_escape(v, false);
}

CljString* to_string_with_escape(ID v, bool escape_strings) {
    size_t len = to_string_calc_length((CljObject*)v, escape_strings);
    CljString *result = (CljString*)AUTORELEASE(make_string_buffer(len));

    size_t offset = 0;
    to_string_build_string((CljObject*)v, result->data, &offset, escape_strings);
    result->data[offset] = '\0';
    result->length = (uint16_t)offset;

    return result;
}

CljString* pr_str(ID v) {
    return to_string_with_escape(v, true);
}

CljString* print_str(ID v) {
    return to_string_with_escape(v, false);
}

