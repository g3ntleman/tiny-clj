#include "test_common.h"

TEST(test_make_byte_array_initializes_zero) {
    CljByteArray *ba = make_byte_array(4);
    TEST_ASSERT_NOT_NULL(ba);
    TEST_ASSERT_EQUAL_INT(4, byte_array_length((CljValue)ba));
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, byte_array_get((CljValue)ba, i));
    }
    RELEASE((CljObject*)ba);
}

TEST(test_byte_array_set_and_get) {
    CljByteArray *ba = make_byte_array(3);
    byte_array_set((CljValue)ba, 0, 0xAA);
    byte_array_set((CljValue)ba, 1, 0xBB);
    byte_array_set((CljValue)ba, 2, 0xCC);
    TEST_ASSERT_EQUAL_UINT8(0xAA, byte_array_get((CljValue)ba, 0));
    TEST_ASSERT_EQUAL_UINT8(0xBB, byte_array_get((CljValue)ba, 1));
    TEST_ASSERT_EQUAL_UINT8(0xCC, byte_array_get((CljValue)ba, 2));
    RELEASE((CljObject*)ba);
}

TEST(test_byte_array_copy_between_arrays) {
    CljByteArray *src = make_byte_array(4);
    for (int i = 0; i < 4; ++i) {
        byte_array_set((CljValue)src, i, (uint8_t)(i + 1));
    }
    CljByteArray *dest = make_byte_array(4);
    byte_array_copy((CljValue)dest, 0, (CljValue)src, 0, 4);
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_UINT8(i + 1, byte_array_get((CljValue)dest, i));
    }
    RELEASE((CljObject*)src);
    RELEASE((CljObject*)dest);
}

TEST(test_byte_array_slice) {
    CljByteArray *src = make_byte_array(5);
    for (int i = 0; i < 5; ++i) {
        byte_array_set((CljValue)src, i, (uint8_t)(10 + i));
    }
    CljValue slice = byte_array_slice((CljValue)src, 1, 3);
    TEST_ASSERT_TRUE(slice != NULL);
    TEST_ASSERT_EQUAL_INT(3, byte_array_length(slice));
    TEST_ASSERT_EQUAL_UINT8(11, byte_array_get(slice, 0));
    TEST_ASSERT_EQUAL_UINT8(12, byte_array_get(slice, 1));
    TEST_ASSERT_EQUAL_UINT8(13, byte_array_get(slice, 2));
    RELEASE(slice);
    RELEASE((CljObject*)src);
}
