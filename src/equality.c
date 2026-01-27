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
#include "kv_macros.h"
#include "list.h"

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

    // Check that both objects have the same type
    if (!a_obj || !b_obj || a_obj->type != b_obj->type) return false;

    // Content comparison based on type
    // Note: CLJ_BOOL, CLJ_SYMBOL are handled by pointer comparison (line 23)
    // Symbols are interned - only identity (pointer comparison) is needed
    switch (a_obj->type) {
        // CLJ_INT, CLJ_FLOAT removed - handled as immediates
        // CLJ_SYMBOL removed - handled by pointer comparison (line 23) due to interning

        // Complex types - content comparison
        case CLJ_STRING: {
            CljString *str_a = (CljString*)a;
            CljString *str_b = (CljString*)b;

            // Special case: empty string singleton comparison
            if (str_a == string_empty_singleton && str_b == string_empty_singleton) {
                return true;
            }

            // Compare string data directly
            return strcmp(str_a->data, str_b->data) == 0;
        }

        case CLJ_VECTOR_PERSISTENT:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            CljPersistentVector *vec_a = (CljPersistentVector*)a;
            CljPersistentVector *vec_b = (CljPersistentVector*)b;
            int count_a = vector_count(vec_a);
            int count_b = vector_count(vec_b);
            if (count_a != count_b) return false;
            for (int i = 0; i < count_a; i++) {
                // Vector elements can be immediates or heap objects
                ID elem_a = vector_nth(vec_a, i);
                ID elem_b = vector_nth(vec_b, i);
                if (!clj_equal(elem_a, elem_b)) return false;
            }
            return true;
        }

        case CLJ_MAP: {
            CljMap *map_a = as_map(a);
            CljMap *map_b = as_map(b);
            if (map_a->count != map_b->count) return false;
            MAP_FOR_EACH(map_a, key_a, val_a) {
                ID val_b = map_get(map_b, key_a);
                if (val_b == NOT_FOUND) return false;
                if (!clj_equal(val_a, val_b)) return false;
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

