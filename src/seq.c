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
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            CljVector *vec = as_vector(obj);
            if (!vec) {
                return true;  // Empty vector
            }
            
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
            if (!map || map->count == 0) {
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
                return LIST_FIRST(node);
            }
            return NULL;
        }
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT: {
            if (iter->state.vec.index < iter->state.vec.count) {
                // vector_nth returns element with lifetime tied to vector - no retain needed
                CljVector *vec = (CljVector*)iter->container;
                return vector_nth(vec, iter->state.vec.index);
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
                if (rest && list_type_matches(TAG(rest))) {
                    iter->state.list.current = rest;
                    iter->state.list.index++;
                    return true;
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
                return LIST_FIRST(list) == NULL;
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
    
    // If already a CLJ_SEQ, return it directly (no need to wrap again)
    if (obj && TAG(obj) == CLJ_SEQ) {
        CljSeqIterator *seq = as_seq(obj);
        return seq;  // Already a seq, return as-is
    }
    
    // Check if collection is empty
    if (obj && TAG(obj) == CLJ_VECTOR) {
        CljVector *vec = as_vector((CljObject*)obj);
        if (vec && vector_count(vec) == 0) return NULL;
    } else if (obj && list_type_matches(TAG(obj))) {
        CljList *list = as_list((CljObject*)obj);
        if (!LIST_FIRST(list)) return NULL;
    } else if (obj && (TAG(obj) == CLJ_MAP || TAG(obj) == CLJ_MAP_TRANSIENT)) {
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
        free(heap_seq);
        return NULL;  // Empty or not seqable
    }
    
    // If iterator is empty, return nil (NULL) - JVM-compatible
    if (seq_iter_empty(&heap_seq->iter)) {
        free(heap_seq);
        return NULL;
    }
    
    return heap_seq;
}

void seq_release(ID seq_obj) {
    if (!seq_obj) return;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return;
    
    // Stack iterator doesn't need cleanup
    free(seq);
}

ID seq_first(ID seq_obj) {
    if (!seq_obj) return NULL;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return NULL;
    
    return seq_iter_first(&seq->iter);
}

ID seq_rest(ID seq_obj) {
    if (!seq_obj) return NULL;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return NULL;
    
    // Create new heap wrapper with advanced iterator
    // Use malloc instead of calloc - all fields are immediately initialized
    CljSeqIterator *rest_seq = (CljSeqIterator*)malloc(sizeof(CljSeqIterator));
    if (!rest_seq) return NULL;
    
    rest_seq->base.type = CLJ_SEQ;
    rest_seq->base.rc = 1;
    
    // Copy iterator state
    rest_seq->iter = seq->iter;  // Struct copy
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
                // CRITICAL: RETAIN and AUTORELEASE the rest list because it's part of the original list structure
                // and may be freed when the original list is freed
                // RETAIN and AUTORELEASE handle NULL safely
                return AUTORELEASE(RETAIN(rest));
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
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return true;
    
    return seq_iter_empty(&seq->iter);
}

int seq_count(ID obj) {
    if (!obj) return 0;
    
    // If it's already a seq wrapper, count from iterator state
    if (obj && TAG(obj) == CLJ_SEQ) {
        CljSeqIterator *seq = as_seq(obj);
        if (!seq) return 0;
        
        // Get count from embedded iterator state
        switch (seq->iter.seq_type) {
            case CLJ_VECTOR:
            case CLJ_VECTOR_TRANSIENT_WEAK:
            case CLJ_VECTOR_TRANSIENT:
                return seq->iter.state.vec.count;
            case CLJ_LIST:
                // List doesn't have direct count in state, fall through to iterate
                break;
            case CLJ_STRING:
                return seq->iter.state.str.length;
            case CLJ_MAP:
                return seq->iter.state.map.count;
            default:
                return 0;
        }
    }
    
    // Fast path for vectors - O(1)
    if (obj && TAG(obj) == CLJ_VECTOR) {
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
    if (!obj) return true; // nil is seqable
    
    switch (((CljObject*)obj)->type) {
        case CLJ_LIST:
        case CLJ_AST_NODE:
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT_WEAK:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_MAP:
        case CLJ_STRING:
        case CLJ_SEQ:  // Sequences are seqable
        // Note: nil is now represented as NULL
            return true;
        default:
            return false;
    }
}

bool is_seq(ID obj) {
    if (!obj) return false;
    if (list_type_matches(TAG(obj))) {
        return true;
    }
    return TAG(obj) == CLJ_SEQ;
}

