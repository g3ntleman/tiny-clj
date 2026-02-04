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
#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "to_string.h"
#include "object.h"
#include "strings.h"
#include "namespace.h"
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
#include "types.h"
#include "runtime.h"
#include "regex.h"

#include "instant.h"
#include "uuid.h"

#include "datetime_utc.h"

#include "numeric_utils.h"


static void append_bytes(char *buffer, size_t *offset, const char *data, size_t len) {
    memcpy(buffer + *offset, data, len);
    *offset += len;
}

static void append_cstr(char *buffer, size_t *offset, const char *s) {
    append_bytes(buffer, offset, s, strlen(s));
}

static void append_char(char *buffer, size_t *offset, char c) {
    buffer[*offset] = c;
    *offset += 1;
}

static void append_int32(char *buffer, size_t *offset, int32_t value) {
    char tmp[32];
    size_t n = clj_itoa(value, tmp);
    append_bytes(buffer, offset, tmp, n);
}

static void append_fixed2(char *buffer, size_t *offset, float value) {
    char tmp[32];
    size_t n = clj_ftoa(value, tmp);
    append_bytes(buffer, offset, tmp, n);
}

static size_t write_hex_no_prefix_uintptr(uintptr_t value, char *out) {
    static const char hex[] = "0123456789abcdef";
    size_t i = 0;
    bool started = false;

    for (int shift = (int)(sizeof(uintptr_t) * 8 - 4); shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0x0F);
        if (!started) {
            if (nibble == 0 && shift != 0) {
                continue;
            }
            started = true;
        }
        out[i++] = hex[nibble];
    }

    if (!started) {
        out[i++] = '0';
    }

    return i;
}

static size_t hex_len_no_prefix_uintptr(uintptr_t value) {
    char tmp[2 + sizeof(uintptr_t) * 2];
    return write_hex_no_prefix_uintptr(value, tmp);
}

static void append_ptr_hex(char *buffer, size_t *offset, const void *ptr) {
    append_bytes(buffer, offset, "0x", 2);
    char tmp[2 + sizeof(uintptr_t) * 2];
    size_t n = write_hex_no_prefix_uintptr((uintptr_t)ptr, tmp);
    append_bytes(buffer, offset, tmp, n);
}

static void append_byte_hex2(char *buffer, size_t *offset, uint8_t value) {
    static const char hex[] = "0123456789abcdef";
    buffer[*offset + 0] = '0';
    buffer[*offset + 1] = 'x';
    buffer[*offset + 2] = hex[(value >> 4) & 0x0F];
    buffer[*offset + 3] = hex[value & 0x0F];
    *offset += 4;
}


static size_t format_instant_iso_utc(char *buf, size_t buf_size, const CljInstant *inst) {
    return tinyclj_format_inst_literal_iso8601_utc(buf, buf_size, inst->days, inst->ms);
}

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

    CljValue val = (CljValue)v;
    if (is_immediate(val)) {
        if (is_fixnum(val)) {
            char buf[32];
            return clj_itoa((int32_t)as_fixnum(val), buf);
        }
        if (is_special(val)) {
            uint8_t special = as_special(val);
            switch (special) {
                case SPECIAL_TRUE: return 4; // "true"
                case SPECIAL_FALSE: return 5; // "false"
                default: return 7; // "unknown"
            }
        }
        if (is_fixed(val)) {
            char buf[32];
            return clj_ftoa(as_fixed(val), buf);
        }
        if (is_character(val)) {
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

        case CLJ_VECTOR_PERSISTENT:
        case CLJ_VECTOR_TRANSIENT:{
            CljPersistentVector *vec =
                (v->type == CLJ_VECTOR_TRANSIENT)
                    ? vector_persistent(as_transient_vector((ID)v))
                    : as_persistent_vector((ID)v);
            int count = (int)vector_count(vec);
            size_t len = 2; // "[ ]"
            int i = 0;
            VECTOR_FOR_EACH(vec, elem) {
                len += to_string_calc_length((CljObject*)elem, escape_strings);
                if (i < count - 1) len += 1; // space
                i++;
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

        case CLJ_MAP_PERSISTENT:
        case CLJ_MAP_TRANSIENT: {
            CljPersistentMap *map = as_map(v);
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
            const char *native_name = (native_func->name_sym && native_func->name_sym->cname)
                ? native_func->name_sym->cname
                : NULL;

            if (native_name) {
                // Find namespace containing this function to get fully qualified name
                CljNamespace *ns = ns_find_for_object((CljObject*)v);
                const char *ns_name = (ns && ns->name && ns->name->cname) ? ns->name->cname : NULL;
                
                // Calculate length for fully qualified name if namespace is available
                if (ns_name) {
                    return 20 + strlen(ns_name) + strlen(native_name); // "#<native function NS/NAME>"
                } else {
                    return 19 + strlen(native_name); // "#<native function NAME>"
                }
            }
            return 20; // "#<native function>"
        }

        case CLJ_CLOSURE: {
            CljFunction *clj_func = (CljFunction*)v;
            const char *name = (clj_func && clj_func->name_sym && clj_func->name_sym->cname)
                ? clj_func->name_sym->cname
                : NULL;
            if (name) {
                CljNamespace *ns = ns_find_for_object((CljObject*)v);
                const char *ns_name = ns && ns->name && ns->name->cname ? ns->name->cname : NULL;
                size_t len = 20; // "#<Clojure function "
                if (ns_name) {
                    len += strlen(ns_name) + 1; // "ns/"
                }
                len += strlen(name) + 1; // "name>"
                return len;
            }
            return 20; // "#<Clojure function>"
        }

        case CLJ_SEQ: {
            CljSeqIterator *seq = as_seq(v);
            size_t len = 2; // "( )"
            SeqIterator temp_iter = seq->iter;
            bool first = true;

            // Avoid hidden allocations when iterating maps: seq_iter_first() for CLJ_MAP_PERSISTENT
            // materializes a temporary entry vector ([k v]). For printing, we can
            // format that representation directly.
            if (temp_iter.seq_type == CLJ_MAP_PERSISTENT) {
                CljPersistentMap *map = map_backing((ID)temp_iter.state.map.map);
                int index = temp_iter.state.map.index;
                int count = temp_iter.state.map.count;
                while (map && index < count) {
                    ID key = map->data[index * 2];
                    ID value = map->data[index * 2 + 1];

                    if (!first) len += 1; // space between seq elements

                    // Entry vector syntax: [k v]
                    len += 1; // '['
                    len += to_string_calc_length((CljObject*)key, escape_strings);
                    len += 1; // space
                    len += to_string_calc_length((CljObject*)value, escape_strings);
                    len += 1; // ']'

                    first = false;
                    index++;
                }
                return len;
            }

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
            char tmp[32];
            size_t line_len = clj_itoa((int32_t)exc->line, tmp);
            size_t col_len = clj_itoa((int32_t)exc->col, tmp);

            if (exc->file[0] != '\0') {
                // "%s: %s at %s:%d:%d"
                return strlen(exc->type) + 2 + strlen(exc->message) + 4 + strlen(exc->file) + 1 + line_len + 1 + col_len;
            }
            // "%s: %s at line %d, col %d"
            return strlen(exc->type) + 2 + strlen(exc->message) + 9 + line_len + 6 + col_len;
        }

        case CLJ_ATOM: {
            CljAtom *atom = as_atom(v);
            // "#<Atom@0x...: VALUE>"
            size_t len = 7; // "#<Atom@"
            len += 2 + hex_len_no_prefix_uintptr((uintptr_t)atom); // "0x" + address
            len += 2; // ": "
            if (atom->value) {
                len += to_string_calc_length((CljObject*)atom->value, escape_strings);
            } else {
                len += 3; // "nil"
            }
            len += 1; // ">"
            return len;
        }

        case CLJ_BYTE_ARRAY: {
            CljByteArray *ba = as_byte_array(v);
            int preview_len = ba->length < 8 ? ba->length : 8;
            size_t len = 15; // "#<byte-array ["
            len += preview_len * 5; // "0x00 "
            if (ba->length > 8) len += 5; // " ..."
            len += 2; // "]>"
            return len;
        }

        case CLJ_REGEX: {
            CljRegex *re = (CljRegex*)v;
            const char *pattern = regex_pattern_string(re);
            // Format: #"pattern"
            return 3 + strlen(pattern); // 2 chars (#") + pattern + closing "
        }

        case CLJ_INSTANT: {
            CljInstant *inst = (CljInstant*)v;
            char buf[64];
            return format_instant_iso_utc(buf, sizeof(buf), inst);
        }

        case CLJ_UUID: {
            char uuid[37];
            clj_uuid_to_cstring(v, uuid);
            return 7 + strlen(uuid) + 1; // "#uuid \"" + uuid + "\""
        }

        case CLJ_NAMESPACE: {
            CljNamespace *ns = (CljNamespace*)v;
            if (ns && ns->name && ns->name->cname) {
                return strlen(ns->name->cname);
            }
            return 12; // "#<namespace>"
        }

        default:
            return 10; // "#<unknown>"
    }
}
// Recursive helper: Build string into buffer
static void to_string_build_string(CljObject *v, char *buffer, size_t *offset, bool escape_strings) {
    if (!v) {
        memcpy(buffer + *offset, "nil", 3);
        *offset += 3;
        return;
    }

    CljValue val = (CljValue)v;
    if (is_immediate(val)) {
        if (is_fixnum(val)) {
            append_int32(buffer, offset, (int32_t)as_fixnum(val));
            return;
        }
        // CRITICAL: Check is_special BEFORE is_fixed to correctly identify booleans
        // clj_false has tag 5 (TAG_BOOL), not tag 7 (TAG_FIXED)
        if (is_special(val)) {
            uint8_t special = as_special(val);
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
        if (is_fixed(val)) {
            append_fixed2(buffer, offset, as_fixed(val));
            return;
        }
        if (is_character(val)) {
            buffer[*offset] = (char)as_character(val);
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

        case CLJ_VECTOR_PERSISTENT:
        case CLJ_VECTOR_TRANSIENT:{
            CljPersistentVector *vec =
                (v->type == CLJ_VECTOR_TRANSIENT)
                    ? vector_persistent(as_transient_vector((ID)v))
                    : as_persistent_vector((ID)v);
            if (v->type == CLJ_VECTOR_TRANSIENT) {
                memcpy(buffer + *offset, "<transient ", 11);
                *offset += 11;
            }
            buffer[*offset] = '[';
            *offset += 1;
            int count = vector_count(vec);
            int i = 0;
            VECTOR_FOR_EACH(vec, elem) {
                to_string_build_string((CljObject*)elem, buffer, offset, escape_strings);
                if (i < count - 1) {
                    buffer[*offset] = ' ';
                    *offset += 1;
                }
                i++;
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

        case CLJ_MAP_PERSISTENT:
        case CLJ_MAP_TRANSIENT: {
            CljPersistentMap *map = as_map(v);
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
            const char *native_name = (native_func->name_sym && native_func->name_sym->cname)
                ? native_func->name_sym->cname
                : NULL;

            if (native_name) {
                // Find namespace containing this function to get fully qualified name
                CljNamespace *ns = ns_find_for_object((CljObject*)v);
                const char *ns_name = (ns && ns->name && ns->name->cname) ? ns->name->cname : NULL;
                
                // Build fully qualified name if namespace is available
                if (ns_name) {
                    append_cstr(buffer, offset, "#<native function ");
                    append_cstr(buffer, offset, ns_name);
                    append_char(buffer, offset, '/');
                    append_cstr(buffer, offset, native_name);
                    append_char(buffer, offset, '>');
                } else {
                    append_cstr(buffer, offset, "#<native function ");
                    append_cstr(buffer, offset, native_name);
                    append_char(buffer, offset, '>');
                }
            } else {
                memcpy(buffer + *offset, "#<native function>", 19);
                *offset += 19;
            }
            return;
        }

        case CLJ_CLOSURE: {
            CljFunction *clj_func = (CljFunction*)v;
            const char *name = (clj_func && clj_func->name_sym && clj_func->name_sym->cname)
                ? clj_func->name_sym->cname
                : NULL;
            if (name) {
                CljNamespace *ns = ns_find_for_object((CljObject*)v);
                const char *ns_name = ns && ns->name && ns->name->cname ? ns->name->cname : NULL;
                append_cstr(buffer, offset, "#<Clojure function ");
                if (ns_name) {
                    append_cstr(buffer, offset, ns_name);
                    append_char(buffer, offset, '/');
                }
                append_cstr(buffer, offset, name);
                append_char(buffer, offset, '>');
            } else {
                memcpy(buffer + *offset, "#<Clojure function>", 19);
                *offset += 19;
            }
            return;
        }

        case CLJ_SEQ: {
            CljSeqIterator *seq = as_seq(v);
            buffer[*offset] = '(';
            *offset += 1;
            SeqIterator temp_iter = seq->iter;
            bool first = true;

            // Avoid hidden allocations for map sequences (see calc_length variant).
            if (temp_iter.seq_type == CLJ_MAP_PERSISTENT) {
                CljPersistentMap *map = map_backing((ID)temp_iter.state.map.map);
                int index = temp_iter.state.map.index;
                int count = temp_iter.state.map.count;
                while (map && index < count) {
                    ID key = map->data[index * 2];
                    ID value = map->data[index * 2 + 1];

                    if (!first) {
                        buffer[*offset] = ' ';
                        *offset += 1;
                    }

                    buffer[*offset] = '[';
                    *offset += 1;
                    to_string_build_string((CljObject*)key, buffer, offset, escape_strings);
                    buffer[*offset] = ' ';
                    *offset += 1;
                    to_string_build_string((CljObject*)value, buffer, offset, escape_strings);
                    buffer[*offset] = ']';
                    *offset += 1;

                    first = false;
                    index++;
                }
                buffer[*offset] = ')';
                *offset += 1;
                return;
            }

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

        case CLJ_INSTANT: {
            CljInstant *inst = (CljInstant*)v;
            size_t written = format_instant_iso_utc(buffer + *offset, 64, inst);
            *offset += written;
            return;
        }

        case CLJ_UUID: {
            char uuid[37];
            clj_uuid_to_cstring(v, uuid);
            append_cstr(buffer, offset, "#uuid \"");
            append_cstr(buffer, offset, uuid);
            append_char(buffer, offset, '"');
            return;
        }

        case CLJ_EXCEPTION: {
            CLJException *exc = (CLJException*)v;
            append_cstr(buffer, offset, exc->type);
            append_cstr(buffer, offset, ": ");
            append_cstr(buffer, offset, exc->message);

            if (exc->file[0] != '\0') {
                append_cstr(buffer, offset, " at ");
                append_cstr(buffer, offset, exc->file);
                append_char(buffer, offset, ':');
                append_int32(buffer, offset, (int32_t)exc->line);
                append_char(buffer, offset, ':');
                append_int32(buffer, offset, (int32_t)exc->col);
            } else {
                append_cstr(buffer, offset, " at line ");
                append_int32(buffer, offset, (int32_t)exc->line);
                append_cstr(buffer, offset, ", col ");
                append_int32(buffer, offset, (int32_t)exc->col);
            }
            return;
        }

        case CLJ_ATOM: {
            CljAtom *atom = as_atom(v);
            append_cstr(buffer, offset, "#<Atom@");
            append_ptr_hex(buffer, offset, (const void*)atom);
            append_cstr(buffer, offset, ": ");
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
            append_cstr(buffer, offset, "#<byte-array [");
            int preview_len = ba->length < 8 ? ba->length : 8;
            for (int i = 0; i < preview_len; i++) {
                if (i > 0) {
                    buffer[*offset] = ' ';
                    *offset += 1;
                }
                append_byte_hex2(buffer, offset, ba->data[i]);
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

        case CLJ_REGEX: {
            CljRegex *re = (CljRegex*)v;
            const char *pattern = regex_pattern_string(re);
            // Format: #"pattern"
            buffer[*offset] = '#';
            *offset += 1;
            buffer[*offset] = '"';
            *offset += 1;
            size_t pattern_len = strlen(pattern);
            memcpy(buffer + *offset, pattern, pattern_len);
            *offset += pattern_len;
            buffer[*offset] = '"';
            *offset += 1;
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
    // make_string_buffer allocates len + 1 bytes (including null terminator)
    // But we need to ensure offset doesn't exceed len
    CljString *result = (CljString*)AUTORELEASE(make_string_buffer(len));

    size_t offset = 0;
    to_string_build_string((CljObject*)v, result->data, &offset, escape_strings);
    // Safety check: ensure we don't write beyond allocated buffer
    if (offset > len) {
        offset = len;  // Truncate to allocated size
    }
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
