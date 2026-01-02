#ifndef SUBJECTIVE_C_UUID_H
#define SUBJECTIVE_C_UUID_H

#include "object.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct CljUUID {
    CljObject base;
    uint8_t bytes[16];
    uint32_t hash;
} CljUUID;

// UUIDs are immutable, so their hash can be computed lazily and cached.
// Convention: hash==0 means "not computed yet" (rarely, a real hash could be 0;
// in that case it will be recomputed on each call, but correctness is preserved).

static inline bool clj_is_uuid(ID v) {
    return TAG(v) == CLJ_UUID;
}

static inline CljUUID* as_uuid(ID v) {
    return (CljUUID*)assert_type((CljObject*)v, CLJ_UUID);
}

ID clj_uuid_from_bytes(const uint8_t bytes[16]);
ID clj_uuid_from_string(const char *s);
void clj_uuid_to_cstring(ID v, char out[37]);

#endif
