#include "vector.h"
#include "memory.h"
#include "value.h"  // For IS_IMMEDIATE macro used in memory.h
#include <stdlib.h>
#include <stdbool.h>

// Empty-vector singleton: CLJ_VECTOR with rc=0, statically initialized
static struct {
    CljPersistentVector vec;
} clj_empty_vector_singleton_data = {
    .vec = {
        .base = { .type = CLJ_VECTOR, .rc = 0 },
        .count = 0,
        .capacity = 0,
        .data = NULL
    }
};
static CljPersistentVector *clj_empty_vector_singleton = &clj_empty_vector_singleton_data.vec;

/** Return empty vector singleton (rc=0, do not retain/release). */
CljValue empty_vector(void) {
    return (CljValue)clj_empty_vector_singleton;
}

// Creates a CljVector.
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
CljValue make_vector(unsigned int capacity, bool is_mutable) {
    if (capacity == 0 && !is_mutable) {
        return (CljValue)clj_empty_vector_singleton;
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

    return (CljValue)vec;
}

/** Return a new vector with item appended; original vector remains unchanged.
 * Uses Copy-on-Write: RC=1 → in-place mutation, RC>1 → COW.
 * Hot-path (RC=1, capacity OK): No branches, direct in-place mutation.
 */
CljVector vector_conj(CljVector vec, ID item) {
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
        CljValue new_vec_obj = make_vector(4, false);
        CljPersistentVector *new_vec = as_vector((ID)new_vec_obj);
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

    CljValue new_vec_obj = make_vector(new_capacity, false);
    CljPersistentVector *new_vec = as_vector((ID)new_vec_obj);
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
CljVector vector_assoc(CljVector vec, int index, ID value) {
    if (!vec || vec->base.type != CLJ_VECTOR || !value)
        return NULL;

    CljPersistentVector *old_vec = as_vector((ID)vec);
    if (!old_vec || index < 0 || index >= old_vec->count)
        return NULL;

    // Empty vector singleton (RC=0): Not applicable (index >= count)
    if (old_vec->base.rc == 0) {
        return NULL;
    }

    // OPTIMIZATION: If RC=1, mutate in-place
    if (old_vec->base.rc == 1) {
        RELEASE(old_vec->data[index]);
        old_vec->data[index] = RETAIN(value);
        return vec;  // Return same vector (in-place mutation)
    }

    // RC>1: Copy-on-Write
    CljValue new_vec_obj = make_vector(old_vec->capacity, false);
    CljPersistentVector *new_vec = as_vector((ID)new_vec_obj);
    if (!new_vec)
        return vec;  // Return original vector on OOM

    // Copy existing elements with RETAIN
    for (int i = 0; i < old_vec->count; i++) {
        if (old_vec->data[i]) {
            if (i == index) {
                new_vec->data[i] = RETAIN(value);  // New value
            } else {
                new_vec->data[i] = old_vec->data[i];
                RETAIN(old_vec->data[i]);
            }
        }
    }
    new_vec->count = old_vec->count;

    return new_vec;  // Return new vector (COW)
}

// === Transient API (Phase 2) ===

/** Convert persistent vector to transient. */
CljValue transient(CljValue vec) {
    if (!vec || vec->type != CLJ_VECTOR) {
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
    
    return (CljValue)tvec;
}

/** Grow vector capacity in-place (for RC=1 or transient vectors).
 * @param v Vector to grow
 * @note Throws exception on OOM
 */
void vector_grow_capacity(CljPersistentVector *v) {
    if (!v) {
        throw_oom(CLJ_VECTOR);
        return;
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
CljValue clj_conj(CljValue tvec, CljValue item) {
    if (!tvec || tvec->type != CLJ_TRANSIENT_VECTOR || !item) {
        return NULL;
    }
    
    CljPersistentVector *v = as_vector(tvec);
    if (!v) return NULL;
    
    // Garantiert in-place für Transients
    if (v->count >= v->capacity) {
        vector_grow_capacity(v);
    }
    
    v->data[v->count++] = RETAIN(item);
    
    return tvec; // In-place mutation
}

/** Convert transient vector back to persistent. */
CljValue persistent(CljValue tvec) {
    if (!tvec || tvec->type != CLJ_TRANSIENT_VECTOR) {
        return NULL;
    }
    
    CljPersistentVector *v = as_vector(tvec);
    if (!v) return NULL;
    
    // Clojure-Semantik: Erstelle NEUE persistent collection
    CljValue new_vec = make_vector(v->capacity, 0);  // Neue Instanz
    CljPersistentVector *new_v = as_vector(new_vec);
    
    // Kopiere alle Elemente
    for (int i = 0; i < v->count; i++) {
        if (v->data[i]) {
            new_v->data[i] = v->data[i];
            RETAIN(v->data[i]);
        }
    }
    new_v->count = v->count;
    
    // Original transient wird "invalidated" (kann später implementiert werden)
    // v->base.type = CLJ_INVALID;  // TODO: Invalidierung implementieren
    
    return new_vec; // NEUE persistent collection
}
