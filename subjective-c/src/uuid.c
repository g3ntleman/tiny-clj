#include "uuid.h"

#include "memory.h"

#include <string.h>

static inline int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

ID clj_uuid_from_bytes(const uint8_t bytes[16]) {
    CljUUID *u = ALLOC(CljUUID, 1);
    u->base.type = CLJ_UUID;
    u->base.flags = 0;
    u
    memcpy(u->bytes, bytes, 16);
    u->hash = 0;
    return (ID)u;
}

ID clj_uuid_from_string(const char *s) {
    if (!s) return NULL;
    // Expected: 8-4-4-4-12 = 36 chars
    if (strlen(s) != 36) return NULL;
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') return NULL;

    uint8_t bytes[16];
    int bi = 0;
    for (int i = 0; i < 36; ) {
        if (s[i] == '-') {
            i++;
            continue;
        }
        int hi = hex_val(s[i]);
        int lo = hex_val(s[i + 1]);
        if (hi < 0 || lo < 0) return NULL;
        bytes[bi++] = (uint8_t)((hi << 4) | lo);
        i += 2;
    }
    if (bi != 16) return NULL;
    return clj_uuid_from_bytes(bytes);
}

void clj_uuid_to_cstring(ID v, char out[37]) {
    static const char *hex = "0123456789abcdef";
    CljUUID *u = as_uuid(v);
    if (!u) {
        out[0] = '\0';
        return;
    }

    int oi = 0;
    for (int bi = 0; bi < 16; bi++) {
        // Insert dashes at 4, 6, 8, 10 bytes boundaries
        if (bi == 4 || bi == 6 || bi == 8 || bi == 10) {
            out[oi++] = '-';
        }
        uint8_t b = u->bytes[bi];
        out[oi++] = hex[(b >> 4) & 0xF];
        out[oi++] = hex[b & 0xF];
    }
    out[oi] = '\0';
}
