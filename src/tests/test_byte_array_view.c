/*
 * Unity Byte Array View Tests for Tiny-CLJ
 *
 * Tests for zero-copy byte-array views that reference externally owned memory.
 */

#include "tests_common.h"

static void external_free_mark_and_free(void *ctx) {
    // ctx points to a bool flag; payload is stored immediately after it.
    bool *flag = (bool*)ctx;
    *flag = true;
}

TEST(test_byte_array_view_exposes_external_buffer_without_copy)
{
    uint8_t buf[4] = {1, 2, 3, 4};
    CljByteArray *view = make_byte_array_view(buf, 4);
    TEST_ASSERT_NOT_NULL(view);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG((ID)view));
    TEST_ASSERT_EQUAL_INT(4, view->length);
    TEST_ASSERT_EQUAL_PTR(buf, view->data);

    // Mutating the underlying buffer must be visible via byte_array_get (no copy).
    buf[1] = 99;
    TEST_ASSERT_EQUAL_UINT8(99, byte_array_get((ID)view, 1));

    RELEASE((ID)view);
}

TEST(test_byte_array_external_calls_finalizer_on_release)
{
    // Allocate payload; it must not be freed by byte array finalizer when EXTERNAL flag is set.
    uint8_t *payload = (uint8_t*)CLJ_MALLOC(3);
    TEST_ASSERT_NOT_NULL(payload);
    payload[0] = 7;
    payload[1] = 8;
    payload[2] = 9;

    bool freed = false;

    // Wrap payload with an external finalizer that marks freed.
    CljByteArray *arr = make_byte_array_external(payload, 3, &freed, external_free_mark_and_free);
    TEST_ASSERT_NOT_NULL(arr);
    TEST_ASSERT_EQUAL_INT(3, arr->length);
    TEST_ASSERT_EQUAL_PTR(payload, arr->data);
    TEST_ASSERT_FALSE(freed);

    // Sanity: external metadata is stored on the object.
    CljByteArrayExternal *ext = (CljByteArrayExternal*)arr;
    TEST_ASSERT_TRUE((ext->base_arr.base.flags & CLJ_FLAG_BYTE_ARRAY_EXTERNAL) != 0);
    TEST_ASSERT_EQUAL_PTR(&freed, ext->external_ctx);
    TEST_ASSERT_EQUAL_PTR(external_free_mark_and_free, ext->external_free_fn);

    RELEASE((ID)arr);
#if defined(ZOMBIE_ENABLED) && ZOMBIE_ENABLED
    // In zombie mode, objects must remain intact for debugging, so we intentionally
    // do not run external finalizers (payload must stay accessible).
    TEST_ASSERT_FALSE(freed);
#else
    TEST_ASSERT_TRUE(freed);
#endif

    // Clean up payload explicitly (external owner responsibility).
    CLJ_FREE(payload);
}

