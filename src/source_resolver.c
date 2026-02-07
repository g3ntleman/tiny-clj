#include "source_resolver.h"

#include "fs_layer.h"
#include "runtime.h"
#include "map.h"
#include "strings.h"
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

    CljPersistentMap *emb = g_runtime.embedded_source_map;
    if (emb) {
        ID key = (ID)make_string(path);
        if (!key) return NULL;
        ID val = map_get(emb, key);
        RELEASE(key);
        if (val != NOT_FOUND && val != NULL && TAG(val) == CLJ_BYTE_ARRAY) {
            return AUTORELEASE(RETAIN(val));
        }
    }

    return NULL;
}
