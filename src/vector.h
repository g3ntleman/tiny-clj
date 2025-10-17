#ifndef TINY_CLJ_VECTOR_H
#define TINY_CLJ_VECTOR_H

#include "object.h"
#include "value.h"

// === Legacy API removed - use CljValue API instead ===

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

