#include "tests_common.h"

#include "../to_string.h"
#include "../ast_canon.h"

#include <errno.h>
#include "instant.h"
#include "uuid.h"

static char *read_entire_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);
    if (size < 0)
    {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return NULL;
    }

    char *buf = (char *)CLJ_MALLOC((size_t)size + 1);
    if (!buf)
    {
        fclose(fp);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)size, fp);
    fclose(fp);

    if (n != (size_t)size)
    {
        CLJ_FREE(buf);
        return NULL;
    }

    buf[n] = '\0';
    if (out_len)
    {
        *out_len = n;
    }
    return buf;
}

static void build_fixture_path(char *out, size_t out_size)
{
    const char *self = __FILE__;
    const char *slash = strrchr(self, '/');
    if (!slash)
    {
        test_snprintf(out, out_size, "fixtures/all_types.edn");
        return;
    }
    size_t dir_len = (size_t)(slash - self);
    test_path_join_prefix(out, out_size, self, dir_len, "/fixtures/all_types.edn");
}

static ID map_get_required(CljPersistentMap *m, const char *kw_name)
{
    CljSymbol *kw = intern_symbol_global(kw_name);
    TEST_ASSERT_NOT_NULL(kw);
    ID v = map_get(m, kw);
    TEST_ASSERT_NOT_NULL_MESSAGE(v, kw_name);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(NOT_FOUND, v, kw_name);
    return v;
}

TEST(test_edn_file_all_supported_types)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    char path[1024];
    build_fixture_path(path, sizeof(path));

    size_t len = 0;
    char *src = read_entire_file(path, &len);
    if (!src && errno == ENOENT && path[0] != '/')
    {
        char alt[1024];
        test_snprintf(alt, sizeof(alt), "../%s", path);
        src = read_entire_file(alt, &len);
        if (src)
        {
            test_snprintf(path, sizeof(path), "%s", alt);
        }
    }
    if (!src)
    {
        char msg[256];
        test_snprintf(msg, sizeof(msg), "Failed to read EDN fixture: %s (errno=%d)", path, errno);
        TEST_FAIL_MESSAGE(msg);
    }

    Reader reader;
    reader_init_with_source(&reader, src, path);

    ID parsed = parse_expr(&reader, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_EQUAL_INT(CLJ_MAP_PERSISTENT, TAG(parsed));
    
    // Canonicalize the parsed map (interns symbol tokens, etc.)
    parsed = canonicalize_ast(parsed, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_EQUAL_INT(CLJ_MAP_PERSISTENT, TAG(parsed));

    reader_skip_all(&reader);
    TEST_ASSERT_TRUE(reader_is_eof(&reader));

    CljPersistentMap *m = as_map(parsed);
    TEST_ASSERT_NOT_NULL(m);

    // Test :true first to check if map lookup works at all
    ID v_true = map_get_required(m, ":true");
    TEST_ASSERT_EQUAL_PTR(clj_true, v_true);

    ID v_false = map_get_required(m, ":false");
    TEST_ASSERT_EQUAL_PTR(clj_false, v_false);

    assert_fixnum((CljObject *)map_get_required(m, ":int"), 42);
    assert_fixnum((CljObject *)map_get_required(m, ":neg-int"), -7);

    CLJ_FREE(src);

    ID v_float = map_get_required(m, ":float");
    TEST_ASSERT_TRUE(is_fixed(v_float));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.5f, (float)as_fixed(v_float));

    assert_string((CljObject *)map_get_required(m, ":string"), "hello\nworld");
    assert_string((CljObject *)map_get_required(m, ":escaped"), "quote: \" backslash: \\");

    ID v_char_a = map_get_required(m, ":char-a");
    TEST_ASSERT_TRUE(is_character(v_char_a));
    TEST_ASSERT_EQUAL_INT('a', (int)as_character(v_char_a));

    ID v_char_nl = map_get_required(m, ":char-newline");
    TEST_ASSERT_TRUE(is_character(v_char_nl));
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
    TEST_ASSERT_TRUE(clj_equal(inst, inst_parsed));

    ID uuid = AUTORELEASE(clj_uuid_from_string("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    TEST_ASSERT_NOT_NULL(uuid);
    CljString *uuid_str = pr_str(uuid);
    TEST_ASSERT_NOT_NULL(uuid_str);

    Reader uuid_reader;
    reader_init_with_source(&uuid_reader, clj_string_data(uuid_str), "<uuid roundtrip>");
    ID uuid_parsed = parse_expr(&uuid_reader, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(uuid_parsed);
    TEST_ASSERT_TRUE(clj_equal(uuid, uuid_parsed));
}
