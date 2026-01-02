#include "instant.h"


#include "memory.h"

ID clj_make_instant(int32_t days, uint32_t ms) {
    CljInstant *inst = (CljInstant*)alloc(sizeof(CljInstant), 1, CLJ_INSTANT);
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
