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
                if (elem_a) RELEASE(elem_a);
                if (elem_b) RELEASE(elem_b);
                if (!equal) return false;
            }
            return true;
        }
        
        case CLJ_MAP: {
            CljMap *map_a = as_map(a);
            CljMap *map_b = as_map(b);
            if (!map_a || !map_b) return false;
            if (map_a->count != map_b->count) return false;
            for (int i = 0; i < map_a->count; i++) {
                ID key_a = KV_KEY(map_a->data, i);
                ID val_a = KV_VALUE(map_a->data, i);
                ID val_b = map_get((CljMap*)b, key_a, NULL);
                // Map-Werte können immediates oder heap objects sein
                if (!clj_equal(val_a, val_b)) return false;
            }
            return true;
        }
        
        case CLJ_SYMBOL: {
            // Symbol comparison: name and namespace must match
            CljSymbol *sym_a = as_symbol(a);
            CljSymbol *sym_b = as_symbol(b);
            if (!sym_a || !sym_b) return false;
            if (!sym_a->name || !sym_b->name) return false;
            if (strcmp(sym_a->name, sym_b->name) != 0) return false;
            
            // Compare namespaces (both NULL or both same object)
            if (sym_a->ns == sym_b->ns) return true;
            if (!sym_a->ns || !sym_b->ns) return false;
            
            // Both have namespaces - compare their names
            CljSymbol *ns_a = sym_a->ns->name;
            CljSymbol *ns_b = sym_b->ns->name;
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

