/*
 * Seq Implementation for Tiny-CLJ
 * 
 * Stack-allocated iterator with zero-copy semantics.
 * Optimized for embedded systems.
 */

#include "seq.h"
#include "value.h"
#include "list.h"
#include "vector.h"
#include "strings.h"
#include "map.h"
#include "symbol.h"
#include "memory.h"
#include "namespace.h"
#include "eval.h"
#include "function.h"
#include <string.h>
#include <stdlib.h>

static ID make_map_entry_vector(CljMap *map, int index) {
    if (!map || index < 0 || index >= map->count) {
        return NULL;
    }

    CljObject *key = map->data[index * 2];
    CljObject *value = map->data[index * 2 + 1];

    CljVector *entry = make_vector(2, CLJ_VECTOR);
    if (!entry) {
        return NULL;
    }

    entry = vector_conj(entry, key);
    entry = vector_conj(entry, value);

    return AUTORELEASE(entry);
}

// ============================================================================
// FAST SEQ IMPLEMENTATION
// ============================================================================

bool seq_iter_init(SeqIterator *iter, ID obj) {
    if (!iter) return false;
    
    // Initialize to empty
    memset(iter, 0, sizeof(SeqIterator));
    
    // Handle nil (now represented as NULL)
    if (!obj) {
        // Empty sequence - don't set seq_type, leave it as 0
        return true;  // Empty sequence, but valid
    }
    
    iter->container = obj;
    CljObject *o = obj;
    
    switch (o->type) {
        case CLJ_LIST:
        case CLJ_AST_NODE: {
            CljList *list_data = as_list(obj);
            // Note: LIST_FIRST can be NULL (nil) - it's a valid value in Clojure lists
            // A list is only empty if list_data itself is NULL or the list structure is invalid
            // We check if list_data is valid and has a structure (even if first element is nil)
            if (!list_data) {
                // Empty list - don't set seq_type, leave it as 0
                return true;  // Empty list
            }
            
            // Note: Empty list singleton is handled above (list_data check)
            // In Clojure, () is nil, not an empty list
            // empty_list() singleton is only used by (list) function
            
            // Store the list node itself, not the first element
            // Note: LIST_FIRST(list_data) can be NULL (nil) - this is valid
            iter->state.list.current = (CljObject*)list_data;
            iter->state.list.index = 0;
            iter->seq_type = CLJ_LIST;
            return true;
        }
        
        case CLJ_SEQ: {
            // Already a sequence - copy the embedded iterator state
            CljSeqIterator *seq = as_seq(obj);
            if (!seq) {
                // Empty seq - don't set seq_type, leave it as 0
                return true;  // Empty seq
            }
            
            // Copy the embedded iterator state
            *iter = seq->iter;  // Struct copy
            return true;
        }
        
        case CLJ_LAZY_SEQ: {
            // Lazy sequence - treat as a list and iterate through it
            CljLazySeq *lazy = as_lazy_seq(obj);
            if (!lazy || !lazy->first) {
                // Empty lazy seq - don't set seq_type, leave it as 0
                return true;  // Empty lazy seq
            }
            
            // Convert lazy-seq to list for iteration
            // We'll iterate by calling seq_first and seq_rest
            iter->state.list.current = (CljObject*)obj;  // Store lazy-seq itself
            iter->state.list.index = 0;
            iter->seq_type = CLJ_LAZY_SEQ;
            return true;
        }
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            CljVector *vec = as_vector(obj);
            
            // Use vector_init_seq_iterator to avoid exposing internal data pointer
            if (!vector_init_seq_iterator(iter, vec)) {
                return true;  // Empty vector
            }
            return true;
        }
        
        case CLJ_STRING: {
            CljString *str = (CljString*)obj;
            
            // Special case: empty string singleton
            if (str == string_empty_singleton) {
                // Empty string - don't set seq_type, leave it as 0
                return true;  // Empty string
            }
            
            // Access string data directly
            iter->state.str.data = str->data;
            iter->state.str.index = 0;
            iter->state.str.length = str->length;
            iter->seq_type = CLJ_STRING;
            return true;
        }
        
        case CLJ_MAP: {
            CljMap *map = as_map(obj);
            if (map->count == 0) {
                return true;  // Empty map
            }

            iter->state.map.map = (struct CljMap *)map;
            iter->state.map.index = 0;
            iter->state.map.count = map->count;
            iter->seq_type = CLJ_MAP;
            return true;
        }

        // Note: nil is now represented as NULL, handled above
        return true;
        
        default:
            return false;  // Not seqable
    }
}

ID seq_iter_first(const SeqIterator *iter) {
    if (!iter || seq_iter_empty(iter)) {
        return NULL;
    }
    
    switch (iter->seq_type) {
        case CLJ_LIST: {
            if (iter->state.list.current) {
                CljList *node = as_list(iter->state.list.current);
                ID elem = LIST_FIRST(node);
                // Convert SYM_NIL to NULL (nil representation)
                return (elem == SYM_NIL) ? NULL : elem;
            }
            return NULL;
        }
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            if (iter->state.vec.index < iter->state.vec.count) {
                // vector_nth returns element with lifetime tied to vector - no retain needed
                CljVector *vec = (CljVector*)iter->container;
                ID elem = vector_nth(vec, iter->state.vec.index);
                // Convert SYM_NIL to NULL (nil representation)
                return (elem == SYM_NIL) ? NULL : elem;
            }
            return NULL;
        }
        
        case CLJ_STRING: {
            if (iter->state.str.index < iter->state.str.length) {
                // Return character as integer
                char c = iter->state.str.data[iter->state.str.index];
                return fixnum((int)c);
            }
            return NULL;
        }

        case CLJ_MAP: {
            if (iter->state.map.index < iter->state.map.count) {
                return make_map_entry_vector((CljMap *)iter->state.map.map, iter->state.map.index);
            }
            return NULL;
        }
        
        case CLJ_LAZY_SEQ: {
            if (iter->state.list.current) {
                CljLazySeq *lazy = as_lazy_seq(iter->state.list.current);
                if (lazy && lazy->first) {
                    return RETAIN(lazy->first);
                }
            }
            return NULL;
        }
        
        default:
            return NULL;
    }
}

bool seq_iter_next(SeqIterator *iter) {
    if (!iter || seq_iter_empty(iter)) {
        return false;
    }
    
    switch (iter->seq_type) {
        case CLJ_LIST: {
            if (iter->state.list.current) {
                CljList *node = as_list(iter->state.list.current);
                CljObject *rest = LIST_REST(node);
                // Check if rest is a non-empty list
                // Use list_empty to properly handle list with nil element
                if (rest && list_type_matches(TAG(rest))) {
                    CljList *rest_list = as_list(rest);
                    // Only continue if rest is not empty
                    if (!list_empty(rest_list)) {
                        iter->state.list.current = rest;
                        iter->state.list.index++;
                        return true;
                    }
                }
            }
            // Mark as exhausted
            iter->state.list.current = NULL;
            return false;
        }
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            if (iter->state.vec.index < iter->state.vec.count - 1) {
                iter->state.vec.index++;
                return true;
            }
            // Mark as exhausted
            iter->state.vec.index = iter->state.vec.count;
            return false;
        }
        
        case CLJ_STRING: {
            if (iter->state.str.index < iter->state.str.length - 1) {
                iter->state.str.index++;
                return true;
            }
            // Mark as exhausted
            iter->state.str.index = iter->state.str.length;
            return false;
        }

        case CLJ_MAP: {
            if (iter->state.map.index < iter->state.map.count - 1) {
                iter->state.map.index++;
                return true;
            }
            iter->state.map.index = iter->state.map.count;
            return false;
        }
        
        case CLJ_LAZY_SEQ: {
            if (iter->state.list.current) {
                CljLazySeq *lazy = as_lazy_seq(iter->state.list.current);
                if (lazy) {
                    ID rest = seq_rest((ID)lazy);
                    if (rest && is_lazy_seq(rest)) {
                        iter->state.list.current = (CljObject*)rest;
                        iter->state.list.index++;
                        return true;
                    } else if (rest) {
                        // Rest is not a lazy-seq, convert to list for iteration
                        iter->state.list.current = (CljObject*)rest;
                        iter->state.list.index++;
                        // Update seq_type if rest is a list
                        if (TAG(rest) == CLJ_LIST) {
                            iter->seq_type = CLJ_LIST;
                        }
                        return true;
                    }
                }
            }
            // Mark as exhausted
            iter->state.list.current = NULL;
            return false;
        }
        
        default:
            return false;
    }
}

bool seq_iter_empty(const SeqIterator *iter) {
    if (!iter) return true;
    
    // Check if container is nil
    if (!iter->container) return true;
    
    // Check if container is an empty collection singleton
    if (is_singleton(iter->container)) {
        // Check if it's actually empty based on type
        switch (iter->container->type) {
            case CLJ_VECTOR:
            case CLJ_VECTOR_TRANSIENT_WEAK:
            case CLJ_VECTOR_TRANSIENT: {
                CljVector *vec = (CljVector*)iter->container;
                return vector_count(vec) == 0;
            }
            case CLJ_LIST: {
                CljList *list = (CljList*)iter->container;
                // Use list_empty to properly handle list with nil element
                return list_empty(list);
            }
            case CLJ_STRING: {
                CljString *str = (CljString*)iter->container;
                return str == string_empty_singleton || str->length == 0;
            }
            default:
                return true;
        }
    }
    
    // Check based on seq_type for non-empty sequences
    switch (iter->seq_type) {
        case CLJ_LIST:
            return iter->state.list.current == NULL;
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT:
            return iter->state.vec.index >= iter->state.vec.count;
        
        case CLJ_STRING:
            return iter->state.str.index >= iter->state.str.length;

        case CLJ_MAP:
            return iter->state.map.index >= iter->state.map.count;
        
        case CLJ_LAZY_SEQ:
            return iter->state.list.current == NULL;
        
        default:
            // If seq_type is 0 (not set), it's an empty sequence
            return iter->seq_type == 0;
    }
}

int seq_iter_position(const SeqIterator *iter) {
    if (!iter) return 0;
    
    switch (iter->seq_type) {
        case CLJ_LIST:
            return iter->state.list.index;
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT:
            return iter->state.vec.index;
        case CLJ_STRING:
            return iter->state.str.index;
        case CLJ_MAP:
            return iter->state.map.index;
        default:
            return 0;
    }
}

// ============================================================================
// COMPATIBILITY LAYER (Heap-based API)
// ============================================================================

CljSeqIterator* make_seq(ID obj) {
    // Handle nil and empty collections - return nil singleton
    if (!obj) return NULL;
    
    unsigned char obj_tag = TAG(obj);
    
    // If already a CLJ_SEQ, return it directly (no need to wrap again)
    if (obj_tag == CLJ_SEQ) {
        CljSeqIterator *seq = as_seq(obj);
        return seq;  // Already a seq, return as-is
    }
    
    // Check if collection is empty
    if (obj_tag == CLJ_VECTOR) {
        CljVector *vec = as_vector((CljObject*)obj);
        if (vec && vector_count(vec) == 0) return NULL;
    } else if (list_type_matches(obj_tag)) {
        CljList *list = as_list((CljObject*)obj);
        if (!LIST_FIRST(list)) return NULL;
    } else if (obj_tag == CLJ_MAP || obj_tag == CLJ_MAP_TRANSIENT) {
        CljMap *map = as_map(obj);
        if (!map || map->count == 0) return NULL;
    }
    
    // Allocate heap wrapper
    // Use malloc instead of calloc - all fields are immediately initialized
    CljSeqIterator *heap_seq = (CljSeqIterator*)malloc(sizeof(CljSeqIterator));
    if (!heap_seq) return NULL;
    
    heap_seq->base.type = CLJ_SEQ;
    heap_seq->base.rc = 1;
    
    // Initialize embedded stack iterator
    if (!seq_iter_init(&heap_seq->iter, (CljObject*)obj)) {
        DEALLOC(heap_seq);
        return NULL;  // Empty or not seqable
    }
    
    // If iterator is empty, return nil (NULL) - JVM-compatible
    if (seq_iter_empty(&heap_seq->iter)) {
        DEALLOC(heap_seq);
        return NULL;
    }
    
    return heap_seq;
}

CljLazySeq* make_lazy_seq(ID first, ID rest_fn) {
    CljLazySeq *lazy = ALLOC(CljLazySeq, 1);
    lazy->base.type = CLJ_LAZY_SEQ;
    lazy->base.rc = 1;
    lazy->first = RETAIN(first);
    lazy->rest_fn = RETAIN(rest_fn);
    lazy->cached_rest = NULL;
    return lazy;
}

void seq_release(ID seq_obj) {
    if (!seq_obj) return;
    
    if (is_lazy_seq(seq_obj)) {
        CljLazySeq *lazy = as_lazy_seq(seq_obj);
        RELEASE(lazy->first);
        
        // Für native Generator-Funktionen: env-Pointer freigeben falls RangeParams oder RepeatedlyParams
        if (lazy->rest_fn && TAG(lazy->rest_fn) == CLJ_FUNC) {
            CljCFunc *native_func = (CljCFunc*)lazy->rest_fn;
            // Prüfe ob env ein RangeParams* oder RepeatedlyParams* ist
            // Wir erkennen es am Funktionsnamen
            if (native_func->env && native_func->name) {
                if (strcmp(native_func->name, "range-gen") == 0 ||
                    strcmp(native_func->name, "repeatedly-gen") == 0) {
                    free(native_func->env);
                    native_func->env = NULL; // Prevent double free
                }
            }
        }
        
        RELEASE(lazy->rest_fn);
        RELEASE(lazy->cached_rest);
        DEALLOC(lazy);
        return;
    }
    
    CljSeqIterator *seq = as_seq(seq_obj);
    DEALLOC(seq);
}

ID seq_first(ID seq_obj) {
    if (!seq_obj) return NULL;
    
    if (is_lazy_seq(seq_obj)) {
        return RETAIN(as_lazy_seq(seq_obj)->first);
    }
    
    CljSeqIterator *seq = as_seq(seq_obj);
    return seq ? seq_iter_first(&seq->iter) : NULL;
}

ID seq_rest(ID seq_obj) {
    if (!seq_obj) return NULL;
    
    if (is_lazy_seq(seq_obj)) {
        CljLazySeq *lazy = as_lazy_seq(seq_obj);
        if (lazy->cached_rest) {
            return RETAIN(lazy->cached_rest);
        }
        if (!lazy->rest_fn) {
            return NULL;
        }
        
        // Für native Funktionen: Übergib die Funktion selbst als ersten Parameter
        // damit die Generator-Funktion auf env zugreifen kann
        if (TAG(lazy->rest_fn) == CLJ_FUNC) {
            CljCFunc *native_func = (CljCFunc*)lazy->rest_fn;
            if (native_func && native_func->fn) {
                ID fn_args[1] = {lazy->rest_fn};
                ID rest_result = native_func->fn((CljObject**)fn_args, 1);
                lazy->cached_rest = RETAIN(rest_result);
                return RETAIN(rest_result);
            }
        }
        
        // Clojure-Funktion: Normale eval_function_call
        ID rest_result = eval_function_call(lazy->rest_fn, NULL, 0, NULL, get_global_eval_state());
        lazy->cached_rest = RETAIN(rest_result);
        return RETAIN(rest_result);
    }
    
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return NULL;
    
    CljSeqIterator *rest_seq = (CljSeqIterator*)malloc(sizeof(CljSeqIterator));
    if (!rest_seq) return NULL;
    
    rest_seq->base.type = CLJ_SEQ;
    rest_seq->base.rc = 1;
    rest_seq->iter = seq->iter;
    seq_iter_next(&rest_seq->iter);
    
    return (CljObject*)rest_seq;
}

ID seq_next(ID seq_obj) {
    if (!seq_obj) return NULL;
    
    // CRITICAL: If the original sequence was a CLJ_LIST, return CLJ_LIST directly
    // This matches the behavior of native_next in builtins.c
    CljSeqIterator *seq = as_seq(seq_obj);
    if (seq && seq->iter.seq_type == CLJ_LIST) {
        // Original was a CLJ_LIST - return CLJ_LIST directly (not CLJ_SEQ)
        if (seq->iter.state.list.current) {
            CljList *current_list = as_list(seq->iter.state.list.current);
            if (current_list) {
                CljObject *rest = LIST_REST(current_list);
                // next returns nil if rest is empty, otherwise rest
                // rest is part of the original list structure, which is already safe (caller has strong reference)
                return rest;
            }
        }
        // Empty list - return nil
        return NULL;
    }

    // For other types (CLJ_VECTOR, CLJ_STRING, etc.), use seq_rest
    // Get rest sequence (DRY: reuse seq_rest implementation)
    ID rest_seq = seq_rest(seq_obj);
    if (!rest_seq) return NULL;
    
    // Check if rest is empty - if so, return nil (Clojure-compatible)
    if (seq_empty(rest_seq)) {
        seq_release(rest_seq);
        return NULL;  // nil
    }
    
    // Rest is non-empty, return it
    return rest_seq;
}

ID seq_next_inplace(ID seq_obj) {
    if (!seq_obj) return NULL;
    
    if (is_lazy_seq(seq_obj)) {
        CljLazySeq *lazy = as_lazy_seq(seq_obj);
        
        // Recycling: Bei rc==1 können wir das lazy-seq Objekt selbst mutieren
        if (lazy->base.rc == 1) {
            // Hole nächste Sequenz vom Generator (falls noch nicht gecacht)
            if (!lazy->cached_rest) {
                if (!lazy->rest_fn) {
                    return NULL;
                }
                ID rest_result = eval_function_call(lazy->rest_fn, NULL, 0, NULL, get_global_eval_state());
                lazy->cached_rest = RETAIN(rest_result);
            }
            
            ID next_seq = lazy->cached_rest;
            if (!next_seq || seq_empty(next_seq)) {
                // Sequenz zu Ende - setze first auf NULL
                RELEASE(lazy->first);
                lazy->first = NULL;
                RELEASE(lazy->rest_fn);
                lazy->rest_fn = NULL;
                RELEASE(lazy->cached_rest);
                lazy->cached_rest = NULL;
                return NULL;
            }
            
            // Recycling: Nur wenn nächste Sequenz auch lazy-seq ist
            if (is_lazy_seq(next_seq)) {
                CljLazySeq *next_lazy = as_lazy_seq(next_seq);
                
                // Update lazy-seq auf nächste Sequenz (recycle Objekt in-place)
                ID next_first = next_lazy->first;
                ID next_rest_fn = next_lazy->rest_fn;
                
                RELEASE(lazy->first);
                lazy->first = RETAIN(next_first);
                RELEASE(lazy->rest_fn);
                lazy->rest_fn = RETAIN(next_rest_fn);
                
                // cached_rest zurücksetzen für nächste Iteration
                RELEASE(lazy->cached_rest);
                lazy->cached_rest = NULL;
                
                // Release next_lazy da wir seine Felder übernommen haben
                RELEASE(next_lazy->first);
                RELEASE(next_lazy->rest_fn);
                DEALLOC(next_lazy);
                
                return seq_obj; // Gleiches Objekt wiederverwendet
            }
            
            // Nächste Sequenz ist normale Sequenz - kein Recycling möglich
            // Fallback zu normaler Logik
        }
        
        // RC>1: Normale Logik (kein Recycling, da mehrere Referenzen existieren)
        return seq_next(seq_obj);
    }
    
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return NULL;
    
    if (seq->iter.seq_type == CLJ_LIST) {
        return seq_next(seq_obj);
    }
    
    if (!seq_iter_next(&seq->iter)) {
        return NULL;
    }
    
    return seq_obj;
}

bool seq_empty(ID seq_obj) {
    if (!seq_obj) return true;
    
    if (is_lazy_seq(seq_obj)) {
        return as_lazy_seq(seq_obj)->first == NULL;
    }
    
    CljSeqIterator *seq = as_seq(seq_obj);
    return seq ? seq_iter_empty(&seq->iter) : true;
}

int seq_count(ID obj) {
    if (!obj) return 0;
    
    // If it's already a seq wrapper, count from iterator state
    if (TAG(obj) == CLJ_SEQ) {
        CljSeqIterator *seq = as_seq(obj);
        if (!seq) return 0;
        
        // Get count from embedded iterator state (remaining elements)
        switch (seq->iter.seq_type) {
            case CLJ_VECTOR:
            case CLJ_VECTOR_TRANSIENT_WEAK:
            case CLJ_VECTOR_TRANSIENT:
                // Return remaining elements, not total count
                return seq->iter.state.vec.count - seq->iter.state.vec.index;
            case CLJ_LIST:
                // List doesn't have direct count in state, fall through to iterate
                break;
            case CLJ_STRING:
                // Return remaining characters, not total length
                return seq->iter.state.str.length - seq->iter.state.str.index;
            case CLJ_MAP:
                // Return remaining entries, not total count
                return seq->iter.state.map.count - seq->iter.state.map.index;
            default:
                return 0;
        }
    }
    
    // Fast path for vectors - O(1)
    if (TAG(obj) == CLJ_VECTOR) {
        CljVector *vec = as_vector((CljObject*)obj);
        return vec ? vector_count(vec) : 0;
    }
    
    // Fallback: iterate and count - O(n)
    SeqIterator iter;
    if (!seq_iter_init(&iter, (CljObject*)obj)) return 0;
    
    int count = 0;
    while (!seq_iter_empty(&iter)) {
        count++;
        seq_iter_next(&iter);
    }
    return count;
}

// ============================================================================
// SEQABLE PREDICATES (Compatibility)
// ============================================================================

bool is_seqable(ID obj) {
    if (!obj) return true;
    
    switch (((CljObject*)obj)->type) {
        case CLJ_LIST:
        case CLJ_AST_NODE:
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_MAP:
        case CLJ_STRING:
        case CLJ_SEQ:
        case CLJ_LAZY_SEQ:
            return true;
        default:
            return false;
    }
}

bool is_seq(ID obj) {
    if (!obj) return false;
    CljType tag = TAG(obj);
    return list_type_matches(tag) || tag == CLJ_SEQ || tag == CLJ_LAZY_SEQ;
}

