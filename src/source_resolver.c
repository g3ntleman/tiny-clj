#include "source_resolver.h"

#include "fs_layer.h"
#include "embedded_sources.h"
#include "byte_array.h"
#include "value.h"
#include "memory.h"

/**
 * @brief Resolves a virtual path to byte content from KV store or embedded sources.
 *
 * Lookup order is KV store first (to allow overrides), then the compiled-in
 * embedded source table. The resolver must not force KV backend initialization:
 * if the global KV store is not initialized yet, embedded sources are used
 * directly. Returns a caller-usable byte-array object on success.
 *
 * @param path Virtual path to resolve.
 * @return Byte-array object (pool-managed) or NULL when not found.
 */
ID resolve_path_to_bytes(const char *path) {
    if (!path || !path[0]) return NULL;

    // Consult KV overrides only when the store is already initialized.
    // This keeps plain require() from creating host-side tiny-db files.
    FsKvStore *st = fs_global_store_if_initialized();
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
