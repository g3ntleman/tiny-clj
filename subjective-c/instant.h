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

ID make_instant(int32_t days, uint32_t ms);
int32_t clj_instant_days(ID v);
uint32_t clj_instant_ms(ID v);

// Parse ISO-8601 UTC instant string (e.g. "1970-01-01T00:00:00.000Z") into a new Instant.
// Returns NULL on parse/alloc error and writes a static error message to err_out (if non-NULL).
ID make_instant_from_iso8601_utc_string(const char *iso, const char **err_out);

#endif
