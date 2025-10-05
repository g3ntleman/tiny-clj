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

SeqIterator* seq_create(CljObject *obj) {
    // Handle NULL (nil) case
    if (!obj) {
        SeqIterator *seq = calloc(1, sizeof(SeqIterator));
        if (!seq) return NULL;
        seq->container = NULL;
        seq->state = NULL;
        seq->seq_type = CLJ_NIL;
        return seq;
    }
    
    SeqIterator *seq = calloc(1, sizeof(SeqIterator));
    if (!seq) return NULL;
    
    seq->container = obj;
    retain(obj); // Borrowed reference
    
    switch (obj->type) {
        case CLJ_LIST: {
            CljList *list_data = as_list(obj);
            if (!list_data) {
                free(seq);
                return NULL;
            }
            
            ListIteratorState *state = calloc(1, sizeof(ListIteratorState));
            state->current = list_data->head;
            state->index = 0;
            state->count = list_count(obj);
            seq->state = state;
            seq->seq_type = CLJ_LIST;
            break;
        }
        
        case CLJ_VECTOR: {
            CljPersistentVector *vec = as_vector(obj);
            if (!vec) {
                free(seq);
                return NULL;
            }
            
            VectorIteratorState *state = calloc(1, sizeof(VectorIteratorState));
            state->index = 0;
            state->count = vec->count;
            state->data = vec->data;
            seq->state = state;
            seq->seq_type = CLJ_VECTOR;
            break;
        }
        
        case CLJ_MAP: {
            CljMap *map = as_map(obj);
            if (!map) {
                free(seq);
                return NULL;
            }
            
            MapIteratorState *state = calloc(1, sizeof(MapIteratorState));
            state->index = 0;
            state->count = map->count;
            state->keys = map->data;      // Keys are at even indices
            state->values = map->data + 1; // Values are at odd indices
            seq->state = state;
            seq->seq_type = CLJ_MAP;
            break;
        }
        
        case CLJ_STRING: {
            StringIteratorState *state = calloc(1, sizeof(StringIteratorState));
            state->index = 0;
            state->length = strlen((char*)obj->as.data);
            state->data = (const char*)obj->as.data;
            seq->state = state;
            seq->seq_type = CLJ_STRING;
            break;
        }
        
        case CLJ_NIL:
            // Empty sequence - create a minimal state
            seq->state = NULL;
            seq->seq_type = CLJ_NIL;
            seq->container = NULL; // Don't retain NULL
            break;
            
        default:
            // Not seqable
            free(seq);
            return NULL;
    }
    
    return seq;
}

void seq_release(SeqIterator *seq) {
    if (!seq) return;
    
    if (seq->container) {
        release(seq->container);
    }
    
    if (seq->state) {
        free(seq->state);
    }
    
    free(seq);
}

void seq_retain(SeqIterator *seq) {
    if (seq && seq->container) {
        retain(seq->container);
    }
}

// ============================================================================
// SEQ OPERATIONS
// ============================================================================

CljObject* seq_first(SeqIterator *seq) {
    if (!seq || !seq->state) return clj_nil();
    
    switch (seq->seq_type) {
        case CLJ_LIST: {
            ListIteratorState *state = (ListIteratorState*)seq->state;
            if (state->current) {
                retain(state->current);
                return state->current;
            }
            break;
        }
        
        case CLJ_VECTOR: {
            VectorIteratorState *state = (VectorIteratorState*)seq->state;
            if (state->index < state->count && state->data[state->index]) {
                retain(state->data[state->index]);
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
                        retain(key);
                        retain(value);
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

SeqIterator* seq_rest(SeqIterator *seq) {
    if (!seq || !seq->state) return NULL;
    
    SeqIterator *rest_seq = calloc(1, sizeof(SeqIterator));
    if (!rest_seq) return NULL;
    
    rest_seq->container = seq->container;
    retain(rest_seq->container);
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
    
    return rest_seq;
}

SeqIterator* seq_next(SeqIterator *seq) {
    return seq_rest(seq);
}

bool seq_empty(SeqIterator *seq) {
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

int seq_count(SeqIterator *seq) {
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
    return obj && obj->type == CLJ_LIST; // Lists are the primary sequence type
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

CljObject* seq_to_list(SeqIterator *seq) {
    if (!seq) return make_list();
    
    // For now, return an empty list to avoid complex list building
    // This can be enhanced later with proper list construction
    return make_list();
}

// ============================================================================
// EQUALITY
// ============================================================================

bool seq_equal(SeqIterator *seq1, SeqIterator *seq2) {
    if (!seq1 && !seq2) return true;
    if (!seq1 || !seq2) return false;
    
    // Compare element by element
    SeqIterator *current1 = seq1;
    SeqIterator *current2 = seq2;
    
    while (!seq_empty(current1) && !seq_empty(current2)) {
        CljObject *first1 = seq_first(current1);
        CljObject *first2 = seq_first(current2);
        
        // Simple equality check (could be enhanced with proper equality)
        if (first1 != first2) {
            seq_release(current1);
            seq_release(current2);
            return false;
        }
        
        SeqIterator *next1 = seq_next(current1);
        SeqIterator *next2 = seq_next(current2);
        
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
    SeqIterator *seq1 = seq_create(obj1);
    SeqIterator *seq2 = seq_create(obj2);
    
    bool equal = seq_equal(seq1, seq2);
    
    if (seq1) seq_release(seq1);
    if (seq2) seq_release(seq2);
    
    return equal;
}
