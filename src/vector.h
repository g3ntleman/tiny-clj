#ifndef TINY_CLJ_VECTOR_H
#define TINY_CLJ_VECTOR_H

#include "object.h"
#include "value.h"

// === Legacy API (deprecated - use CljValue API) ===
/** @deprecated Use make_vector_v() instead. Create a vector with given capacity; capacity<=0 returns empty-vector singleton. */
CljObject* make_vector(int capacity, int is_mutable);
/** @deprecated Use vector_conj_v() instead. Return a new vector with item appended; original vector remains unchanged. */
CljObject* vector_conj(CljObject *vec, CljObject *item);
/** @deprecated Use conj_v() for transient vectors instead. Append into mutable or weak vector (in-place when possible). */
int vector_push_inplace(CljObject *vec, CljObject *item);
/** @deprecated Use make_vector_v() instead. Weak vector (no retain on push). */
CljObject* make_weak_vector(int capacity);
/** @deprecated Use make_vector_v() instead. Create a vector from an array of items (retains non-NULL items). */
CljObject* vector_from_items(CljObject **items, int count);
/** @deprecated Use make_vector_v() instead. */
CljObject* make_vector_from_stack(CljObject **stack, int count);

// === Neue CljValue API (Phase 1: Parallel) ===
/** Create a vector with given capacity; capacity<=0 returns empty-vector singleton. */
CljValue make_vector_v(int capacity, int is_mutable);
/** Return a new vector with item appended; original vector remains unchanged. */
CljValue vector_conj_v(CljValue vec, CljValue item);

// === Transient API (Phase 2) ===
/** Convert persistent vector to transient. */
CljValue transient(CljValue vec);
/** Append to transient vector (guaranteed in-place). */
CljValue conj_v(CljValue tvec, CljValue item);
/** Convert transient vector back to persistent. */
CljValue persistent_v(CljValue tvec);

#endif

