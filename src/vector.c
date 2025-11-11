#include "vector.h"
#include "memory.h"
#include "value.h"  // For IS_IMMEDIATE macro used in memory.h
#include "types.h"  // For SINGLETON_RC
#include "symbol.h"  // For SYM_NIL
#include "seq.h"  // For SeqIterator
#include <stdlib.h>
#include <stdbool.h>

// CljPersistentVector struct definition (opaque pointer - only visible in vector.c)
struct CljPersistentVector {
    CljObject base;
    int count;
    int capacity;
    // mutable_flag removed: COW (RC-based) handles mutability automatically
    CljObject **data;
};

// Empty-vector singleton: CLJ_VECTOR with rc=SINGLETON_RC, statically initialized
static struct {
    CljPersistentVector vec;
} clj_empty_vector_singleton_data = {
    .vec = {
        .base = { .type = CLJ_VECTOR, .rc = SINGLETON_RC },
        .count = 0,
        .capacity = 0,
        .data = NULL
    }
};
static CljPersistentVector *clj_empty_vector_singleton = &clj_empty_vector_singleton_data.vec;
// Export as external symbol (similar to empty_string_singleton pattern)
CljPersistentVector* empty_vector_singleton = (CljPersistentVector*)&clj_empty_vector_singleton_data.vec;

/** Return empty vector singleton (rc=0, do not retain/release). */
ID empty_vector(void) {
    return (ID)clj_empty_vector_singleton;
}

/** Get vector count. Returns 0 if vec is NULL. */
int vector_count(CljPersistentVector *vec) {
    if (!vec) return 0;
    return vec->count;
}

/** Get element at index. Returns retained element or NULL if index out of bounds or nil.
 * @param vec Vector to access
 * @param index Index (0-based)
 * @return Retained element or NULL (nil)
 */
ID vector_nth(CljPersistentVector *vec, int index) {
    if (!vec || index < 0 || index >= vec->count) return NULL;
    ID result = vec->data[index];
    if (!result || result == SYM_NIL) return NULL;
    return RETAIN(result);
}

/** Initialize seq iterator for vector (internal use by seq.c).
 * Sets up iterator state without exposing internal data pointer.
 * @param iter Iterator to initialize
 * @param vec Vector to iterate over
 * @return true if successful, false if vector is empty
 */
bool vector_init_seq_iterator(SeqIterator *iter, CljPersistentVector *vec) {
    if (!iter || !vec) return false;
    
    // Check if empty
    if (vec->count == 0 || vec == empty_vector_singleton || 
        (vec->count == 0 && is_singleton((CljObject*)vec))) {
        return false;  // Empty vector
    }
    
    // Store vector in container (already set by seq_iter_init)
    // Set index and count, but NOT data pointer
    iter->state.vec.index = 0;
    iter->state.vec.count = vec->count;
    iter->state.vec.data = NULL;  // Don't expose internal pointer
    iter->seq_type = CLJ_VECTOR;
    
    return true;
}

/** Get element at index without RETAIN (internal use for seq iterator).
 * @param vec Vector to access
 * @param index Index (0-based)
 * @return Element or NULL (nil), NOT retained
 */
ID vector_get_element_no_retain(CljPersistentVector *vec, int index) {
    if (!vec || index < 0 || index >= vec->count) return NULL;
    ID result = vec->data[index];
    if (!result || result == SYM_NIL) return NULL;
    return result;  // No RETAIN - caller must handle reference counting
}


/** Set element at index. Returns new vector with updated element (COW if needed).
 * @param vec Vector to update
 * @param index Index (0-based)
 * @param value New value (will be retained)
 * @return New vector with updated element, or NULL on error
 */
CljPersistentVector* vector_set_nth(CljPersistentVector* vec, int index, ID value) {
    if (!vec || index < 0) return NULL;
    CljPersistentVector *v = as_vector((ID)vec);
    if (!v || index >= v->count) return NULL;
    return vector_assoc(vec, index, value);
}

// Creates a CljPersistentVector*.
// Notes:
// - When capacity <= 0, returns empty-vector singleton (rc=0, data=NULL); do
// not retain/release.
// - When capacity > 0, returns heap vector (rc=1) with zero-initialized backing
// store.
// Legacy make_vector removed - use make_vector instead

// Legacy make_weak_vector removed - use make_vector instead

// Legacy vector_push_inplace removed - use conj for transient vectors instead

// Legacy vector_conj removed - use vector_conj instead

// Legacy vector_from_items removed - use make_vector instead

// Legacy make_vector_from_stack removed - use make_vector instead

// === Neue CljValue API (Phase 1: Parallel) ===

/** Create a vector with given capacity; capacity<=0 returns empty-vector singleton. */
CljPersistentVector* make_vector(unsigned int capacity, bool is_mutable) {
    if (capacity == 0 && !is_mutable) {
        return clj_empty_vector_singleton;
    }
    CljPersistentVector *vec = ALLOC(CljPersistentVector, 1);
    if (!vec)
        throw_oom(CLJ_VECTOR);

    vec->base.type = CLJ_VECTOR;
    vec->base.rc = 1;
    vec->count = 0;
    vec->capacity = capacity;
    // mutable_flag removed: COW (RC-based) handles mutability automatically
    if (capacity > 0) {
        vec->data = (CljObject **)calloc((size_t)capacity, sizeof(CljObject *));
        if (!vec->data) {
            free(vec);
            throw_oom(CLJ_VECTOR);
        }
    } else {
        vec->data = NULL;
    }

    return vec;
}

/** Return a new vector with item appended; original vector remains unchanged.
 * Uses Copy-on-Write: RC=1 → in-place mutation, RC>1 → COW.
 * Hot-path (RC=1, capacity OK): No branches, direct in-place mutation.
 */
CljPersistentVector* vector_conj(CljPersistentVector* vec, ID item) {
    if (!vec || vec->base.type != CLJ_VECTOR)
        return NULL;
    // Note: item can be NULL (nil) - it's a valid value in Clojure collections

    CljPersistentVector *old_vec = as_vector((ID)vec);
    if (!old_vec)
        return NULL;

    // HOT-PATH: RC=1 && capacity OK → direct in-place mutation (no branches)
    // Most common case: single owner, enough capacity
    if (old_vec->base.rc == 1 && old_vec->count < old_vec->capacity) {
        // NULL (nil) is a valid value - store directly without RETAIN
        old_vec->data[old_vec->count++] = item ? RETAIN(item) : NULL;
        return vec;  // Return same vector (in-place mutation)
    }

    // Early returns for uncommon cases
    if (old_vec->base.rc == 0) {
        // Empty vector singleton: create new vector
        CljPersistentVector* new_vec = make_vector(4, false);
        if (!new_vec)
            return vec;
        // NULL (nil) is a valid value - store directly without RETAIN
        new_vec->data[0] = item ? RETAIN(item) : NULL;
        new_vec->count = 1;
        return new_vec;
    }

    // COW path: RC>1 or out of capacity
    int new_capacity = old_vec->capacity;
    if (old_vec->count >= old_vec->capacity) {
        new_capacity = old_vec->capacity * 2;
        if (new_capacity < 4) new_capacity = 4;
    }

    CljPersistentVector* new_vec = make_vector(new_capacity, false);
    if (!new_vec)
        return vec;

    // Copy existing elements with RETAIN
    for (int i = 0; i < old_vec->count; i++) {
        if (old_vec->data[i]) {
            new_vec->data[i] = old_vec->data[i];
            RETAIN(old_vec->data[i]);
        } else {
            new_vec->data[i] = NULL;  // nil elements
        }
    }
    new_vec->count = old_vec->count;

    // Append new item (NULL/nil is a valid value)
    new_vec->data[new_vec->count++] = item ? RETAIN(item) : NULL;

    return new_vec;  // Return new vector (COW)
}

/** Update element at index with COW: RC=1 → in-place, RC>1 → COW. */
CljPersistentVector* vector_assoc(CljPersistentVector* vec, int index, ID value) {
    if (!vec || vec->base.type != CLJ_VECTOR || !value)
        return NULL;

    CljPersistentVector *old_vec = as_vector((ID)vec);
    if (!old_vec || index < 0 || index >= vector_count(old_vec))
        return NULL;

    // Empty vector singleton (RC=0): Not applicable (index >= count)
    if (old_vec->base.rc == 0) {
        return NULL;
    }

    // OPTIMIZATION: If RC=1, mutate in-place
    if (old_vec->base.rc == 1) {
        ASSIGN(old_vec->data[index], RETAIN(value));
        return vec;  // Return same vector (in-place mutation)
    }

    // RC>1: Copy-on-Write
    int count = vector_count(old_vec);
    CljPersistentVector* new_vec = make_vector(old_vec->capacity, false);
    if (!new_vec)
        return vec;  // Return original vector on OOM

    // Copy existing elements with RETAIN
    for (int i = 0; i < count; i++) {
        if (old_vec->data[i]) {
            if (i == index) {
                new_vec->data[i] = RETAIN(value);  // New value
            } else {
                new_vec->data[i] = old_vec->data[i];
                RETAIN(old_vec->data[i]);
            }
        } else {
            new_vec->data[i] = NULL;
        }
    }
    new_vec->count = count;

    return new_vec;  // Return new vector (COW)
}

// === Transient API (Phase 2) ===

/** Convert persistent vector to transient. */
ID transient(ID vec) {
    if (!vec) return NULL;
    CljObject *obj = (CljObject*)vec;
    if (obj->type != CLJ_VECTOR) {
        return NULL;
    }
    
    CljPersistentVector *v = as_vector(vec);
    if (!v) return NULL;
    
    // Erstelle Kopie mit transient type
    CljPersistentVector *tvec = ALLOC(CljPersistentVector, 1);
    if (!tvec) return NULL;
    
    tvec->base.type = CLJ_TRANSIENT_VECTOR;
    tvec->base.rc = 1;
    tvec->count = v->count;
    tvec->capacity = v->capacity;
    // mutable_flag removed: Transients identified by CLJ_TRANSIENT_VECTOR type
    
    // Kopiere data array
    if (v->capacity > 0) {
        tvec->data = (CljObject**)calloc((size_t)v->capacity, sizeof(CljObject*));
        if (!tvec->data) {
            free(tvec);
            return NULL;
        }
        for (int i = 0; i < v->count; i++) {
            if (v->data[i]) {
                tvec->data[i] = v->data[i];
                RETAIN(v->data[i]);
            }
        }
    } else {
        tvec->data = NULL;
    }
    
    return tvec;
}

/** Grow vector capacity in-place (for RC=1 or transient vectors).
 * Only grows if count >= capacity. Safe to call even if growth is not needed.
 * @param v Vector to grow
 * @note Throws exception on OOM
 */
void vector_grow_capacity(CljPersistentVector *v) {
    if (!v) {
        throw_oom(CLJ_VECTOR);
        return;
    }
    
    // Only grow if capacity is insufficient
    if (v->count < v->capacity) {
        return;  // No growth needed
    }
    
    int newcap = v->capacity ? v->capacity * 2 : 4;
    void *p = realloc(v->data, (size_t)newcap * sizeof(CljObject *));
    if (!p) {
        throw_oom(CLJ_VECTOR);
        return;
    }
    
    v->data = (CljObject **)p;
    // Initialize new slots to NULL
    for (int i = v->capacity; i < newcap; ++i)
        v->data[i] = NULL;
    v->capacity = newcap;
}

/** Append to transient vector (guaranteed in-place). */
ID clj_conj(ID tvec, ID item) {
    if (!tvec) return NULL;
    // Note: item can be NULL (nil) - it's a valid value in Clojure collections
    CljObject *obj = (CljObject*)tvec;
    if (obj->type != CLJ_TRANSIENT_VECTOR) {
        return NULL;
    }
    
    CljPersistentVector *v = as_vector(tvec);
    if (!v) return NULL;
    
    // Garantiert in-place für Transients
    if (v->count >= v->capacity) {
        vector_grow_capacity(v);
    }
    
    // NULL (nil) is a valid value - store directly without RETAIN
    v->data[v->count++] = item ? RETAIN(item) : NULL;
    
    return tvec; // In-place mutation
}

/** Convert transient vector back to persistent. */
ID persistent(ID tvec) {
    if (!tvec) return NULL;
    CljObject *obj = (CljObject*)tvec;
    if (obj->type != CLJ_TRANSIENT_VECTOR) {
        return NULL;
    }
    
    CljPersistentVector *v = as_vector(tvec);
    if (!v) return NULL;
    
    // Clojure-Semantik: Erstelle NEUE persistent collection
    CljPersistentVector* new_vec = make_vector(v->capacity, 0);  // Neue Instanz
    
    // Kopiere alle Elemente
    for (int i = 0; i < v->count; i++) {
        if (v->data[i]) {
            new_vec->data[i] = v->data[i];
            RETAIN(v->data[i]);
        }
    }
    new_vec->count = v->count;
    
    // Original transient wird "invalidated" (kann später implementiert werden)
    // v->base.type = CLJ_INVALID;  // TODO: Invalidierung implementieren
    
    return new_vec; // NEUE persistent collection
}
