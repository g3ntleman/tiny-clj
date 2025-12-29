#ifndef TINY_CLJ_VECTOR_H
#define TINY_CLJ_VECTOR_H

#include <subjective-c/vector.h>

static inline bool is_clj_vector(CljObject *obj) {
    return is_vector(obj);
}

#endif
