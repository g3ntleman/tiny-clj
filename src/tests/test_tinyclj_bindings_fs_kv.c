#include "tests_common.h"

#include "../fs_layer.h"

TEST(test_tinyclj_fs_and_kv_bindings_smoke)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    /* Reset global store so tests are deterministic. */
    fs_global_store_reset();

    /* Load :native stubs */
    eval_string("(require 'tinyclj.fs)", g_test_eval_state);
    eval_string("(require 'tinyclj.kv)", g_test_eval_state);

    /* mkdir */
    CljObject *mk = eval_string("(tinyclj.fs/mkdir \"/data/\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(mk);
    TEST_ASSERT_EQUAL_INT(CLJ_BOOL, TAG(mk));

    /* write bytes */
    CljObject *w = eval_string(
        "(let [a (byte-array 3)]"
        "  (aset a 0 1) (aset a 1 2) (aset a 2 3)"
        "  (tinyclj.fs/spit-bytes \"/data/x.bin\" a))",
        g_test_eval_state);
    (void)w; /* returns nil */

    /* read bytes */
    CljObject *rb = eval_string("(tinyclj.fs/slurp-bytes \"/data/x.bin\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(rb);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(rb));
    CljByteArray *ba = as_byte_array(rb);
    TEST_ASSERT_EQUAL_INT(3, ba->length);
    TEST_ASSERT_EQUAL_UINT8(1, ba->data[0]);
    TEST_ASSERT_EQUAL_UINT8(2, ba->data[1]);
    TEST_ASSERT_EQUAL_UINT8(3, ba->data[2]);

    /* list */
    CljObject *lst = eval_string("(tinyclj.fs/list \"/data/\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(lst));

    /* kv put/get (key must not start with /) */
    eval_string(
        "(let [a (byte-array 2)]"
        "  (aset a 0 9) (aset a 1 8)"
        "  (tinyclj.kv/put-bytes \"user:prefs\" a))",
        g_test_eval_state);
    CljObject *kvb = eval_string("(tinyclj.kv/get-bytes \"user:prefs\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(kvb);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(kvb));
    CljByteArray *kba = as_byte_array(kvb);
    TEST_ASSERT_EQUAL_INT(2, kba->length);
    TEST_ASSERT_EQUAL_UINT8(9, kba->data[0]);
    TEST_ASSERT_EQUAL_UINT8(8, kba->data[1]);
}

