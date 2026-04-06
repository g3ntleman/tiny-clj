#include "source_resolver.h"

#include "fs_layer.h"
#include "embedded_sources.h"
#include "byte_array.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#if defined(__APPLE__) && !defined(ESP32_BUILD)
#include <mach-o/dyld.h>
#endif

static char g_source_resolver_bundle_root[PATH_MAX] = {0};
static bool g_source_resolver_bundle_root_override_active = false;

static bool source_resolver_has_prefix(const char *text, const char *prefix) {
    if (!text || !prefix) {
        return false;
    }
    size_t prefix_len = strlen(prefix);
    return strncmp(text, prefix, prefix_len) == 0;
}

static bool source_resolver_has_suffix(const char *text, const char *suffix) {
    if (!text || !suffix) {
        return false;
    }
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static const char *source_resolver_virtual_rel_path(const char *path) {
    if (!path) {
        return NULL;
    }
    return path[0] == '/' ? path + 1 : path;
}

#if !defined(ESP32_BUILD)
static bool source_resolver_is_bundle_virtual_path(const char *path) {
    return source_resolver_has_prefix(path, "/libs/") ||
           source_resolver_has_prefix(path, "/assets/") ||
           source_resolver_has_prefix(path, "/boot/") ||
           source_resolver_has_suffix(path, ".clj");
}
#endif // !ESP32_BUILD

static bool source_resolver_is_filesystem_fallback_path(const char *path) {
    return source_resolver_has_prefix(path, "/libs/") ||
           source_resolver_has_prefix(path, "/assets/") ||
           source_resolver_has_suffix(path, ".clj");
}

#if !defined(ESP32_BUILD)
static bool source_resolver_join_path(char *out, size_t out_size, const char *root, const char *rel) {
    if (!out || out_size == 0u || !root || !root[0] || !rel || !rel[0]) {
        return false;
    }

    int written = snprintf(out, out_size, "%s/%s", root, rel);
    return written > 0 && (size_t)written < out_size;
}
#endif // !ESP32_BUILD

void source_resolver_set_bundle_resource_root(const char *root_path) {
    g_source_resolver_bundle_root[0] = '\0';
    g_source_resolver_bundle_root_override_active = false;
    if (!root_path || !root_path[0]) {
        return;
    }
    size_t len = strlen(root_path);
    if (len >= sizeof(g_source_resolver_bundle_root)) {
        len = sizeof(g_source_resolver_bundle_root) - 1u;
    }
    memcpy(g_source_resolver_bundle_root, root_path, len);
    g_source_resolver_bundle_root[len] = '\0';
    g_source_resolver_bundle_root_override_active = true;
}

void source_resolver_clear_bundle_resource_root(void) {
    g_source_resolver_bundle_root[0] = '\0';
    g_source_resolver_bundle_root_override_active = false;
}

#if !defined(ESP32_BUILD)
static const char *source_resolver_bundle_root(void) {
    if (g_source_resolver_bundle_root_override_active) {
        return g_source_resolver_bundle_root[0] ? g_source_resolver_bundle_root : NULL;
    }

#if defined(__APPLE__) && !defined(ESP32_BUILD)
    static char detected_root[PATH_MAX] = {0};
    static bool did_probe = false;
    if (did_probe) {
        return detected_root[0] ? detected_root : NULL;
    }
    did_probe = true;

    uint32_t exe_size = (uint32_t)sizeof(detected_root);
    if (_NSGetExecutablePath(detected_root, &exe_size) == 0) {
        const char *marker = strstr(detected_root, ".app/Contents/MacOS/");
        if (marker) {
            size_t app_prefix_len = (size_t)(marker - detected_root) + 4u;
            if (app_prefix_len + strlen("/Contents/Resources") + 1u < sizeof(detected_root)) {
                char bundle_root[PATH_MAX];
                memcpy(bundle_root, detected_root, app_prefix_len);
                bundle_root[app_prefix_len] = '\0';
                strcat(bundle_root, "/Contents/Resources");
                strcpy(detected_root, bundle_root);
                return detected_root;
            }
        }
    }
    detected_root[0] = '\0';
#endif

    return NULL;
}
#endif // !ESP32_BUILD (source_resolver_bundle_root)

#if !defined(ESP32_BUILD)
static const char *source_resolver_repo_root(void) {
    static char repo_root[PATH_MAX] = {0};
    static bool did_probe = false;

    if (did_probe) {
        return repo_root[0] ? repo_root : NULL;
    }
    did_probe = true;

    const char *marker_abs = "/src/source_resolver.c";
    const char *marker_rel = "src/source_resolver.c";
    const char *pos = strstr(__FILE__, marker_abs);
    if (!pos) {
        pos = strstr(__FILE__, marker_rel);
    }
    if (!pos) {
        return NULL;
    }

    size_t repo_len = (size_t)(pos - __FILE__);
    if (repo_len == 0u || repo_len >= sizeof(repo_root)) {
        return NULL;
    }

    memcpy(repo_root, __FILE__, repo_len);
    repo_root[repo_len] = '\0';
    return repo_root;
}
#endif // !ESP32_BUILD (source_resolver_repo_root)

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

static ID source_resolver_try_bundle_resources(const char *path) {
#if defined(ESP32_BUILD)
    (void)path;
    return NULL;
#else
    if (!source_resolver_is_bundle_virtual_path(path)) return NULL;

    const char *bundle_root = source_resolver_bundle_root();
    if (!bundle_root || !bundle_root[0]) return NULL;

    char bundle_path[PATH_MAX];
    const char *rel = source_resolver_virtual_rel_path(path);
    if (!source_resolver_join_path(bundle_path, sizeof(bundle_path), bundle_root, rel)) {
        return NULL;
    }
    return source_resolver_read_file_bytes(bundle_path);
#endif
}

static ID source_resolver_try_filesystem(const char *path) {
    if (!path || !path[0]) return NULL;

    // Keep filesystem fallback scoped to source/module resolution plus
    // project asset lookups under /assets/.
    if (!source_resolver_is_filesystem_fallback_path(path)) return NULL;

    // 1) Allow direct absolute/relative paths first.
    ID bytes = source_resolver_read_file_bytes(path);
    if (bytes) return bytes;

    const char *rel = source_resolver_virtual_rel_path(path);

    // 2) Common fallback for running from project root.
    bytes = source_resolver_read_file_bytes(rel);
    if (bytes) return bytes;

#if !defined(ESP32_BUILD)
    // 2) Try repository-root mapping derived from this source file path.
    //    Example: /libs/tiny-gfx/converter.clj -> <repo>/libs/tiny-gfx/converter.clj
    const char *repo_root = source_resolver_repo_root();
    if (repo_root) {
        char repo_path[PATH_MAX];
        if (source_resolver_join_path(repo_path, sizeof(repo_path), repo_root, rel)) {
            bytes = source_resolver_read_file_bytes(repo_path);
            if (bytes) return bytes;
        }
    }

    // 3) Host fallback for running from build/ or similar.
    if (strlen(rel) + 4u < PATH_MAX) {
        char parent_path[PATH_MAX];
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

#if defined(ESP32_BUILD)
static const char *const g_source_resolver_flash_seed_paths[] = {
    "/libs/tiny-fx/sound.clj",
    "/libs/tiny-fx/assets.clj",
    "/libs/tiny-fx/trk1.clj",
    "/libs/tiny-fx/sound-demos.clj",
    "/libs/tiny-fx/sound-demos-william.clj",
    "/assets/tiny-fx/sound-demos/the-entertainer.edn",
    "/assets/tiny-fx/sound-demos/minuet-in-g.edn",
    "/assets/tiny-fx/sound-demos/gymnopedie-no-1.edn",
    "/assets/tiny-fx/sound-demos/rondo-alla-turca.edn",
    "/assets/tiny-fx/sound-demos/hall-of-the-mountain-king.edn",
    "/assets/tiny-fx/sound-demos/can-can.edn",
    "/assets/tiny-fx/sound-demos/laser-sfx.edn",
    "/assets/tiny-fx/sound-demos/rocket-launch-sfx.edn",
    "/assets/tiny-fx/sound-demos/the-entertainer.trk1",
};

static bool source_resolver_is_flash_seed_path(const char *path) {
    if (!path || !path[0]) {
        return false;
    }
    for (size_t i = 0; i < sizeof(g_source_resolver_flash_seed_paths) /
                            sizeof(g_source_resolver_flash_seed_paths[0]); i++) {
        if (strcmp(path, g_source_resolver_flash_seed_paths[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool source_resolver_seed_flash_entry_if_missing(FsKvStore *st, const char *path) {
    if (!st || !path || !path[0]) {
        return false;
    }
    if (fs_exists(st, path)) {
        return true;
    }
    const uint8_t *embedded_data = NULL;
    int embedded_len = 0;
    if (!embedded_source_lookup(path, &embedded_data, &embedded_len) ||
        !embedded_data || embedded_len < 0) {
        return false;
    }
    return fs_write_bytes(st, path, embedded_data, (size_t)embedded_len) == FS_NO_ERR;
}
#endif

void source_resolver_seed_flash_sources(void) {
#if defined(ESP32_BUILD)
    FsKvStore *st = fs_global_store_if_initialized();
    if (!st) {
        st = fs_global_store();
    }
    if (!st) {
        return;
    }
    for (size_t i = 0; i < sizeof(g_source_resolver_flash_seed_paths) /
                            sizeof(g_source_resolver_flash_seed_paths[0]); i++) {
        (void)source_resolver_seed_flash_entry_if_missing(st, g_source_resolver_flash_seed_paths[i]);
    }
#endif
}

/**
 * @brief Resolves a virtual path to byte content from KV store or embedded sources.
 *
 * Lookup order is KV store first (to allow overrides), then the compiled-in
 * embedded source table. A final filesystem fallback is allowed for local dev
 * and host tooling namespaces that are not embedded. In ESP32 builds, selected
 * sound-demo paths are flash-seeded on demand and therefore may force store
 * initialization. Returns a caller-usable byte-array object on success.
 *
 * @param path Virtual path to resolve.
 * @return Byte-array object (pool-managed) or NULL when not found.
 */
ID resolve_path_to_bytes(const char *path) {
    if (!path || !path[0]) return NULL;

    // Consult KV overrides only when the store is already initialized.
    // This keeps plain require() from creating host-side tiny-db files.
    FsKvStore *st = fs_global_store_if_initialized();
#if defined(ESP32_BUILD)
    if (!st && source_resolver_is_flash_seed_path(path)) {
        st = fs_global_store();
    }
#endif
    if (st) {
        ID kv_bytes = fs_read_bytes(st, path);
        if (kv_bytes && TAG(kv_bytes) == CLJ_BYTE_ARRAY) return kv_bytes;
    }

#if defined(ESP32_BUILD)
    if (st && source_resolver_is_flash_seed_path(path) &&
        source_resolver_seed_flash_entry_if_missing(st, path)) {
        ID seeded_kv_bytes = fs_read_bytes(st, path);
        if (seeded_kv_bytes && TAG(seeded_kv_bytes) == CLJ_BYTE_ARRAY) return seeded_kv_bytes;
    }
#endif

    const uint8_t *embedded_data = NULL;
    int embedded_len = 0;
    if (embedded_source_lookup(path, &embedded_data, &embedded_len) && embedded_data && embedded_len >= 0) {
        CljByteArray *view = make_byte_array_view((uint8_t *)embedded_data, embedded_len);
        if (!view) return NULL;
        return AUTORELEASE((ID)view);
    }

    ID bundle_bytes = source_resolver_try_bundle_resources(path);
    if (bundle_bytes) return bundle_bytes;

    ID fs_bytes = source_resolver_try_filesystem(path);
    if (fs_bytes) return fs_bytes;

    return NULL;
}
