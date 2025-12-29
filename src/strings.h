#ifndef TINY_CLJ_STRINGS_H
#define TINY_CLJ_STRINGS_H

#include <subjective-c/strings.h>

static inline bool is_clj_string(CljObject *obj) {
    return is_string(obj);
}

#endif
