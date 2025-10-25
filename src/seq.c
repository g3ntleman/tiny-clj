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
#include <string.h>
#include <stdlib.h>

// ============================================================================
// FAST SEQ IMPLEMENTATION
// ============================================================================

bool seq_iter_init(SeqIterator *iter, CljObject *obj) {
    if (!iter) return false;
    
    // Initialize to empty
    memset(iter, 0, sizeof(SeqIterator));
    
    // Handle nil (now represented as NULL)
    if (!obj) {
        iter->seq_type = CLJ_UNKNOWN; // Use UNKNOWN for empty sequence
        return true;  // Empty sequence, but valid
    }
    
    iter->container = obj;
    
    switch (obj->type) {
        case CLJ_LIST: {
            CljList *list_data = as_list(obj);
            if (!LIST_FIRST(list_data)) {
                iter->seq_type = CLJ_UNKNOWN;
                return true;  // Empty list
            }
            
            iter->state.list.current = LIST_FIRST(list_data);
            iter->state.list.index = 0;
            iter->seq_type = CLJ_LIST;
            return true;
        }
        
        case CLJ_VECTOR: {
            CljPersistentVector *vec = as_vector(obj);
            if (!vec || vec->count == 0) {
                iter->seq_type = CLJ_UNKNOWN;
                return true;  // Empty vector
            }
            
            // Zero-copy: direct pointer to vector data
            iter->state.vec.data = vec->data;
            iter->state.vec.index = 0;
            iter->state.vec.count = vec->count;
            iter->seq_type = CLJ_VECTOR;
            return true;
        }
        
        case CLJ_STRING: {
            // String data is stored directly after CljObject header
            char **str_ptr = (char**)((char*)obj + sizeof(CljObject));
            const char *str_data = (const char*)*str_ptr;
            if (!str_data || str_data[0] == '\0') {
                iter->seq_type = CLJ_UNKNOWN;
                return true;  // Empty string
            }
            
            iter->state.str.data = str_data;
            iter->state.str.index = 0;
            iter->state.str.length = strlen(str_data);
            iter->seq_type = CLJ_STRING;
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
                return (ID)LIST_FIRST(node);
            }
            return NULL;
        }
        
        case CLJ_VECTOR: {
            if (iter->state.vec.index < iter->state.vec.count) {
                return (ID)iter->state.vec.data[iter->state.vec.index];
            }
            return NULL;
        }
        
        case CLJ_STRING: {
            if (iter->state.str.index < iter->state.str.length) {
                // Return character as integer
                char c = iter->state.str.data[iter->state.str.index];
                return (ID)fixnum((int)c);
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
                if (node->rest) {
                    iter->state.list.current = node->rest;
                    iter->state.list.index++;
                    return true;
                }
            }
            // Mark as exhausted
            iter->state.list.current = NULL;
            return false;
        }
        
        case CLJ_VECTOR: {
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
        
        default:
            return false;
    }
}

bool seq_iter_empty(const SeqIterator *iter) {
    if (!iter) return true;
    
    switch (iter->seq_type) {
        case CLJ_LIST:
            return iter->state.list.current == NULL;
        
        case CLJ_VECTOR:
            return iter->state.vec.index >= iter->state.vec.count;
        
        case CLJ_STRING:
            return iter->state.str.index >= iter->state.str.length;
        
        // Note: nil is now represented as NULL
            return true;
        
        default:
            return true;
    }
}

int seq_iter_position(const SeqIterator *iter) {
    if (!iter) return 0;
    
    switch (iter->seq_type) {
        case CLJ_LIST:
            return iter->state.list.index;
        case CLJ_VECTOR:
            return iter->state.vec.index;
        case CLJ_STRING:
            return iter->state.str.index;
        default:
            return 0;
    }
}

// ============================================================================
// COMPATIBILITY LAYER (Heap-based API)
// ============================================================================

CljObject* seq_create(ID obj) {
    // Handle nil and empty collections - return nil singleton
    if (!obj) return NULL;
    
    // Check if collection is empty
    if (is_type((CljObject*)obj, CLJ_VECTOR)) {
        CljPersistentVector *vec = as_vector((CljObject*)obj);
        if (vec && vec->count == 0) return NULL;
    } else if (is_type((CljObject*)obj, CLJ_LIST)) {
        CljList *list = as_list((CljObject*)obj);
        if (!LIST_FIRST(list)) return NULL;
    }
    
    // Allocate heap wrapper
    CljSeqIterator *heap_seq = calloc(1, sizeof(CljSeqIterator));
    if (!heap_seq) return NULL;
    
    heap_seq->base.type = CLJ_SEQ;
    heap_seq->base.rc = 1;
    
    // Initialize embedded stack iterator
    if (!seq_iter_init(&heap_seq->iter, (CljObject*)obj)) {
        free(heap_seq);
        return NULL;  // Empty or not seqable
    }
    
    // If iterator is empty (seq_type == CLJ_UNKNOWN), return nil (NULL)
    if (heap_seq->iter.seq_type == CLJ_UNKNOWN) {
        free(heap_seq);
        return NULL;
    }
    
    return (CljObject*)heap_seq;
}

void seq_release(ID seq_obj) {
    if (!seq_obj) return;
    CljSeqIterator *seq = as_seq((ID)seq_obj);
    if (!seq) return;
    
    // Stack iterator doesn't need cleanup
    free(seq);
}

ID seq_first(ID seq_obj) {
    if (!seq_obj) return NULL;
    CljSeqIterator *seq = as_seq((ID)seq_obj);
    if (!seq) return NULL;
    
    return seq_iter_first(&seq->iter);
}

ID seq_rest(ID seq_obj) {
    if (!seq_obj) return NULL;
    CljSeqIterator *seq = as_seq((ID)seq_obj);
    if (!seq) return NULL;
    
    // Create new heap wrapper with advanced iterator
    CljSeqIterator *rest_seq = calloc(1, sizeof(CljSeqIterator));
    if (!rest_seq) return NULL;
    
    rest_seq->base.type = CLJ_SEQ;
    rest_seq->base.rc = 1;
    
    // Copy iterator state
    rest_seq->iter = seq->iter;  // Struct copy
    seq_iter_next(&rest_seq->iter);
    
    return (ID)(CljObject*)rest_seq;
}

ID seq_next(ID seq_obj) {
    return seq_rest(seq_obj);
}

bool seq_empty(ID seq_obj) {
    if (!seq_obj) return true;
    CljSeqIterator *seq = as_seq((ID)seq_obj);
    if (!seq) return true;
    
    return seq_iter_empty(&seq->iter);
}

int seq_count(ID obj) {
    if (!obj) return 0;
    
    // If it's already a seq wrapper, count from iterator state
    if (is_type((CljObject*)obj, CLJ_SEQ)) {
        CljSeqIterator *seq = as_seq((ID)obj);
        if (!seq) return 0;
        
        // Get count from embedded iterator state
        switch (seq->iter.seq_type) {
            case CLJ_VECTOR:
                return seq->iter.state.vec.count;
            case CLJ_LIST:
                // List doesn't have direct count in state, fall through to iterate
                break;
            case CLJ_STRING:
                return seq->iter.state.str.length;
            default:
                return 0;
        }
    }
    
    // Fast path for vectors - O(1)
    if (is_type((CljObject*)obj, CLJ_VECTOR)) {
        CljPersistentVector *vec = as_vector((CljObject*)obj);
        return vec ? vec->count : 0;
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
        case CLJ_VECTOR:
        case CLJ_MAP:
        case CLJ_STRING:
        // Note: nil is now represented as NULL
            return true;
        default:
            return false;
    }
}

bool is_seq(ID obj) {
    return TYPE((CljObject*)obj) == CLJ_SEQ || TYPE((CljObject*)obj) == CLJ_LIST;
}

