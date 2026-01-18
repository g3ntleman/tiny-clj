// Pretty printing tests: clojure.pprint (tiny-clj subset)

#include "tests_common.h"

TEST(test_pprint_require_and_pprint_str_multiline)
{
    // Require should succeed once libs/clojure/pprint.clj exists.
    ID out = eval_string(
        "(do "
        "  (require 'clojure.pprint) "
        "  (clojure.pprint/pprint-str {:a 1 :b [2 3] :c (list 4 5)}))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(out));

    const char *s = clj_string_data((CljString*)out);
    TEST_ASSERT_NOT_NULL(s);
    test_fprintf(stderr, "pprint-str(map) output:\n%s\n", s);

    // Must be multi-line and indented.
    TEST_ASSERT_TRUE_MESSAGE(strstr(s, "{\n") != NULL, "pprint-str(map) should start with '{\\n'");
    TEST_ASSERT_TRUE_MESSAGE(strstr(s, "\n  ") != NULL, "pprint-str(map) should contain indented lines");
    TEST_ASSERT_TRUE_MESSAGE(strstr(s, "\n}") != NULL, "pprint-str(map) should contain closing brace on its own line");

    // Must contain keys (order is intentionally unspecified; no sorting).
    TEST_ASSERT_TRUE(strstr(s, ":a") != NULL);
    TEST_ASSERT_TRUE(strstr(s, ":b") != NULL);
    TEST_ASSERT_TRUE(strstr(s, ":c") != NULL);

    // Pretty-print a list as well.
    ID out2 = eval_string("(clojure.pprint/pprint-str (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out2);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(out2));
    const char *s2 = clj_string_data((CljString*)out2);
    TEST_ASSERT_NOT_NULL(s2);
    test_fprintf(stderr, "pprint-str(list) output:\n%s\n", s2);
    TEST_ASSERT_TRUE_MESSAGE(strstr(s2, "(\n") != NULL, "pprint-str(list) should start with '(\\n'");
    TEST_ASSERT_TRUE_MESSAGE(strstr(s2, "\n  1") != NULL, "pprint-str(list) should contain indented elements");
    TEST_ASSERT_TRUE_MESSAGE(strstr(s2, "\n)") != NULL, "pprint-str(list) should contain closing paren on its own line");
}

