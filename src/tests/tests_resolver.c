/*
 * Test helper: register source at a path so require/load-file resolve via the resolver (KV store).
 * Tests use this instead of relying on files on disk.
 */

#include "tests_common.h"
#include "../fs_layer.h"
#include <string.h>

void register_resolver_source(const char *path, const char *source) {
    if (!path || !source) return;
    FsKvStore *st = fs_global_store();
    if (!st) return;
    size_t len = strlen(source);
    (void)fs_write_bytes(st, path, (const uint8_t *)source, len);
}

static const char *test_ns_src = "(ns test.ns)\n(def v 42)\n(def v2 7)\n";
static const char *test_alias_src = "(ns test.alias)\n(def func 100)\n";
static const char *test_refer_src = "(ns test.refer)\n(def func 200)\n";
static const char *test_referall_src = "(ns test.referall)\n(def var1 300)\n(def var2 400)\n";
static const char *test_multi1_src = "(ns test.multi1)\n(def x 500)\n";
static const char *test_multi2_src = "(ns test.multi2)\n(def y 600)\n";
static const char *test_require_idempotent_src = "(ns test.require-idempotent)\n(def loaded-token (gensym))\n";
static const char *test_nested_path_src = "(ns test.nested.path)\n(def nested-var 42)\n";
static const char *test_aliasres_src = "(ns test.aliasres)\n(def resvar 700)\n";
static const char *test_unique_src = "(ns test.unique)\n(def unique-func 300)\n";
static const char *test_conflict_src = "(ns test.conflict)\n(def map 400)\n";

void register_test_namespace_libs(void) {
    register_resolver_source("/libs/test/ns.clj", test_ns_src);
    register_resolver_source("/libs/test/alias.clj", test_alias_src);
    register_resolver_source("/libs/test/refer.clj", test_refer_src);
    register_resolver_source("/libs/test/referall.clj", test_referall_src);
    register_resolver_source("/libs/test/multi1.clj", test_multi1_src);
    register_resolver_source("/libs/test/multi2.clj", test_multi2_src);
    register_resolver_source("/libs/test/require-idempotent.clj", test_require_idempotent_src);
    register_resolver_source("/libs/test/nested/path.clj", test_nested_path_src);
    register_resolver_source("/libs/test/aliasres.clj", test_aliasres_src);
    register_resolver_source("/libs/test/unique.clj", test_unique_src);
    register_resolver_source("/libs/test/conflict.clj", test_conflict_src);
}
