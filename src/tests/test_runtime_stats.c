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

TEST(test_runtime_stats_build_time_before_now)
{
    // Get build-time from stats
    ID stats = eval_string("(do (require 'tinyclj.runtime) (tinyclj.runtime/stats))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_TRUE(is_map(stats));
    
    ID k_build_time = (ID)intern_symbol_global(":build-time");
    ID v_build_time = map_get_sentinel((CljMap *)stats, k_build_time, NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(NOT_FOUND, v_build_time);
    TEST_ASSERT_EQUAL_INT(CLJ_INSTANT, TAG(v_build_time));
    
    // Get current time via (now) - it's in clojure.core
    ID now_result = eval_string("(now)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(now_result);
    TEST_ASSERT_EQUAL_INT(CLJ_INSTANT, TAG(now_result));
    
    // Build time must be <= now
    int32_t build_days = clj_instant_days(v_build_time);
    int32_t now_days = clj_instant_days(now_result);
    
    // Either build_days < now_days, or (build_days == now_days && build_millis <= now_millis)
    if (build_days < now_days) {
        // Build time is definitely before now
        TEST_PASS();
    } else if (build_days == now_days) {
        uint32_t build_millis = clj_instant_ms(v_build_time);
        uint32_t now_millis = clj_instant_ms(now_result);
        TEST_ASSERT_TRUE_MESSAGE(build_millis <= now_millis, 
                                 "Build time milliseconds must be <= now milliseconds on same day");
    } else {
        TEST_FAIL_MESSAGE("Build time is in the future!");
    }
}

