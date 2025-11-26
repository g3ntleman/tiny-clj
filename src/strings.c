#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include "object.h"
#include "strings.h"
#include "namespace.h"  // For CljNamespace definition
#include "value.h"
#include "symbol.h"
#include "vector.h"  // Must be before memory.h to avoid CljVector type conflict
#include "list.h"
#include "map.h"
#include "function.h"
#include "seq.h"
#include "exception.h"
#include "atom.h"
#include "byte_array.h"
#include "memory.h"
#include "kv_macros.h"
#include "types.h"  // For SINGLETON_RC
#include "runtime.h"  // For g_runtime

// Empty string singleton with CljString layout
static struct {
    CljObject base;
    uint16_t length;
    char data[1];  // Just the null terminator
} empty_string_data = {
    .base = { .type = CLJ_STRING, .rc = SINGLETON_RC },
    .length = 0,
    .data = ""
};

CljString* string_empty_singleton = (CljString*)&empty_string_data;

/**
 * @brief Create a string value
 * @param str String to create
 * @return CljString object (caller must release)
 */
struct CljString* make_string(const char *str) {
    if (!str || str[0] == '\0') {
        return string_empty_singleton;
    }

    // Allocate CljString + space for string data + null terminator
    size_t len = strlen(str);

    // Assert that string length fits in 16-bit field (max 65,535 characters)
    assert(len <= UINT16_MAX && "String length exceeds 16-bit limit (65,535 chars)");

    CljString *s = (CljString*)alloc(sizeof(CljString) + len + 1, 1, CLJ_STRING);
    if (!s) throw_oom();

    s->base.type = CLJ_STRING;
    s->base.rc = 1;
    s->length = (uint16_t)len;
    memcpy(s->data, str, len + 1);  // includes null terminator

    return s;
}

CljString* make_string_buffer(size_t length) {
    // Return empty string singleton if length is 0
    if (length == 0) {
        return string_empty_singleton;
    }

    // Check that length fits in 16-bit field (max 65,535 characters)
    if (length > UINT16_MAX) {
        throw_exception(EXCEPTION_RUNTIME, "make_string_buffer: length exceeds maximum (65,535)",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Allocate CljString + space for string data + null terminator
    CljString *s = (CljString*)alloc(sizeof(CljString) + length + 1, 1, CLJ_STRING);
    if (!s) {
        throw_oom();
        return NULL;
    }

    s->base.type = CLJ_STRING;
    s->base.rc = 1;
    s->length = (uint16_t)length;
    // Zero-initialize the buffer (including null terminator)
    memset(s->data, 0, length + 1);

    return s;
}

// Forward declarations for recursive helpers
static size_t to_string_calc_length(CljObject *v, bool escape_strings);
static void to_string_build_string(CljObject *v, char *buffer, size_t *offset, bool escape_strings);

// Helper: Calculate length of string with escaping
static size_t escape_string_calc_length(CljString *s) {
    size_t len = s->length;
    size_t escaped_len = len;
    const char *data = s->data;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '"' || data[i] == '\\') {
            escaped_len++;  // Each needs a backslash
        }
    }
    return escaped_len + 2;  // +2 for quotes
}

// Helper: Write string with escaping to buffer
static void escape_string_write(CljString *s, char *buffer, size_t *offset) {
    buffer[*offset] = '"';
    (*offset)++;

    const char *data = s->data;
    size_t len = s->length;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '"' || data[i] == '\\') {
            buffer[*offset] = '\\';
            (*offset)++;
        }
        buffer[*offset] = data[i];
        (*offset)++;
    }

    buffer[*offset] = '"';
    (*offset)++;
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
                if (sym->ns_name && sym->ns_name->cname) {
                return strlen(sym->ns_name->cname) + 1 + strlen(sym->cname); // "ns/name"
                }
            return strlen(sym->cname);
            }

        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_VECTOR_WEAK: {
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

        case CLJ_LIST: {
            size_t len = 2; // "( )"
            ID current = (ID)v;
            int count = 0;
            while (current && TAG(current) == CLJ_LIST && count < 1000) {
                CljList *current_list = as_list(current);
                if (current_list && current_list->first) {
                    len += to_string_calc_length(current_list->first, escape_strings);
                    if (count > 0) len += 1; // space
                    count++;
                }
                current = current_list ? current_list->rest : NULL;
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
                return 19 + strlen(native_func->name); // "#<native function NAME>"
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
                if (element) {
                    if (!first) len += 1; // space
                    len += to_string_calc_length(element, escape_strings);
                    first = false;
                }
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
            if (sym->ns_name && sym->ns_name->cname) {
                size_t ns_len = strlen(sym->ns_name->cname);
                memcpy(buffer + *offset, sym->ns_name->cname, ns_len);
                *offset += ns_len;
                buffer[*offset] = '/';
                *offset += 1;
            }
            size_t name_len = strlen(sym->cname);
            memcpy(buffer + *offset, sym->cname, name_len);
            *offset += name_len;
            return;
                }

        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_VECTOR_WEAK: {
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

        case CLJ_LIST: {
            buffer[*offset] = '(';
            *offset += 1;
            ID current = (ID)v;
            int count = 0;
                while (current && TAG(current) == CLJ_LIST && count < 1000) {
                    CljList *current_list = as_list(current);
                    if (current_list && current_list->first) {
                    if (count > 0) {
                        buffer[*offset] = ' ';
                        *offset += 1;
                    }
                    to_string_build_string(current_list->first, buffer, offset, escape_strings);
                    count++;
                }
                current = current_list ? current_list->rest : NULL;
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
                int written = snprintf(buffer + *offset, 256, "#<native function %s>", native_func->name);
                *offset += written;
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
                if (element) {
                    if (!first) {
                        buffer[*offset] = ' ';
                        *offset += 1;
                    }
                    to_string_build_string(element, buffer, offset, escape_strings);
                    first = false;
                }
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

        case CLJ_NAMESPACE:
            {
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

const char* to_cstring(CljObject *v) {
    return to_cstring_with_escape(v, false);
}

const char* to_cstring_with_escape(CljObject *v, bool escape_strings) {
    // Calculate length and build string using helper functions
    size_t len = to_string_calc_length(v, escape_strings);
    char *result = ALLOC(char, len + 1);
    if (!result) {
        return strdup("#<error>");
    }

    size_t offset = 0;
    to_string_build_string(v, result, &offset, escape_strings);
    result[offset] = '\0';

        return result;
    }

const char* pr_str(CljObject *v) {
    // pr_str adds quotes around strings and escapes quotes inside
    // For all types: use to_cstring_with_escape with escape_strings=true
    return to_cstring_with_escape(v, true);
}

const char* print_str(CljObject *v) {
    // print_str does NOT add quotes around strings (unlike pr_str)
    // For all types including strings: use to_cstring_with_escape with escape_strings=false
    return to_cstring_with_escape(v, false);
}


