#include "source_resolver.h"

#include "fs_layer.h"
#include "embedded_sources.h"
#include "byte_array.h"
#include "value.h"
#include "memory.h"

ID resolve_path_to_bytes(const char *path) {
    if (!path || !path[0]) return NULL;

    // Consult the KV store first (create/open on demand).
    FsKvStore *st = fs_global_store();
    if (st) {
        ID kv_bytes = fs_read_bytes(st, path);
        if (kv_bytes && TAG(kv_bytes) == CLJ_BYTE_ARRAY) return kv_bytes;
    }

    const uint8_t *embedded_data = NULL;
    int embedded_len = 0;
    if (embedded_source_lookup(path, &embedded_data, &embedded_len) && embedded_data && embedded_len >= 0) {
        CljByteArray *view = make_byte_array_view((uint8_t *)embedded_data, embedded_len);
        if (!view) return NULL;
        return AUTORELEASE((ID)view);
    }

    return NULL;
}
