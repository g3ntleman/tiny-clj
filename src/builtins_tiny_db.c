/**
 * builtins_tiny_db.c - Native bindings for tinyclj.fs and tiny-db.kv
 *
 * Filesystem and key-value store operations for embedded flash storage.
 */

#include "byte_array.h"
#include "exception.h"  // CHECK_ARITY
#include "fs_layer.h"
#include "map.h"
#include "memory.h"
#include "strings.h"
#include "symbol.h"
#include "to_string.h"
#include "value.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Helper to get string argument
static const char *require_c_string_arg(ID arg, const char *fn_name, const char *arg_desc)
{
    if (!arg) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s requires %s", fn_name, arg_desc); return NULL;
        return NULL;
    }
    CljString *s = to_string(arg);
    if (!s) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s requires %s", fn_name, arg_desc); return NULL;
        return NULL;
    }
    return string_data(s);
}

// -----------------------------------------------------------------------------
// tinyclj.fs native functions
// -----------------------------------------------------------------------------

ID native_tinyclj_fs_spit_bytes(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "tinyclj.fs/spit-bytes");
    const char *path = require_c_string_arg(args[0], "tinyclj.fs/spit-bytes", "a path string");
    if (!path) return NULL;
    /* If second arg is nil -> delete the file. Otherwise expect a byte-array. */
    FsKvStore *st = fs_global_store();
    if (!st) return NULL;
    if (!args[1]) {
        /* delete file */
        (void)fs_delete(st, path);
        return NULL;
    }
    if (TAG(args[1]) != CLJ_BYTE_ARRAY) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.fs/spit-bytes expects a byte-array or nil"); return NULL;
    }
    CljByteArray *ba = as_byte_array(args[1]);
    fs_err_t e = fs_write_bytes(st, path, ba->data, (size_t)ba->length);
    if (e != FS_NO_ERR) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.fs/spit-bytes failed (err=%d)", (int)e); return NULL;
    }
    return NULL;
}

ID native_tinyclj_fs_slurp_bytes(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "tinyclj.fs/slurp-bytes");
    const char *path = require_c_string_arg(args[0], "tinyclj.fs/slurp-bytes", "a path string");
    if (!path) return NULL;
    FsKvStore *st = fs_global_store();
    if (!st) return NULL;
    return fs_read_bytes(st, path);
}

ID native_tinyclj_fs_stat(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "tinyclj.fs/stat");
    const char *path = require_c_string_arg(args[0], "tinyclj.fs/stat", "a path string");
    if (!path) return NULL;
    FsKvStore *st = fs_global_store();
    if (!st) return NULL;

    int64_t size = fs_stat_size(st, path);
    if (size < 0) return NULL;

    // Build result map: {:path "..." :size N :type :file}
    CljMap *m = make_map(4);
    m = map_by_associng_kv(m, (ID)SYM_KW_PATH, (ID)make_string(path));
    m = map_by_associng_kv(m, (ID)SYM_KW_SIZE, fixnum((int32_t)size));
    m = map_by_associng_kv(m, (ID)SYM_KW_TYPE, (ID)SYM_KW_FILE);
    return AUTORELEASE(m);
}

ID native_tinyclj_fs_list_batch(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 3, "tinyclj.fs/list-batch");
    const char *dir_path = require_c_string_arg(args[0], "tinyclj.fs/list-batch", "a dir path string");
    if (!dir_path) return NULL;

    const char *after_key = NULL;
    if (args[1]) {
        after_key = require_c_string_arg(args[1], "tinyclj.fs/list-batch", "nil or a continuation key string");
        if (!after_key) return NULL;
    }

    if (!is_fixnum(args[2])) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.fs/list-batch expects a batch-size fixnum"); return NULL;
    }
    int bs = as_fixnum(args[2]);
    if (bs <= 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.fs/list-batch batch-size must be > 0"); return NULL;
    }

    FsKvStore *st = fs_global_store();
    if (!st) return NULL;

    char last_key[FS_KEY_MAX] = {0};
    ID entries = fs_list_dir_batch(st, dir_path, after_key, (size_t)bs, last_key, sizeof(last_key));
    if (!entries) return NULL;

    CljMap *m = make_map(4);
    m = map_by_associng_kv(m, (ID)SYM_KW_ENTRIES, entries);
    if (last_key[0] != '\0') {
        m = map_by_associng_kv(m, (ID)SYM_KW_LAST_KEY, (ID)make_string(last_key));
    } else {
        m = map_by_associng_kv(m, (ID)SYM_KW_LAST_KEY, NULL);
    }
    return AUTORELEASE(m);
}

ID native_tinyclj_fs_delete(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "tinyclj.fs/delete!");
    const char *path = require_c_string_arg(args[0], "tinyclj.fs/delete!", "a path string");
    if (!path) return NULL;
    FsKvStore *st = fs_global_store();
    if (!st) return NULL;
    return fs_delete(st, path) ? (ID)clj_true : (ID)clj_false;
}

ID native_tinyclj_fs_set_size(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "tinyclj.fs/set-size!");
    const char *path = require_c_string_arg(args[0], "tinyclj.fs/set-size!", "a path string");
    if (!path) return NULL;
    if (!is_fixnum(args[1])) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.fs/set-size! expects a size fixnum"); return NULL;
    }
    int32_t s = as_fixnum(args[1]);
    if (s < 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tinyclj.fs/set-size! size must be >= 0"); return NULL;
    }

    FsKvStore *st = fs_global_store();
    if (!st) return NULL;
    fs_err_t e = fs_set_size(st, path, (uint32_t)s);
    if (e != FS_NO_ERR) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                         "tinyclj.fs/set-size! failed (err=%d)", (int)e); return NULL;
    }

    int64_t size = fs_stat_size(st, path);
    if (size < 0) return NULL;

    CljMap *m = make_map(4);
    m = map_by_associng_kv(m, (ID)SYM_KW_PATH, (ID)make_string(path));
    m = map_by_associng_kv(m, (ID)SYM_KW_SIZE, fixnum((int32_t)size));
    m = map_by_associng_kv(m, (ID)SYM_KW_TYPE, (ID)SYM_KW_FILE);
    return AUTORELEASE(m);
}

// -----------------------------------------------------------------------------
// tiny-db.kv native functions
// -----------------------------------------------------------------------------

static ID tinyclj_kv_throw_ft(const char* op, tdb_status_t stc)
{
    if (stc == TDB_ERR_NO_MEMORY) {
        throw_oom();
        return NULL;
    }
    throw_exception_formatted(
        stc == TDB_ERR_INVALID_ARG ? EXCEPTION_ILLEGAL_ARGUMENT : EXCEPTION_RUNTIME,
        __FILE__, __LINE__, 0,
        "%s failed (err=%d)", op, (int)stc
    ); return NULL;
}

ID native_tinyclj_kv_put_bytes(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "tiny-db.kv/put-bytes");
    CljString *key_str = to_string(args[0]);
    if (!key_str) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tiny-db.kv/put-bytes expects a key string"); return NULL;
    }
    const uint8_t *key_bytes = (const uint8_t *)string_data(key_str);
    size_t key_len = (size_t)string_length(key_str);
    if (!key_bytes || key_len == 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tiny-db.kv/put-bytes key must not be empty"); return NULL;
    }
    if (key_bytes[0] == '/') {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tiny-db.kv keys must not start with '/'"); return NULL;
    }
    if (!args[1] || TAG(args[1]) != CLJ_BYTE_ARRAY) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tiny-db.kv/put-bytes expects a byte-array"); return NULL;
    }
    FsKvStore *st = fs_global_store();
    if (!st) return NULL;
    CljByteArray *ba = as_byte_array(args[1]);
    tdb_status_t stc = fs_kv_put_key_bytes_status(st, key_bytes, key_len, ba->data, (size_t)ba->length);
    if (stc != TDB_OK) {
        return tinyclj_kv_throw_ft("tiny-db.kv/put-bytes", stc);
    }
    return NULL;
}

ID native_tinyclj_kv_get_bytes(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "tiny-db.kv/get-bytes");
    CljString *key_str = to_string(args[0]);
    if (!key_str) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tiny-db.kv/get-bytes expects a key string"); return NULL;
    }
    const uint8_t *key_bytes = (const uint8_t *)string_data(key_str);
    size_t key_len = (size_t)string_length(key_str);
    if (!key_bytes || key_len == 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tiny-db.kv/get-bytes key must not be empty"); return NULL;
    }
    if (key_bytes[0] == '/') {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tiny-db.kv keys must not start with '/'"); return NULL;
    }
    FsKvStore *st = fs_global_store();
    if (!st) return NULL;

    size_t saved = 0;
    tdb_status_t stc = fs_kv_get_key_bytes_status(st, key_bytes, key_len, NULL, 0, &saved);
    if (stc == TDB_ERR_NOT_FOUND) {
        return NULL;
    }
    if (stc != TDB_OK) {
        return tinyclj_kv_throw_ft("tiny-db.kv/get-bytes", stc);
    }
    ID arr = (ID)make_byte_array((int)saved);
    if (!arr) return NULL;
    CljByteArray *ba = as_byte_array(arr);
    stc = fs_kv_get_key_bytes_status(st, key_bytes, key_len, ba->data, (size_t)ba->length, &saved);
    if (stc != TDB_OK) {
        return tinyclj_kv_throw_ft("tiny-db.kv/get-bytes", stc);
    }
    return AUTORELEASE(arr);
}

ID native_tinyclj_kv_delete(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "tiny-db.kv/delete!");
    CljString *key_str = to_string(args[0]);
    if (!key_str) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tiny-db.kv/delete! expects a key string"); return NULL;
    }
    const uint8_t *key_bytes = (const uint8_t *)string_data(key_str);
    size_t key_len = (size_t)string_length(key_str);
    if (!key_bytes || key_len == 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tiny-db.kv/delete! key must not be empty"); return NULL;
    }
    if (key_bytes[0] == '/') {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                         "tiny-db.kv keys must not start with '/'"); return NULL;
    }
    FsKvStore *st = fs_global_store();
    if (!st) return NULL;
    tdb_status_t stc = fs_kv_del_key_bytes_status(st, key_bytes, key_len);
    if (stc == TDB_OK) return (ID)clj_true;
    if (stc == TDB_ERR_NOT_FOUND) return (ID)clj_false;
    return tinyclj_kv_throw_ft("tiny-db.kv/delete!", stc);
}
