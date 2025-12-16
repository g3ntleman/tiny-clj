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

// Forward declaration for string_empty_singleton
extern CljString* string_empty_singleton;

// Full equality implementation for all Clojure types
// Registered as callback in runtime_init()
bool clj_equal_full(ID a, ID b) {
    if (a == b) return true;  // Pointer equality (for singletons and interned symbols)
    if (!a || !b) return false;

    // Handle tagged integers (fixnums) - most common case
    if (is_fixnum(a) || is_fixnum(b)) {
        if (is_fixnum(a) && is_fixnum(b)) {
            return as_fixnum(a) == as_fixnum(b);
        }
        return false;  // Different types
    }

    // Handle other immediate types (bool, etc.)
    if (is_immediate(a) || is_immediate(b)) {
        if (is_immediate(a) && is_immediate(b)) {
            return a == b;  // For immediates, pointer equality is sufficient
        }
        return false;  // Different types
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
            if (!str_a || !str_b) return false;

            // Special case: empty string singleton comparison
            if (str_a == string_empty_singleton && str_b == string_empty_singleton) {
                return true;
            }

            // Compare string data directly
            return strcmp(str_a->data, str_b->data) == 0;
        }

        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            CljVector *vec_a = (CljVector*)a;
            CljVector *vec_b = (CljVector*)b;
            if (!vec_a || !vec_b) return false;
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
            if (!map_a || !map_b) return false;
            if (map_a->count != map_b->count) return false;
            MAP_FOR_EACH(map_a, key_a, val_a) {
                ID val_b = map_get((CljMap*)b, (ID)key_a, NULL);
                if (!clj_equal((ID)val_a, val_b)) return false;
            }
            return true;
        }

        case CLJ_LIST: {
            // Structural equality for lists (needed for metadata lookup)
            CljList *list_a = as_list(a);
            CljList *list_b = as_list(b);
            if (!list_a || !list_b) return false;

            // Compare lists element by element
            CljList *curr_a = list_a;
            CljList *curr_b = list_b;
            while (curr_a && curr_b) {
                // Compare first elements
                if (!clj_equal((ID)curr_a->first, (ID)curr_b->first)) {
                    return false;
                }
                // Move to next elements
                CljObject *rest_a = curr_a->rest;
                CljObject *rest_b = curr_b->rest;
                // Check if rest is NULL (end of list)
                if (!rest_a && !rest_b) return true;
                if (!rest_a || !rest_b) return false;
                // Check if rest is a list
                if (!list_type_matches(TAG(rest_a)) || !list_type_matches(TAG(rest_b))) {
                    // Rest is not a list - compare directly
                    if (!clj_equal((ID)rest_a, (ID)rest_b)) {
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

