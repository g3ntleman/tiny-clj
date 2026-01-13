// Runtime stats tests: (tinyclj.runtime/stats)

#include "tests_common.h"

TEST(test_runtime_stats_basic_keys_present)
{
    // Ensure tinyclj.runtime is loaded so the :native stub is defined.
    ID result = eval_string("(do (require 'tinyclj.runtime) (tinyclj.runtime/stats))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_map(result));

    CljMap *m = (CljMap *)result;

    ID k_host_os = (ID)intern_symbol_global(":host-os");
    ID k_host_os_version = (ID)intern_symbol_global(":host-os-version");
    ID k_tiny_clj_version = (ID)intern_symbol_global(":tiny-clj-version");
    ID k_build_time = (ID)intern_symbol_global(":build-time");

    ID v_host_os = map_get_sentinel(m, k_host_os, NOT_FOUND);
    ID v_host_os_version = map_get_sentinel(m, k_host_os_version, NOT_FOUND);
    ID v_tiny_clj_version = map_get_sentinel(m, k_tiny_clj_version, NOT_FOUND);
    ID v_build_time = map_get_sentinel(m, k_build_time, NOT_FOUND);

    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_host_os);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_host_os_version);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_tiny_clj_version);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_build_time);

    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(v_host_os));
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(v_host_os_version));
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(v_tiny_clj_version));
    TEST_ASSERT_EQUAL_INT(CLJ_INSTANT, TAG(v_build_time));
}

