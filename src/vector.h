#ifndef TINY_CLJ_VECTOR_H
#define TINY_CLJ_VECTOR_H

#include "CljObject.h"

/** Create a vector with given capacity; capacity<=0 returns empty-vector singleton. */
CljObject* make_vector(int capacity, int is_mutable);
/** Return a new vector with item appended; original vector remains unchanged. */
CljObject* vector_conj(CljObject *vec, CljObject *item);
/** Append into mutable or weak vector (in-place when possible). */
int vector_push_inplace(CljObject *vec, CljObject *item);
/** Weak vector (no retain on push). */
CljObject* make_weak_vector(int capacity);

#endif

