#include "tests_common.h"

TEST(test_plain_group_pattern_matches_shared_group_name) {
    TEST_ASSERT_TRUE(test_name_matches_pattern(
        "shared_test_equal/equal_vectors",
        "test_equal/*"));
}

TEST(test_shared_group_pattern_matches_plain_group_name) {
    TEST_ASSERT_TRUE(test_name_matches_pattern(
        "test_equal/equal_vectors",
        "shared_test_equal/*"));
}

TEST(test_unrelated_group_pattern_does_not_match_shared_group_name) {
    TEST_ASSERT_FALSE(test_name_matches_pattern(
        "shared_test_equal/equal_vectors",
        "test_map/*"));
}
