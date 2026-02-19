/*
 * Equality Comparison
 *
 * Structural equality comparison for Clojure values.
 * Implements clj_equal() for comparing Clojure objects by value.
 */

#include <string.h>
#include "object.h"
#include "value.h"
#include "symbol.h"
#include "vector.h"
#include "map.h"
#include "strings.h"
#include "symbol_token.h"
#include "kv_macros.h"
#include "list.h"
#include "seq.h"
#include "hashset.h"
#include "record.h"

#include "instant.h"
#include "uuid.h"

// Forward declaration for string_empty_singleton
extern CljString* string_empty_singleton;

// Full equality implementation for all Clojure types
// Registered as callback in runtime_init()
bool clj_equal_full(ID a, ID b) {
    // Pointer equality covers ALL immediates (fixnum, char, bool, fixed)
    // because their value is encoded in the pointer itself
    if (a == b) return true;
    if (!a || !b) return false;

    // After a == b check, unequal immediates are automatically unequal
    // (fixnum(5) == fixnum(5) is always true via pointer comparison)
    if (is_immediate(a) || is_immediate(b)) {
        return false;  // One or both are immediates and a != b
    }

    // Handle CljObject* types
    CljObject *a_obj = (CljObject*)a;
    CljObject *b_obj = (CljObject*)b;
    CLJ_ASSERT(a_obj && b_obj && "clj_equal_full expects non-null heap objects after immediate checks");

    // Clojure-compatible sequential equality:
    // Lists, vectors, seq wrappers and lazy seqs compare by element sequence,
    // even across different concrete types (e.g. '(1 2) == [1 2]).
    const unsigned char ta = TAG(a_obj);
    const unsigned char tb = TAG(b_obj);
    const bool a_seq = (ta == CLJ_LIST || ta == CLJ_VECTOR_PERSISTENT || ta == CLJ_SEQ || ta == CLJ_LAZY_SEQ);
    const bool b_seq = (tb == CLJ_LIST || tb == CLJ_VECTOR_PERSISTENT || tb == CLJ_SEQ || tb == CLJ_LAZY_SEQ);
    if (a_seq && b_seq) {
        SeqIterator ia, ib;
        if (!seq_iter_init(&ia, a_obj) || !seq_iter_init(&ib, b_obj)) {
            return false;
        }
        while (!seq_iter_empty(&ia) && !seq_iter_empty(&ib)) {
            ID ea = seq_iter_first(&ia);
            ID eb = seq_iter_first(&ib);
            if (!clj_equal(ea, eb)) return false;
            seq_iter_next(&ia);
            seq_iter_next(&ib);
        }
        return seq_iter_empty(&ia) && seq_iter_empty(&ib);
    }

    // Special-case symbol tokens vs interned symbols (compare by name).
    if (ta == CLJ_SYMBOL_TOKEN && tb == CLJ_SYMBOL) {
        const char *tok = symbol_token_data((CljSymbolToken*)a);
        const CljSymbol *sym = (const CljSymbol*)b;
        CLJ_ASSERT(tok && sym && sym->cname && "symbol-token vs symbol compare expects valid names");
        return strcmp(tok, sym->cname) == 0;
    }
    if (tb == CLJ_SYMBOL_TOKEN && ta == CLJ_SYMBOL) {
        const char *tok = symbol_token_data((CljSymbolToken*)b);
        const CljSymbol *sym = (const CljSymbol*)a;
        CLJ_ASSERT(tok && sym && sym->cname && "symbol-token vs symbol compare expects valid names");
        return strcmp(tok, sym->cname) == 0;
    }
    if (ta == CLJ_SYMBOL_TOKEN && tb == CLJ_SYMBOL_TOKEN) {
        const char *a_tok = symbol_token_data((CljSymbolToken*)a);
        const char *b_tok = symbol_token_data((CljSymbolToken*)b);
        CLJ_ASSERT(a_tok && b_tok && "symbol-token compare expects valid token strings");
        return strcmp(a_tok, b_tok) == 0;
    }

    // Otherwise require same concrete type for structural equality.
    if (a_obj->type != b_obj->type) return false;

    // Content comparison based on type
    // Note: CLJ_BOOL handled by pointer comparison (line 23)
    switch (a_obj->type) {
        // CLJ_INT, CLJ_FLOAT removed - handled as immediates
        // Complex types - content comparison
        case CLJ_STRING: {
            CljString *str_a = (CljString*)a;
            CljString *str_b = (CljString*)b;

            uint16_t len_a = string_length((ID)str_a);
            uint16_t len_b = string_length((ID)str_b);
            if (len_a != len_b) return false;

            const char *data_a = string_data((ID)str_a);
            const char *data_b = string_data((ID)str_b);
            if (len_a == 0) return true;
            return memcmp(data_a, data_b, len_a) == 0;
        }

        case CLJ_VECTOR_PERSISTENT: {
            CljPersistentVector *vec_a =
                as_persistent_vector(a);
            CljPersistentVector *vec_b =
                as_persistent_vector(b);
            int count_a = (int)vector_count(vec_a);
            int count_b = (int)vector_count(vec_b);
            if (count_a != count_b) return false;
            for (int i = 0; i < count_a; i++) {
                // Vector elements can be immediates or heap objects
                ID elem_a = vector_nth(vec_a, i);
                ID elem_b = vector_nth(vec_b, i);
                if (!clj_equal(elem_a, elem_b)) return false;
            }
            return true;
        }

        case CLJ_VECTOR_TRANSIENT:
            // Transients do not have value semantics (only identity via a==b)
            return false;

        case CLJ_SYMBOL: {
            const CljSymbol *sa = (const CljSymbol*)a;
            const CljSymbol *sb = (const CljSymbol*)b;
            CLJ_ASSERT(sa && sb && sa->cname && sb->cname && "CLJ_SYMBOL values must have names");
            if (sa->cname == sb->cname) {
                // Fast path: shared name pointer (interned)
            } else {
                if (strcmp(sa->cname, sb->cname) != 0) return false;
            }
            const CljSymbol *nsa = (const CljSymbol*)sa->ns_name;
            const CljSymbol *nsb = (const CljSymbol*)sb->ns_name;
            if (nsa == nsb) return true; // includes both NULL
            if (!nsa || !nsb) return false;
            CLJ_ASSERT(nsa->cname && nsb->cname && "symbol namespace symbols must have names");
            return strcmp(nsa->cname, nsb->cname) == 0;
        }
        case CLJ_MAP_PERSISTENT: {
            CljPersistentMap *map_a = as_map(a);
            CljPersistentMap *map_b = as_map(b);
            CLJ_ASSERT(map_a && map_b && "CLJ_MAP_PERSISTENT values must have backing maps");
            if (map_a->count != map_b->count) return false;
            MAP_FOR_EACH(map_a, key_a, val_a) {
                ID val_b = map_get(map_b, key_a);
                if (val_b == NOT_FOUND) return false;
                if (!clj_equal(val_a, val_b)) return false;
            }
            return true;
        }

        case CLJ_RECORD: {
            CljPersistentRecord *ra = as_record(a);
            CljPersistentRecord *rb = as_record(b);
            CLJ_ASSERT(ra && rb && ra->descriptor && rb->descriptor && "record equality requires valid descriptors");
            // Record type identity is descriptor identity; same type symbol alone is not sufficient
            // across unload/reload scenarios with potentially different field layouts.
            if (ra->descriptor != rb->descriptor) return false;
            if (ra->field_count != rb->field_count) return false;
            for (unsigned int i = 0; i < ra->field_count; i++) {
                if (!clj_equal(ra->values[i], rb->values[i])) return false;
            }
            return true;
        }

        case CLJ_HASHSET: {
            CljHashSet *hs_a = (CljHashSet*)a;
            CljHashSet *hs_b = (CljHashSet*)b;
            CLJ_ASSERT(hs_a && hs_b && "CLJ_HASHSET values must be non-null");
            if (hashset_count(hs_a) != hashset_count(hs_b)) return false;
            ID key;
            HASHSET_FOR_EACH(hs_a, key) {
                if (!hashset_contains(hs_b, key)) return false;
            }
            return true;
        }

        case CLJ_LIST: {
            // Structural equality for lists (needed for metadata lookup)
            CljList *list_a = as_list(a);
            CljList *list_b = as_list(b);

            // Compare lists element by element
            CljList *curr_a = list_a;
            CljList *curr_b = list_b;
            while (curr_a && curr_b) {
                // Compare first elements
                if (!clj_equal(curr_a->first, curr_b->first)) {
                    return false;
                }
                // Move to next elements
                CljObject *rest_a = curr_a->rest;
                CljObject *rest_b = curr_b->rest;
                // Check if rest is NULL (end of list)
                if (!rest_a && !rest_b) return true;
                if (!rest_a || !rest_b) return false;
                // Check if rest is a list
                if (!is_list_type(TAG(rest_a)) || !is_list_type(TAG(rest_b))) {
                    // Rest is not a list - compare directly
                    if (!clj_equal(rest_a, rest_b)) {
                        return false;
                    }
                    return true; // Both have same non-list rest
                }
                curr_a = as_list(rest_a);
                curr_b = as_list(rest_b);
            }
            // Both should be NULL at the same time
            return curr_a == curr_b;
        }

        case CLJ_INSTANT: {
            CljInstant *ia = (CljInstant*)a;
            CljInstant *ib = (CljInstant*)b;
            return ia->days == ib->days && ia->ms == ib->ms;
        }

        case CLJ_UUID: {
            CljUUID *ua = (CljUUID*)a;
            CljUUID *ub = (CljUUID*)b;
            return memcmp(ua->bytes, ub->bytes, 16) == 0;
        }

        // Reference types - only pointer comparison (already handled by a == b)
        case CLJ_FUNC:
        case CLJ_CLOSURE:
            // Functions are only equal if they're the same instance
            return a == b;

        // Unknown or unsupported types
        default:
            return false;
    }
}
