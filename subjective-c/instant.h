#ifndef SUBJECTIVE_C_INSTANT_H
#define SUBJECTIVE_C_INSTANT_H

#include "object.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct CljInstant {
    CljObject base;
    int32_t days;
    uint32_t ms;
} CljInstant;

static inline bool clj_is_instant(ID v) {
    return TAG(v) == CLJ_INSTANT;
}

static inline CljInstant* as_instant(ID v) {
    return (CljInstant*)assert_type((CljObject*)v, CLJ_INSTANT);
}

ID clj_make_instant(int32_t days, uint32_t ms);
int32_t clj_instant_days(ID v);
uint32_t clj_instant_ms(ID v);

#endif
