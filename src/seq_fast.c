/*
 * Fast Seq Implementation for Tiny-CLJ
 * 
 * Stack-allocated iterator with zero-copy semantics.
 */

#include "seq_fast.h"
#include "list_operations.h"
#include "vector.h"
#include "string.h"
#include <string.h>

// ============================================================================
// FAST SEQ IMPLEMENTATION
// ============================================================================

bool fast_seq_init(FastSeqIterator *iter, CljObject *obj) {
    if (!iter) return false;
    
    // Initialize to empty
    memset(iter, 0, sizeof(FastSeqIterator));
    
    // Handle nil
    if (!obj) {
        iter->seq_type = CLJ_NIL;
        return true;  // Empty sequence, but valid
    }
    
    iter->container = obj;
    
    switch (obj->type) {
        case CLJ_LIST: {
            CljList *list_data = as_list(obj);
            if (!list_data || !list_data->head) {
                iter->seq_type = CLJ_NIL;
                return true;  // Empty list
            }
            
            iter->state.list.current = list_data->head;
            iter->state.list.index = 0;
            iter->seq_type = CLJ_LIST;
            return true;
        }
        
        case CLJ_VECTOR: {
            CljPersistentVector *vec = as_vector(obj);
            if (!vec || vec->count == 0) {
                iter->seq_type = CLJ_NIL;
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
            const char *str_data = (const char*)obj->as.data;
            if (!str_data || str_data[0] == '\0') {
                iter->seq_type = CLJ_NIL;
                return true;  // Empty string
            }
            
            iter->state.str.data = str_data;
            iter->state.str.index = 0;
            iter->state.str.length = strlen(str_data);
            iter->seq_type = CLJ_STRING;
            return true;
        }
        
        case CLJ_NIL:
            iter->seq_type = CLJ_NIL;
            return true;
        
        default:
            return false;  // Not seqable
    }
}

CljObject* fast_seq_first(const FastSeqIterator *iter) {
    if (!iter || fast_seq_empty(iter)) {
        return clj_nil();
    }
    
    switch (iter->seq_type) {
        case CLJ_LIST: {
            if (iter->state.list.current) {
                CljList *node = as_list(iter->state.list.current);
                return node ? node->head : clj_nil();
            }
            return clj_nil();
        }
        
        case CLJ_VECTOR: {
            if (iter->state.vec.index < iter->state.vec.count) {
                return iter->state.vec.data[iter->state.vec.index];
            }
            return clj_nil();
        }
        
        case CLJ_STRING: {
            if (iter->state.str.index < iter->state.str.length) {
                // Return character as integer
                char c = iter->state.str.data[iter->state.str.index];
                return make_int((int)c);
            }
            return clj_nil();
        }
        
        default:
            return clj_nil();
    }
}

bool fast_seq_next(FastSeqIterator *iter) {
    if (!iter || fast_seq_empty(iter)) {
        return false;
    }
    
    switch (iter->seq_type) {
        case CLJ_LIST: {
            if (iter->state.list.current) {
                CljList *node = as_list(iter->state.list.current);
                if (node && node->tail) {
                    iter->state.list.current = node->tail;
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

bool fast_seq_empty(const FastSeqIterator *iter) {
    if (!iter) return true;
    
    switch (iter->seq_type) {
        case CLJ_LIST:
            return iter->state.list.current == NULL;
        
        case CLJ_VECTOR:
            return iter->state.vec.index >= iter->state.vec.count;
        
        case CLJ_STRING:
            return iter->state.str.index >= iter->state.str.length;
        
        case CLJ_NIL:
            return true;
        
        default:
            return true;
    }
}

int fast_seq_position(const FastSeqIterator *iter) {
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

