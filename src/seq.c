/*
 * Seq Implementation for Tiny-CLJ
 * 
 * Provides iterator-based sequence views for efficient traversal
 * without heap allocation overhead.
 */

#include "seq.h"
#include "list_operations.h"
#include "vector.h"
#include "map.h"
#include "string.h"
#include "clj_symbols.h"
#include "memory_hooks.h"
#include <stdlib.h>
#include <string.h>

// ============================================================================
// INTERNAL HELPER FUNCTIONS
// ============================================================================

// Iterator state for different sequence types
typedef struct {
    int index;              // Current position
    int count;              // Total count
    CljObject **data;       // Data array (for vectors)
    CljObject *current;     // Current node (for lists)
} ListIteratorState;

typedef struct {
    int index;              // Current position
    int count;              // Total count
    CljObject **data;       // Data array
} VectorIteratorState;

typedef struct {
    int index;              // Current position
    int count;              // Total count
    CljObject **keys;       // Key array
    CljObject **values;     // Value array
} MapIteratorState;

typedef struct {
    int index;              // Current position
    int length;             // String length
    const char *data;       // String data
} StringIteratorState;

// ============================================================================
// SEQ CREATION AND MANAGEMENT
// ============================================================================

/**
 * @brief Create a sequence iterator for any seqable Clojure object
 * @param obj The Clojure object to create a sequence iterator for (can be NULL for nil)
 * @return A new SeqIterator (caller must call seq_release()) or NULL on error
 * 
 * This function creates a unified sequence iterator that can handle all seqable types:
 * - CLJ_LIST: Iterates through linked list elements
 * - CLJ_VECTOR: Iterates through vector elements by index
 * - CLJ_MAP: Iterates through key-value pairs (keys at even indices, values at odd)
 * - CLJ_STRING: Iterates through individual characters
 * - CLJ_NIL: Creates an empty sequence iterator
 * 
 * Memory Policy:
 * - Returns a new SeqIterator that must be released with seq_release()
 * - Retains the container object to prevent premature deallocation
 * - Allocates type-specific state structures for iteration tracking
 * - Returns NULL for non-seqable types or allocation failures
 * 
 * Usage Pattern:
 *   SeqIterator *seq = seq_create(obj);
 *   if (seq) {
 *       // Use seq_first(), seq_rest(), etc.
 *       seq_release(seq);  // Always release when done
 *   }
 */
CljObject* seq_create(CljObject *obj) {
    // Handle NULL (nil) case - create empty sequence iterator
    if (!obj) {
        CljSeqIterator *seq = calloc(1, sizeof(CljSeqIterator));
        if (!seq) return NULL;
        seq->base.type = CLJ_SEQ;
        seq->base.rc = 1;
        seq->container = NULL;    // No container for nil
        seq->state = NULL;        // No iteration state needed
        seq->seq_type = CLJ_NIL;   // Mark as nil sequence
        return (CljObject*)seq;
    }
    
    // Allocate the main sequence iterator structure
    CljSeqIterator *seq = calloc(1, sizeof(CljSeqIterator));
    if (!seq) return NULL;
    seq->base.type = CLJ_SEQ;
    seq->base.rc = 1;
    
    // Retain the container object to prevent premature deallocation during iteration
    seq->container = obj;
    RETAIN(obj); // Borrowed reference - prevents container from being freed
    
    // Create type-specific iterator state based on object type
    switch (obj->type) {
        case CLJ_LIST: {
            // List iteration: traverse linked list structure
            CljList *list_data = as_list(obj);
            if (!list_data) {
                free(seq);
                return NULL;
            }
            
            // Check if list is empty - return nil singleton for empty lists
            int count = list_count(obj);
            if (count == 0) {
                RELEASE(obj);  // Release the retained reference
                free(seq);
                return clj_nil();    // Empty list -> nil sequence (singleton)
            }
            
            // Allocate list-specific iteration state
            ListIteratorState *state = calloc(1, sizeof(ListIteratorState));
            state->current = list_data->head;  // Start at head of list
            state->index = 0;                  // Track position in sequence
            state->count = count;             // Total number of elements
            seq->state = state;
            seq->seq_type = CLJ_LIST;
            break;
        }
        
        case CLJ_VECTOR: {
            // Vector iteration: access elements by index
            CljPersistentVector *vec = as_vector(obj);
            if (!vec) {
                free(seq);
                return NULL;
            }
            
            // Allocate vector-specific iteration state
            VectorIteratorState *state = calloc(1, sizeof(VectorIteratorState));
            state->index = 0;              // Start at index 0
            state->count = vec->count;     // Total number of elements
            state->data = vec->data;       // Direct access to vector data
            seq->state = state;
            seq->seq_type = CLJ_VECTOR;
            break;
        }
        
        case CLJ_MAP: {
            // Map iteration: traverse key-value pairs
            CljMap *map = as_map(obj);
            if (!map) {
                free(seq);
                return NULL;
            }
            
            // Allocate map-specific iteration state
            MapIteratorState *state = calloc(1, sizeof(MapIteratorState));
            state->index = 0;              // Start at index 0
            state->count = map->count;     // Total number of key-value pairs
            state->keys = map->data;       // Keys are at even indices (0, 2, 4, ...)
            state->values = map->data + 1; // Values are at odd indices (1, 3, 5, ...)
            seq->state = state;
            seq->seq_type = CLJ_MAP;
            break;
        }
        
        case CLJ_STRING: {
            // String iteration: traverse individual characters
            StringIteratorState *state = calloc(1, sizeof(StringIteratorState));
            state->index = 0;                              // Start at character 0
            state->length = strlen((char*)obj->as.data);  // Total string length
            state->data = (const char*)obj->as.data;       // Direct access to string data
            seq->state = state;
            seq->seq_type = CLJ_STRING;
            break;
        }
        
        case CLJ_NIL:
            // Empty sequence - create a minimal state for nil
            seq->state = NULL;        // No iteration state needed
            seq->seq_type = CLJ_NIL;   // Mark as nil sequence
            seq->container = NULL;     // Don't retain NULL
            break;
            
        default:
            // Not seqable - free allocated memory and return NULL
            free(seq);
            return NULL;
    }
    
    return (CljObject*)seq;
}

void seq_release(CljObject *seq_obj) {
    if (!seq_obj) return;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq) return;
    
    if (seq->container) {
        release(seq->container);
    }
    
    if (seq->state) {
        free(seq->state);
    }
    
    free(seq);
}

void seq_retain(CljObject *seq_obj) {
    if (!seq_obj) return;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (seq && seq->container) {
        RETAIN(seq->container);
    }
}

// ============================================================================
// SEQ OPERATIONS
// ============================================================================

CljObject* seq_first(CljObject *seq_obj) {
    if (!seq_obj) return clj_nil();
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq || !seq->state) return clj_nil();
    
    switch (seq->seq_type) {
        case CLJ_LIST: {
            ListIteratorState *state = (ListIteratorState*)seq->state;
            if (state->current) {
                RETAIN(state->current);
                return state->current;
            }
            break;
        }
        
        case CLJ_VECTOR: {
            VectorIteratorState *state = (VectorIteratorState*)seq->state;
            if (state->index < state->count && state->data[state->index]) {
                RETAIN(state->data[state->index]);
                return state->data[state->index];
            }
            break;
        }
        
        case CLJ_MAP: {
            MapIteratorState *state = (MapIteratorState*)seq->state;
            if (state->index < state->count) {
                // Return [key value] pair for map entries
                CljObject *key = state->keys[state->index];
                CljObject *value = state->values[state->index];
                if (key && value) {
                    CljObject *pair = make_vector(2, 1);
                    CljPersistentVector *pair_vec = as_vector(pair);
                    if (pair_vec) {
                        RETAIN(key);
                        RETAIN(value);
                        pair_vec->data[0] = key;
                        pair_vec->data[1] = value;
                        pair_vec->count = 2;
                        return pair;
                    }
                }
            }
            break;
        }
        
        case CLJ_STRING: {
            StringIteratorState *state = (StringIteratorState*)seq->state;
            if (state->index < state->length) {
                // Return single character as string
                char ch = state->data[state->index];
                char str[2] = {ch, '\0'};
                return make_string(str);
            }
            break;
        }
        
        case CLJ_NIL:
        default:
            break;
    }
    
    return clj_nil();
}

CljObject* seq_rest(CljObject *seq_obj) {
    if (!seq_obj) return NULL;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq || !seq->state) return NULL;
    
    CljSeqIterator *rest_seq = calloc(1, sizeof(CljSeqIterator));
    if (!rest_seq) return NULL;
    rest_seq->base.type = CLJ_SEQ;
    rest_seq->base.rc = 1;
    
    rest_seq->container = seq->container;
    RETAIN(rest_seq->container);
    rest_seq->seq_type = seq->seq_type;
    
    switch (seq->seq_type) {
        case CLJ_LIST: {
            ListIteratorState *old_state = (ListIteratorState*)seq->state;
            ListIteratorState *new_state = calloc(1, sizeof(ListIteratorState));
            
            if (old_state->current && as_list(old_state->current)) {
                CljList *list_data = as_list(old_state->current);
                new_state->current = list_data ? list_data->tail : NULL;
                new_state->index = old_state->index + 1;
                new_state->count = old_state->count - 1;
            } else {
                new_state->current = NULL;
                new_state->index = old_state->index;
                new_state->count = 0;
            }
            rest_seq->state = new_state;
            break;
        }
        
        case CLJ_VECTOR: {
            VectorIteratorState *old_state = (VectorIteratorState*)seq->state;
            VectorIteratorState *new_state = calloc(1, sizeof(VectorIteratorState));
            
            new_state->index = old_state->index + 1;
            new_state->count = old_state->count - 1;
            new_state->data = old_state->data;
            rest_seq->state = new_state;
            break;
        }
        
        case CLJ_MAP: {
            MapIteratorState *old_state = (MapIteratorState*)seq->state;
            MapIteratorState *new_state = calloc(1, sizeof(MapIteratorState));
            
            new_state->index = old_state->index + 1;
            new_state->count = old_state->count - 1;
            new_state->keys = old_state->keys;
            new_state->values = old_state->values;
            rest_seq->state = new_state;
            break;
        }
        
        case CLJ_STRING: {
            StringIteratorState *old_state = (StringIteratorState*)seq->state;
            StringIteratorState *new_state = calloc(1, sizeof(StringIteratorState));
            
            new_state->index = old_state->index + 1;
            new_state->length = old_state->length;
            new_state->data = old_state->data;
            rest_seq->state = new_state;
            break;
        }
        
        case CLJ_NIL:
        default:
            free(rest_seq);
            return NULL;
    }
    
    return (CljObject*)rest_seq;
}

CljObject* seq_next(CljObject *seq_obj) {
    return seq_rest(seq_obj);
}

bool seq_empty(CljObject *seq_obj) {
    if (!seq_obj) return true;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq || !seq->state) return true;
    
    switch (seq->seq_type) {
        case CLJ_LIST: {
            ListIteratorState *state = (ListIteratorState*)seq->state;
            return state->current == NULL;
        }
        
        case CLJ_VECTOR: {
            VectorIteratorState *state = (VectorIteratorState*)seq->state;
            return state->index >= state->count;
        }
        
        case CLJ_MAP: {
            MapIteratorState *state = (MapIteratorState*)seq->state;
            return state->index >= state->count;
        }
        
        case CLJ_STRING: {
            StringIteratorState *state = (StringIteratorState*)seq->state;
            return state->index >= state->length;
        }
        
        case CLJ_NIL:
        default:
            return true;
    }
}

int seq_count(CljObject *seq_obj) {
    if (!seq_obj) return 0;
    CljSeqIterator *seq = as_seq(seq_obj);
    if (!seq || !seq->state) return 0;
    
    switch (seq->seq_type) {
        case CLJ_LIST: {
            ListIteratorState *state = (ListIteratorState*)seq->state;
            return state->count;
        }
        
        case CLJ_VECTOR: {
            VectorIteratorState *state = (VectorIteratorState*)seq->state;
            return state->count;
        }
        
        case CLJ_MAP: {
            MapIteratorState *state = (MapIteratorState*)seq->state;
            return state->count;
        }
        
        case CLJ_STRING: {
            StringIteratorState *state = (StringIteratorState*)seq->state;
            return state->length;
        }
        
        case CLJ_NIL:
        default:
            return 0;
    }
}

// ============================================================================
// PREDICATES
// ============================================================================

bool is_seqable(CljObject *obj) {
    if (!obj) return true; // nil is seqable
    
    switch (obj->type) {
        case CLJ_LIST:
        case CLJ_VECTOR:
        case CLJ_MAP:
        case CLJ_STRING:
        case CLJ_NIL:
            return true;
        default:
            return false;
    }
}

bool is_seq(CljObject *obj) {
    // In our implementation, sequences are represented by SeqIterator objects
    // This function checks if an object is already a sequence
    return type(obj) == CLJ_LIST; // Lists are the primary sequence type
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Direct seq operations - these work on CljObject directly without iterators
// The key insight: lists and sequences are the same thing

// ============================================================================
// EQUALITY
// ============================================================================

bool seq_equal(CljObject *seq1_obj, CljObject *seq2_obj) {
    if (!seq1_obj && !seq2_obj) return true;
    if (!seq1_obj || !seq2_obj) return false;
    
    // Compare element by element
    CljObject *current1 = seq1_obj;
    CljObject *current2 = seq2_obj;
    
    while (!seq_empty(current1) && !seq_empty(current2)) {
        CljObject *first1 = seq_first(current1);
        CljObject *first2 = seq_first(current2);
        
        // Simple equality check (could be enhanced with proper equality)
        if (first1 != first2) {
            seq_release(current1);
            seq_release(current2);
            return false;
        }
        
        CljObject *next1 = seq_next(current1);
        CljObject *next2 = seq_next(current2);
        
        seq_release(current1);
        seq_release(current2);
        
        current1 = next1;
        current2 = next2;
    }
    
    bool equal = seq_empty(current1) && seq_empty(current2);
    
    if (current1) seq_release(current1);
    if (current2) seq_release(current2);
    
    return equal;
}

bool seqable_equal(CljObject *obj1, CljObject *obj2) {
    CljObject *seq1 = seq_create(obj1);
    CljObject *seq2 = seq_create(obj2);
    
    bool equal = seq_equal(seq1, seq2);
    
    if (seq1) seq_release(seq1);
    if (seq2) seq_release(seq2);
    
    return equal;
}
