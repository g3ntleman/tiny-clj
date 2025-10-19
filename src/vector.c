#include "vector.h"
#include "memory.h"
#include <stdlib.h>
#include <stdbool.h>

// Empty-vector singleton: actual CljVector instance with rc=0
static CljPersistentVector clj_empty_vector_singleton;
/** @brief Initialize empty vector singleton once */
static void init_empty_vector_singleton_once(void) {
  static bool initialized = false;
  if (initialized)
    return;
  clj_empty_vector_singleton.base.type = CLJ_VECTOR;
  clj_empty_vector_singleton.base.rc =
      0; // Singletons have no reference counting
  clj_empty_vector_singleton.count = 0;
  clj_empty_vector_singleton.capacity = 0;
  clj_empty_vector_singleton.mutable_flag = 0;
  clj_empty_vector_singleton.data = NULL;
    initialized = true;
}

// Creates a CljVector.
// Notes:
// - When capacity <= 0, returns empty-vector singleton (rc=0, data=NULL); do
// not retain/release.
// - When capacity > 0, returns heap vector (rc=1) with zero-initialized backing
// store.
// Legacy make_vector removed - use make_vector_v instead

// Legacy make_weak_vector removed - use make_vector_v instead

// Legacy vector_push_inplace removed - use conj_v for transient vectors instead

// Legacy vector_conj removed - use vector_conj_v instead

// Legacy vector_from_items removed - use make_vector_v instead

// Legacy make_vector_from_stack removed - use make_vector_v instead

// === Neue CljValue API (Phase 1: Parallel) ===

/** Create a vector with given capacity; capacity<=0 returns empty-vector singleton. */
CljValue make_vector_v(int capacity, int is_mutable) {
    if (capacity <= 0) {
        init_empty_vector_singleton_once();
        return (CljValue)&clj_empty_vector_singleton;
    }
    CljPersistentVector *vec = ALLOC(CljPersistentVector, 1);
    if (!vec)
        return NULL;

    vec->base.type = CLJ_VECTOR;
    vec->base.rc = 1;
    vec->count = 0;
    vec->capacity = capacity;
    vec->mutable_flag = is_mutable ? 1 : 0;
    if (capacity > 0) {
        vec->data = (CljObject **)calloc((size_t)capacity, sizeof(CljObject *));
        if (!vec->data) {
            free(vec);
            return NULL;
        }
    } else {
        vec->data = NULL;
    }

    return (CljValue)vec;
}

/** Return a new vector with item appended; original vector remains unchanged. */
CljValue vector_conj_v(CljValue vec, CljValue item) {
    if (!vec || ((CljObject*)vec)->type != CLJ_VECTOR || !item)
        return NULL;

    CljPersistentVector *old_vec = as_vector((CljObject*)vec);
    if (!old_vec)
        return NULL;

    int new_capacity = old_vec->capacity + 1;
    if (new_capacity < 4)
        new_capacity = 4;

    CljValue new_vec_obj = make_vector_v(new_capacity, 0);
    CljPersistentVector *new_vec = as_vector((CljObject*)new_vec_obj);
    if (!new_vec)
        return NULL;

    for (int i = 0; i < old_vec->count; i++) {
        if (old_vec->data[i]) {
            new_vec->data[i] = old_vec->data[i];
            RETAIN(old_vec->data[i]);
        }
    }
    // set count for existing elements (including possible NULL holes)
    new_vec->count = old_vec->count;
    // append the new item
    new_vec->data[new_vec->count++] = (CljObject*)item;
    RETAIN((CljObject*)item);

    return new_vec_obj;
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
    tvec->mutable_flag = 1; // Transients sind immer mutable
    
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

/** Append to transient vector (guaranteed in-place). */
CljValue conj_v(CljValue tvec, CljValue item) {
    if (!tvec || tvec->type != CLJ_TRANSIENT_VECTOR || !item) {
        return NULL;
    }
    
    CljPersistentVector *v = as_vector(tvec);
    if (!v) return NULL;
    
    // Garantiert in-place für Transients
    if (v->count >= v->capacity) {
        int newcap = v->capacity ? v->capacity * 2 : 4;
        void *p = realloc(v->data, (size_t)newcap * sizeof(CljObject *));
        if (!p) return NULL;
        v->data = (CljObject **)p;
        for (int i = v->capacity; i < newcap; ++i)
            v->data[i] = NULL;
        v->capacity = newcap;
    }
    
    v->data[v->count++] = item;
    RETAIN(item);
    
    return tvec; // In-place mutation
}

/** Convert transient vector back to persistent. */
CljValue persistent_v(CljValue tvec) {
    if (!tvec || tvec->type != CLJ_TRANSIENT_VECTOR) {
        return NULL;
    }
    
    CljPersistentVector *v = as_vector(tvec);
    if (!v) return NULL;
    
    // Clojure-Semantik: Erstelle NEUE persistent collection
    CljValue new_vec = make_vector_v(v->capacity, 0);  // Neue Instanz
    CljPersistentVector *new_v = as_vector(new_vec);
    if (!new_v) return NULL;
    
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
