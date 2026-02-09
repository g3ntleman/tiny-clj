#include "tests_common.h"

#include "../to_string.h"
#include "../ast_canon.h"

#include "instant.h"
#include "uuid.h"

/* Embedded EDN fixture (avoids path/cwd issues). */
static const char ALL_TYPES_EDN[] =
R"EDN({:nil nil
 :true true
 :false false

 :int 42
 :neg-int -7
 :float 3.5

 :string "hello\nworld"
 :escaped "quote: \" backslash: \\"

 :char-a \a
 :char-newline \newline

 :keyword :kw
 :symbol foo

 :vector [1 2 3]
 :list (1 2 3)

 :map {:a 1 :b 2}
 :empty-vector []
 :empty-map {}

 :instant #inst "1970-01-01T00:00:00.000Z"
 :uuid #uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6"}
)EDN";

static ID map_get_required(CljPersistentMap *m, const char *kw_name)
{
    CljSymbol *kw = intern_symbol_global(kw_name);
    TEST_ASSERT_NOT_NULL(kw);
    ID v = map_get((ID)m, kw);
    if (v == NULL || v == NOT_FOUND) {
        char msg[128];
        mini_snprintf(msg, sizeof(msg), "key '%s' not in map (count=%d)", kw_name, map_count((ID)m));
        TEST_FAIL_MESSAGE(msg);
    }
    return v;
}

TEST(test_edn_file_all_supported_types)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    Reader reader;
    reader_init_with_source(&reader, ALL_TYPES_EDN, "<all_types.edn>");

    ID parsed = NULL;
    TRY {
        parsed = parse_expr(&reader, g_test_eval_state);
    } CATCH(ex) {
        if (ex) TEST_FAIL_MESSAGE(ex->message);
        TEST_FAIL_MESSAGE("parse_expr threw");
    } END_TRY

    TEST_ASSERT_NOT_NULL_MESSAGE(parsed, "parse_expr returned NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_MAP_PERSISTENT, TAG(parsed), "expected map");
    TEST_ASSERT_TRUE_MESSAGE(map_count(parsed) > 0, "parsed map empty");

    /* Canonicalize so symbol tokens become CljSymbol and key lookup works. */
    parsed = canonicalize_ast(parsed, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_EQUAL_INT(CLJ_MAP_PERSISTENT, TAG(parsed));
    TEST_ASSERT_TRUE_MESSAGE(map_count(parsed) > 0, "map empty after canonicalize");

    reader_skip_all(&reader);

    CljPersistentMap *m = as_map(parsed);
    TEST_ASSERT_NOT_NULL(m);

    // Test :true first to check if map lookup works at all
    ID v_true = map_get_required(m, ":true");
    TEST_ASSERT_EQUAL_PTR(clj_true, v_true);

    ID v_false = map_get_required(m, ":false");
    TEST_ASSERT_EQUAL_PTR(clj_false, v_false);

    assert_fixnum((CljObject *)map_get_required(m, ":int"), 42);
    assert_fixnum((CljObject *)map_get_required(m, ":neg-int"), -7);

    ID v_float = map_get_required(m, ":float");
    float fval = 0.0f;
    if (is_fixed(v_float)) fval = (float)as_fixed(v_float);
    else if (is_fixnum(v_float)) fval = (float)as_fixnum(v_float);
    else TEST_FAIL_MESSAGE(":float must be fixed or fixnum");
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.5f, fval);

    assert_string((CljObject *)map_get_required(m, ":string"), "hello\nworld");
    assert_string((CljObject *)map_get_required(m, ":escaped"), "quote: \" backslash: \\");

    ID v_char_a = map_get_required(m, ":char-a");
    if (!is_character(v_char_a)) {
        char msg[64];
        mini_snprintf(msg, sizeof(msg), ":char-a tag=%u not character", (unsigned)TAG(v_char_a));
        TEST_FAIL_MESSAGE(msg);
    }
    TEST_ASSERT_EQUAL_INT('a', (int)as_character(v_char_a));

    ID v_char_nl = map_get_required(m, ":char-newline");
    if (!is_character(v_char_nl)) {
        char msg[64];
        mini_snprintf(msg, sizeof(msg), ":char-newline tag=%u not character", (unsigned)TAG(v_char_nl));
        TEST_FAIL_MESSAGE(msg);
    }
    TEST_ASSERT_EQUAL_INT('\n', (int)as_character(v_char_nl));

    ID v_kw = map_get_required(m, ":keyword");
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(v_kw));
    TEST_ASSERT_EQUAL_STRING(":kw", as_symbol((CljValue)v_kw)->cname);

    ID v_sym = map_get_required(m, ":symbol");
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(v_sym));
    TEST_ASSERT_EQUAL_STRING("foo", as_symbol((CljValue)v_sym)->cname);

    ID v_vec = map_get_required(m, ":vector");
    assert_vector((CljObject *)v_vec);
    CljPersistentVector *vec = as_persistent_vector(v_vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(vec, 0)));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(vec, 1)));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(vector_nth(vec, 2)));

    ID v_list = map_get_required(m, ":list");
    assert_list((CljObject *)v_list);
    TEST_ASSERT_EQUAL_INT(3, list_count(as_list(v_list)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(list_nth(as_list(v_list), 0)));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(list_nth(as_list(v_list), 1)));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(list_nth(as_list(v_list), 2)));

    ID v_map = map_get_required(m, ":map");
    assert_map((CljObject *)v_map);
    CljPersistentMap *inner = as_map(v_map);
    ID a_val = map_get(inner, (ID)intern_symbol_global(":a"));
    ID b_val = map_get(inner, (ID)intern_symbol_global(":b"));
    TEST_ASSERT_NOT_EQUAL((ID)NOT_FOUND, a_val);
    TEST_ASSERT_NOT_EQUAL((ID)NOT_FOUND, b_val);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(a_val));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(b_val));

    ID v_empty_vec = map_get_required(m, ":empty-vector");
    assert_vector((CljObject *)v_empty_vec);
    CljPersistentVector *empty_vec = as_persistent_vector(v_empty_vec);
    TEST_ASSERT_EQUAL_INT(0, vector_count(empty_vec));

    ID v_empty_map = map_get_required(m, ":empty-map");
    assert_map((CljObject *)v_empty_map);
    TEST_ASSERT_EQUAL_INT(0, map_count(as_map(v_empty_map)));

    ID v_inst = map_get_required(m, ":instant");
    TEST_ASSERT_EQUAL_INT(CLJ_INSTANT, TAG(v_inst));
    TEST_ASSERT_EQUAL_INT(0, clj_instant_days(v_inst));
    TEST_ASSERT_EQUAL_INT(0, (int)clj_instant_ms(v_inst));

    ID v_uuid = map_get_required(m, ":uuid");
    TEST_ASSERT_EQUAL_INT(CLJ_UUID, TAG(v_uuid));
    char uuid_buf[37];
    clj_uuid_to_cstring(v_uuid, uuid_buf);
    TEST_ASSERT_EQUAL_STRING("f81d4fae-7dec-11d0-a765-00a0c91e6bf6", uuid_buf);
}

TEST(test_tagged_literals_roundtrip_inst_uuid)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID inst = AUTORELEASE(make_instant(0, 0));
    CljString *inst_str = pr_str(inst);
    TEST_ASSERT_NOT_NULL(inst_str);

    Reader inst_reader;
    reader_init_with_source(&inst_reader, clj_string_data(inst_str), "<inst roundtrip>");
    ID inst_parsed = parse_expr(&inst_reader, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(inst_parsed);
    TEST_ASSERT_EQUAL_INT(CLJ_INSTANT, TAG(inst_parsed));
    TEST_ASSERT_EQUAL_INT(clj_instant_days(inst), clj_instant_days(inst_parsed));
    TEST_ASSERT_EQUAL_INT((int)clj_instant_ms(inst), (int)clj_instant_ms(inst_parsed));

    ID uuid = AUTORELEASE(clj_uuid_from_string("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    TEST_ASSERT_NOT_NULL(uuid);
    CljString *uuid_str = pr_str(uuid);
    TEST_ASSERT_NOT_NULL(uuid_str);

    Reader uuid_reader;
    reader_init_with_source(&uuid_reader, clj_string_data(uuid_str), "<uuid roundtrip>");
    ID uuid_parsed = parse_expr(&uuid_reader, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(uuid_parsed);
    TEST_ASSERT_EQUAL_INT(CLJ_UUID, TAG(uuid_parsed));
    char uuid_buf[37], parsed_buf[37];
    clj_uuid_to_cstring(uuid, uuid_buf);
    clj_uuid_to_cstring(uuid_parsed, parsed_buf);
    TEST_ASSERT_EQUAL_STRING(uuid_buf, parsed_buf);
}
