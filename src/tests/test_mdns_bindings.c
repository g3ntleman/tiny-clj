/*
 * tiny-clj.net.mdns native binding smoke tests
 */

#include "tests_common.h"

// Force direct usage so clangd doesn't complain about the include being unused.
// The file relies on types and declarations from tests_common.h.
static void *g_tests_common_anchor = (void *)&g_test_eval_state;

TEST(test_mdns_bindings_native_functions_exist)
{
    (void)g_tests_common_anchor;
    // Assert that the :native stubs can resolve to a native function implementation
    // without depending on clojure.core helpers (like resolve) being loaded.
    CljSymbol *ns = intern_symbol_global("tiny-clj.net.mdns");
    TEST_ASSERT_NOT_NULL(ns);

    CljSymbol *s_open = intern_symbol(ns, "open");
    CljSymbol *s_on_event = intern_symbol(ns, "on-event");
    CljSymbol *s_browse = intern_symbol(ns, "browse!");
    CljSymbol *s_close = intern_symbol(ns, "close!");

    TEST_ASSERT_NOT_NULL(s_open);
    TEST_ASSERT_NOT_NULL(s_on_event);
    TEST_ASSERT_NOT_NULL(s_browse);
    TEST_ASSERT_NOT_NULL(s_close);

    TEST_ASSERT_NOT_NULL(native_function_lookup(s_open, NULL));
    TEST_ASSERT_NOT_NULL(native_function_lookup(s_on_event, NULL));
    TEST_ASSERT_NOT_NULL(native_function_lookup(s_browse, NULL));
    TEST_ASSERT_NOT_NULL(native_function_lookup(s_close, NULL));
}

