#include "record.h"

#include "runtime.h"
#include "symbol.h"
#include "symbol_token.h"
#include "seq.h"
#include "strings.h"
#include "value.h"
#include "exception.h"
#include "mini_format.h"

// Normalize a field designator to a keyword (e.g. x, "x", :x -> :x).
static ID keyword_from_field(ID field) {
    if (!field)
        return NULL;
    if (IS_KEYWORD(field))
        return field;

    const char *name = NULL;
    if (TAG(field) == CLJ_SYMBOL) {
        CljSymbol *sym = as_symbol(field);
        name = sym ? sym->cname : NULL;
    } else if (TAG(field) == CLJ_SYMBOL_TOKEN) {
        name = symbol_token_data(field);
    } else if (TAG(field) == CLJ_STRING) {
        name = string_data(field);
    }

    if (!name || !name[0])
        return NULL;
    while (*name == ':')
        name++;
    if (!name[0])
        return NULL;

    char kw_name[256] = {0};
    size_t pos = format_append_char(kw_name, 0, sizeof(kw_name), ':');
    format_append(kw_name, pos, sizeof(kw_name), name);
    return intern_symbol_global(kw_name);
}

// Convert an arbitrary seqable field collection to a keyword vector.
// Returns owned vector (rc=1), or NULL on invalid input/allocation failure.
static CljPersistentVector *coerce_fields_to_vector(ID fields) {
    if (!fields)
        return NULL;

    CljPersistentVector *out = make_vector(4, STRONG);
    if (!out)
        return NULL;

    CljType tag = TAG(fields);
    if (tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT) {
        CljPersistentVector *vec = as_vector(fields);
        unsigned int count = vector_count(vec);
        for (unsigned int i = 0; i < count; i++) {
            ID key = keyword_from_field(vector_nth(vec, i));
            if (!key) {
                RELEASE(out);
                return NULL;
            }
            vector_conj_inplace(&out, key);
        }
        return out;
    }

    if (!is_seqable(fields)) {
        RELEASE(out);
        return NULL;
    }

    SeqIterator iter;
    if (!seq_iter_init(&iter, fields)) {
        RELEASE(out);
        return NULL;
    }

    while (!seq_iter_empty(&iter)) {
        ID key = keyword_from_field(seq_iter_first(&iter));
        if (!key) {
            RELEASE(out);
            return NULL;
        }
        vector_conj_inplace(&out, key);
        seq_iter_next(&iter);
    }

    return out;
}

static size_t append_field_debug_name(char *buf, size_t pos, size_t cap, ID field) {
    if (!buf || cap == 0u) {
        return pos;
    }
    if (!field) {
        return format_append(buf, pos, cap, "nil");
    }
    if (TAG(field) == CLJ_SYMBOL) {
        CljSymbol *sym = as_symbol(field);
        return format_append(buf, pos, cap, (sym && sym->cname) ? sym->cname : "<symbol>");
    }
    if (TAG(field) == CLJ_STRING) {
        return format_append(buf, pos, cap, string_data(field));
    }
    return format_append(buf, pos, cap, "<field>");
}

static void format_field_vector_debug(char *buf, size_t cap, ID fields) {
    if (!buf || cap == 0u) {
        return;
    }
    buf[0] = '\0';
    size_t pos = format_append_char(buf, 0u, cap, '[');
    CljPersistentVector *vec = as_vector(fields);
    if (!vec) {
        pos = format_append(buf, pos, cap, "<non-vector>");
        (void)format_append_char(buf, pos, cap, ']');
        return;
    }
    unsigned int count = vector_count(vec);
    for (unsigned int i = 0; i < count; i++) {
        if (i > 0u) {
            pos = format_append_char(buf, pos, cap, ' ');
        }
        pos = append_field_debug_name(buf, pos, cap, vector_nth(vec, i));
    }
    (void)format_append_char(buf, pos, cap, ']');
}

// Look up a record descriptor by type symbol in the runtime registry.
// Returns borrowed descriptor pointer, or NULL if not found.
CljRecordDescriptor *record_descriptor_lookup(ID type_symbol) {
    if (!type_symbol || TAG(type_symbol) != CLJ_SYMBOL)
        return NULL;
    if (!g_runtime.record_registry)
        return NULL;

    ID desc = hashmap_get_sentinel(g_runtime.record_registry, type_symbol, NULL);
    if (!desc || TAG(desc) != CLJ_RECORD_DESCRIPTOR)
        return NULL;
    return as_record_descriptor(desc);
}

// Register or reuse a descriptor for a record type.
// Returns borrowed descriptor pointer. Throws on invalid args or mismatched redefinition.
CljRecordDescriptor *record_register_descriptor(ID type_symbol, ID fields) {
    if (!type_symbol || TAG(type_symbol) != CLJ_SYMBOL) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "record-register requires a symbol type name",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljPersistentVector *field_keys = coerce_fields_to_vector(fields);
    if (!field_keys) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "record-register requires a seqable collection of fields",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljRecordDescriptor *existing = record_descriptor_lookup(type_symbol);
    if (existing) {
        if (!clj_equal(existing->field_keys, field_keys)) {
            const char *type_name = NULL;
            if (TAG(type_symbol) == CLJ_SYMBOL) {
                CljSymbol *sym = as_symbol(type_symbol);
                type_name = (sym && sym->cname) ? sym->cname : NULL;
            }
            char existing_repr[96];
            char incoming_repr[96];
            char msg[256];
            format_field_vector_debug(existing_repr, sizeof(existing_repr), existing->field_keys);
            format_field_vector_debug(incoming_repr, sizeof(incoming_repr), (ID)field_keys);
            size_t msg_pos = format_append(msg, 0u, sizeof(msg), "record type ");
            msg_pos = format_append(msg, msg_pos, sizeof(msg), type_name ? type_name : "<unknown>");
            msg_pos = format_append(msg, msg_pos, sizeof(msg), " already registered with different fields (existing=");
            msg_pos = format_append(msg, msg_pos, sizeof(msg), existing_repr);
            msg_pos = format_append(msg, msg_pos, sizeof(msg), ", new=");
            msg_pos = format_append(msg, msg_pos, sizeof(msg), incoming_repr);
            (void)format_append_char(msg, msg_pos, sizeof(msg), ')');
            RELEASE(field_keys);
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            msg,
                            __FILE__, __LINE__, 0);
            return NULL;
        }
        RELEASE(field_keys);
        return existing;
    }

    if (!g_runtime.record_registry) {
        g_runtime.record_registry = make_hashmap(64);
        if (!g_runtime.record_registry) {
            RELEASE(field_keys);
            return NULL;
        }
    }

    CljRecordDescriptor *desc = record_descriptor_create(type_symbol, field_keys);
    RELEASE(field_keys);
    if (!desc)
        return NULL;

    hashmap_assoc_inplace(&g_runtime.record_registry, type_symbol, desc);
    RELEASE(desc);
    return record_descriptor_lookup(type_symbol);
}
