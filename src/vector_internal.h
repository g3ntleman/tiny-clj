#ifndef TINY_CLJ_VECTOR_INTERNAL_H
#define TINY_CLJ_VECTOR_INTERNAL_H

// Internal header for vector structure definition
// Only to be used by event_loop.c and memory.c for direct manipulation
// of transient vectors used as timer queues and autorelease pools

#include "vector.h"

// CljPersistentVector struct definition (internal use only)
struct CljPersistentVector {
    CljObject base;
    int count;
    int capacity;
    CljObject **data;
};

#endif /* TINY_CLJ_VECTOR_INTERNAL_H */

