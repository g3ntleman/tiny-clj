#include "source_resolver.h"

#include "fs_layer.h"
#include "embedded_sources.h"
#include "byte_array.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

static ID source_resolver_read_file_bytes(const char *fs_path) {
    if (!fs_path || !fs_path[0]) return NULL;

    FILE *fp = fopen(fs_path, "rb");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long file_size = ftell(fp);
    if (file_size < 0 || file_size > INT_MAX) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = (char *)CLJ_MALLOC((size_t)file_size + 1u);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t bytes_read = fread(buf, 1, (size_t)file_size, fp);
    fclose(fp);
    if (bytes_read != (size_t)file_size) {
        CLJ_FREE(buf);
        return NULL;
    }

    ID bytes = make_byte_array_from_bytes((const uint8_t *)buf, (int)bytes_read);
    CLJ_FREE(buf);
    if (!bytes || TAG(bytes) != CLJ_BYTE_ARRAY) return NULL;
    return AUTORELEASE(bytes);
}

static ID source_resolver_try_filesystem(const char *path) {
    if (!path || !path[0]) return NULL;
    size_t path_len = strlen(path);
    bool is_libs_path = strncmp(path, "/libs/", 6) == 0;
    bool is_clj_path = path_len >= 4u && strcmp(path + path_len - 4u, ".clj") == 0;

    // Keep filesystem fallback scoped to source/module resolution.
    // This avoids changing slurp() behavior for arbitrary text files.
    if (!is_libs_path && !is_clj_path) return NULL;

    // 1) Allow direct absolute/relative paths first.
    ID bytes = source_resolver_read_file_bytes(path);
    if (bytes) return bytes;

    const char *rel = path[0] == '/' ? path + 1 : path;

    // 2) Common fallback for running from project root.
    bytes = source_resolver_read_file_bytes(rel);
    if (bytes) return bytes;

#if !defined(ESP32_BUILD)
    // 2) Try repository-root mapping derived from this source file path.
    //    Example: /libs/tiny-gfx/converter.clj -> <repo>/libs/tiny-gfx/converter.clj
    const char *marker_abs = "/src/source_resolver.c";
    const char *marker_rel = "src/source_resolver.c";
    const char *pos = strstr(__FILE__, marker_abs);
    if (!pos) pos = strstr(__FILE__, marker_rel);
    if (pos) {
        size_t repo_len = (size_t)(pos - __FILE__);
        size_t rel_len = strlen(rel);
        if (repo_len > 0u && repo_len + 1u + rel_len + 1u < 1024u) {
            char repo_path[1024];
            memcpy(repo_path, __FILE__, repo_len);
            repo_path[repo_len] = '/';
            memcpy(repo_path + repo_len + 1u, rel, rel_len);
            repo_path[repo_len + 1u + rel_len] = '\0';
            bytes = source_resolver_read_file_bytes(repo_path);
            if (bytes) return bytes;
        }
    }

    // 3) Host fallback for running from build/ or similar.
    if (strlen(rel) + 4u < 1024u) {
        char parent_path[1024];
        parent_path[0] = '.';
        parent_path[1] = '.';
        parent_path[2] = '/';
        strcpy(parent_path + 3, rel);
        bytes = source_resolver_read_file_bytes(parent_path);
        if (bytes) return bytes;
    }
#endif

    return NULL;
}

/**
 * @brief Resolves a virtual path to byte content from KV store or embedded sources.
 *
 * Lookup order is KV store first (to allow overrides), then the compiled-in
 * embedded source table. A final filesystem fallback is allowed for local dev
 * and host tooling namespaces that are not embedded. The resolver must not
 * force KV backend initialization:
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

    ID fs_bytes = source_resolver_try_filesystem(path);
    if (fs_bytes) return fs_bytes;

    return NULL;
}
