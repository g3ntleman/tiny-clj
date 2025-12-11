#include "test_common.h"
static CljMap* adopt_map(CljMap *current, CljMap *updated) {
    if (!updated) {
        return current;
    }
    if (current && current != updated) {
        RELEASE((CljObject*)current);
    }
    return updated;
}

static CljMap* make_map_or_fail(int capacity) {
    CljMap *map = make_map(capacity);
    TEST_ASSERT_NOT_NULL(map);
    return map;
}

static void expect_special(CljValue value, uint8_t expected) {
    TEST_ASSERT_TRUE(is_special(value));
    TEST_ASSERT_EQUAL_UINT8(expected, as_special(value));
}

TEST(test_map_assoc_updates_interned_symbol_key) {
    CljMap *map = make_map_or_fail(2);
    ID kw = make_string(":closed");
    TEST_ASSERT_NOT_NULL(kw);

    map = adopt_map(map, map_assoc(map, kw, clj_false));
    expect_special(map_get(map, kw, NULL), SPECIAL_FALSE);

    ID kw_update = make_string(":closed");
    map = adopt_map(map, map_assoc(map, kw_update, clj_true));
    RELEASE(kw_update);
    expect_special(map_get(map, kw, NULL), SPECIAL_TRUE);
    TEST_ASSERT_EQUAL_INT(1, map->count);

    RELEASE(map);
    RELEASE(kw);
}

TEST(test_map_assoc_channel_pattern) {
    CljMap *chan = make_map_or_fail(2);
    ID kw_value = make_string(":value");
    ID kw_closed = make_string(":closed");

    chan = adopt_map(chan, map_assoc(chan, kw_value, NULL));
    chan = adopt_map(chan, map_assoc(chan, kw_closed, clj_false));

    ID kw_update = make_string(":closed");
    chan = adopt_map(chan, map_assoc(chan, kw_update, clj_true));
    RELEASE(kw_update);
    expect_special(map_get(chan, kw_closed, NULL), SPECIAL_TRUE);

    RELEASE(chan);
    RELEASE(kw_value);
    RELEASE(kw_closed);
}

TEST(test_assign_with_immediates) {
    ID var = NULL;

    ASSIGN(var, clj_true);
    TEST_ASSERT_EQUAL_PTR(clj_true, var);
    expect_special(var, SPECIAL_TRUE);

    ASSIGN(var, clj_false);
    TEST_ASSERT_EQUAL_PTR(clj_false, var);
    expect_special(var, SPECIAL_FALSE);

    CljValue fixnum_val = fixnum(42);
    ASSIGN(var, fixnum_val);
    TEST_ASSERT_TRUE(is_fixnum(var));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(var));

    CljValue fixnum_val2 = fixnum(100);
    ASSIGN(var, fixnum_val2);
    TEST_ASSERT_TRUE(is_fixnum(var));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(var));
}

TEST(test_assign_immediates_in_map) {
    CljMap *map = make_map_or_fail(4);
    ID kw = make_string(":test");

    map = adopt_map(map, map_assoc(map, kw, clj_false));
    expect_special(map_get(map, kw, NULL), SPECIAL_FALSE);

    map = adopt_map(map, map_assoc(map, kw, clj_true));
    expect_special(map_get(map, kw, NULL), SPECIAL_TRUE);

    map = adopt_map(map, map_assoc(map, kw, fixnum(123)));
    CljValue val = map_get(map, kw, NULL);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(123, as_fixnum(val));

    map = adopt_map(map, map_assoc(map, kw, clj_false));
    expect_special(map_get(map, kw, NULL), SPECIAL_FALSE);
    TEST_ASSERT_EQUAL_INT(1, map->count);

    RELEASE(map);
    RELEASE(kw);
}

TEST(test_map_assoc_with_different_intern_calls) {
    CljMap *map = make_map_or_fail(4);
    ID kw_value = make_string(":value");
    ID kw_closed = make_string(":closed");

    map = adopt_map(map, map_assoc(map, kw_value, NULL));
    map = adopt_map(map, map_assoc(map, kw_closed, clj_false));

    expect_special(map_get(map, kw_closed, NULL), SPECIAL_FALSE);

    ID kw_closed_new = make_string(":closed");
    map = adopt_map(map, map_assoc(map, kw_closed_new, clj_true));
    RELEASE(kw_closed_new);
    expect_special(map_get(map, kw_closed, NULL), SPECIAL_TRUE);
    TEST_ASSERT_EQUAL_INT(2, map->count);

    RELEASE(map);
    RELEASE(kw_value);
    RELEASE(kw_closed);
}

TEST(test_map_assoc_with_null_value) {
    CljMap *map = make_map_or_fail(4);
    ID kw = make_string(":value");

    map = adopt_map(map, map_assoc(map, kw, NULL));
    TEST_ASSERT_NULL(map_get(map, kw, NULL));

    map = adopt_map(map, map_assoc(map, kw, fixnum(42)));
    CljValue val = map_get(map, kw, NULL);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));

    map = adopt_map(map, map_assoc(map, kw, NULL));
    TEST_ASSERT_NULL(map_get(map, kw, NULL));
    TEST_ASSERT_EQUAL_INT(1, map->count);

    RELEASE(map);
    RELEASE(kw);
}

TEST(test_exact_channel_pattern) {
    CljMap *chan = make_map_or_fail(4);
    ID kw_value = make_string(":value");
    ID kw_closed = make_string(":closed");

    chan = adopt_map(chan, map_assoc(chan, kw_value, NULL));
    chan = adopt_map(chan, map_assoc(chan, kw_closed, clj_false));

    expect_special(map_get(chan, kw_closed, NULL), SPECIAL_FALSE);

    ID kw_update = make_string(":closed");
    chan = adopt_map(chan, map_assoc(chan, kw_update, clj_true));
    RELEASE(kw_update);
    expect_special(map_get(chan, kw_closed, NULL), SPECIAL_TRUE);

    RELEASE(chan);
    RELEASE(kw_value);
    RELEASE(kw_closed);
}

TEST(test_map_assoc_with_pointer_equality) {
    CljMap *map = make_map_or_fail(4);
    ID kw = make_string(":test");

    map = adopt_map(map, map_assoc(map, kw, fixnum(42)));
    CljValue val = map_get(map, kw, NULL);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));

    map = adopt_map(map, map_assoc(map, kw, fixnum(100)));
    val = map_get(map, kw, NULL);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(val));
    TEST_ASSERT_EQUAL_INT(1, map->count);

    RELEASE(map);
    RELEASE(kw);
}

TEST(test_map_assoc_with_structural_equality) {
    CljMap *map = make_map_or_fail(4);
    CljString *str1 = make_clj_string("test-key");
    CljString *str2 = make_clj_string("test-key");
    ID key1 = str1;
    ID key2 = str2;

    TEST_ASSERT_NOT_NULL(str1);
    TEST_ASSERT_NOT_NULL(str2);

    map = adopt_map(map, map_assoc(map, key1, fixnum(42)));
    map = adopt_map(map, map_assoc(map, key2, fixnum(100)));

    CljValue val = map_get(map, key1, NULL);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(val));
    TEST_ASSERT_EQUAL_INT(1, map->count);

    RELEASE((CljObject*)str1);
    RELEASE((CljObject*)str2);
    RELEASE((CljObject*)map);
}

TEST(test_map_assoc_performance_unchanged) {
    CljMap *map = make_map_or_fail(100);
    ID kw = make_string(":test");

    for (int i = 0; i < 50; i++) {
        ID key = make_string(":key");
        map = adopt_map(map, map_assoc(map, key, fixnum(i)));
        RELEASE(key);
    }

    map = adopt_map(map, map_assoc(map, kw, fixnum(42)));
    CljValue val = map_get(map, kw, NULL);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));

    RELEASE(map);
    RELEASE(kw);
}
