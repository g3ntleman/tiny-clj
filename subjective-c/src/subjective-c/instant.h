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

/** @brief Create instant from days and milliseconds
 * @param days Days since epoch (1970-01-01)
 * @param ms Milliseconds within day (0-86399999)
 * @return New instant object
 */
ID make_instant(int32_t days, uint32_t ms);

/** @brief Get days component of instant
 * @param v Instant object
 * @return Days since epoch
 */
int32_t clj_instant_days(ID v);

/** @brief Get milliseconds component of instant
 * @param v Instant object
 * @return Milliseconds within day
 */
uint32_t clj_instant_ms(ID v);

/** @brief Parse ISO-8601 UTC instant string
 * @param iso ISO-8601 string (e.g. "1970-01-01T00:00:00.000Z")
 * @param err_out Error message output (can be NULL)
 * @return New instant or NULL on error
 */
ID make_instant_from_iso8601_utc_string(const char *iso, const char **err_out);

#endif
