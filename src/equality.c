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
#include "namespace.h"  // For CljNamespace definition
#include "vector.h"
#include "map.h"
#include "strings.h"
#include "kv_macros.h"
#include "list.h"

// Forward declaration for string_empty_singleton
extern CljString* string_empty_singleton;

bool clj_equal(ID a, ID b) {
    if (a == b) return true;  // Pointer-Gleichheit (für Singletons und Symbole)
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

    // Inhalt-Vergleich basierend auf Typ
    // Hinweis: CLJ_BOOL, CLJ_SYMBOL werden bereits durch Pointer-Vergleich abgefangen
    switch (a_obj->type) {
        // CLJ_INT, CLJ_FLOAT removed - handled as immediates

        // Komplexe Typen - Inhalt-Vergleich
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
        case CLJ_VECTOR_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            CljVector *vec_a = (CljVector*)a;
            CljVector *vec_b = (CljVector*)b;
            if (!vec_a || !vec_b) return false;
            int count_a = vector_count(vec_a);
            int count_b = vector_count(vec_b);
            if (count_a != count_b) return false;
            for (int i = 0; i < count_a; i++) {
                // Vektorelemente können immediates oder heap objects sein
                ID elem_a = vector_nth(vec_a, i);
                ID elem_b = vector_nth(vec_b, i);
                bool equal = clj_equal(elem_a, elem_b);
                // elem_a and elem_b lifetime is tied to vectors - no release needed
                if (!equal) return false;
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
                // Map-Werte können immediates oder heap objects sein
                if (!clj_equal((ID)val_a, val_b)) return false;
            }
            return true;
        }

        case CLJ_SYMBOL: {
            // Symbol comparison: name and namespace must match
            CljSymbol *sym_a = as_symbol(a);
            CljSymbol *sym_b = as_symbol(b);
            if (!sym_a || !sym_b) return false;
            
            // Safety check: ensure cname is valid before strcmp
            if (!sym_a->cname || !sym_b->cname) {
                // If both are NULL, they are equal; otherwise not equal
                return (sym_a->cname == NULL && sym_b->cname == NULL);
            }

            // Compare symbol names (must match)
            if (strcmp(sym_a->cname, sym_b->cname) != 0) return false;

            // Compare namespaces (pointer comparison works due to interning)
            if (sym_a->ns_name == sym_b->ns_name) return true;

            // If both symbols are unqualified (no namespace), they are equal
            // This handles cases where symbols are parsed in different contexts
            // but are structurally equivalent (e.g., unqualified symbols)
            if (!sym_a->ns_name && !sym_b->ns_name) return true;

            // If one has a namespace and the other doesn't, they are not equal
            if (!sym_a->ns_name || !sym_b->ns_name) return false;

            // Both have namespaces - compare namespace name strings (must match)
            // Safety check: ensure ns_name is a valid symbol before accessing cname
            if (TAG(sym_a->ns_name) != CLJ_SYMBOL || TAG(sym_b->ns_name) != CLJ_SYMBOL) return false;
            CljSymbol *ns_a = as_symbol(sym_a->ns_name);
            CljSymbol *ns_b = as_symbol(sym_b->ns_name);
            if (!ns_a || !ns_b || !ns_a->cname || !ns_b->cname) return false;
            return strcmp(ns_a->cname, ns_b->cname) == 0;
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
                if (TAG(rest_a) != CLJ_LIST || TAG(rest_b) != CLJ_LIST) {
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

        // Referenz-Typen - nur Pointer-Vergleich (bereits durch a == b abgefangen)
        case CLJ_FUNC:
        case CLJ_CLOSURE:
            // Functions are only equal if they're the same instance
            return a == b;

        // Unbekannte oder nicht unterstützte Typen
        default:
            return false;
    }
}

