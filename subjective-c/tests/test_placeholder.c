#include "test_common.h"

extern int subjective_c_placeholder(void);

TEST(test_subjective_c_placeholder_returns_zero) {
    TEST_ASSERT_EQUAL_INT(0, subjective_c_placeholder());
}
