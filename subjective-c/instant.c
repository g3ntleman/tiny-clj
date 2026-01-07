#include "instant.h"


#include "datetime_utc.h"

#include "memory.h"

ID make_instant(int32_t days, uint32_t ms) {
    CljInstant *inst = ALLOC(CljInstant, 1);
    inst->base.type = CLJ_INSTANT;
    inst->base.flags = 0;
    inst->base.rc = 1;
    inst->days = days;
    inst->ms = ms;
    return (ID)inst;
}

int32_t clj_instant_days(ID v) {
    CljInstant *inst = as_instant(v);
    return inst ? inst->days : 0;
}

uint32_t clj_instant_ms(ID v) {
    CljInstant *inst = as_instant(v);
    return inst ? inst->ms : 0;
}

ID make_instant_from_iso8601_utc_string(const char *iso, const char **err_out)
{
    if (err_out)
        *err_out = NULL;

    if (!iso)
    {
        if (err_out)
            *err_out = "ISO-8601 string is NULL";
        return NULL;
    }

    int32_t days = 0;
    uint32_t ms = 0;
    const char *err = tinyclj_parse_iso8601_utc_instant(iso, &days, &ms);
    if (err)
    {
        if (err_out)
            *err_out = err;
        return NULL;
    }

    ID inst = make_instant(days, ms);
    if (!inst)
    {
        if (err_out)
            *err_out = "Failed to allocate Instant";
        return NULL;
    }

    return inst;
}
