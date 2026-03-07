#include "tests_common.h"
#include "../ast.h"
#include "../function.h"
#include "../record.h"
#include <string.h>

static CljSymbol *SYM_TEST_RECORD_TYPE_KEYS = NULL;
static CljSymbol *SYM_TEST_RECORD_TYPE_VALUES = NULL;
static CljSymbol *SYM_TEST_RECORD_TYPE_MAP = NULL;

static CljSymbol *test_record_type_symbol(CljSymbol **slot, const char *name) {
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_NOT_NULL(name);
    if (!*slot) {
        *slot = intern_symbol_global(name);
    }
    TEST_ASSERT_NOT_NULL(*slot);
    return *slot;
}

static void assert_vector_fixnum(ID vec_id, unsigned int index, int expected) {
    TEST_ASSERT_NOT_NULL(vec_id);
    TEST_ASSERT_TRUE(TAG(vec_id) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *vec = as_vector(vec_id);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_TRUE(vector_count(vec) > index);

    ID val = vector_nth(vec, index);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(expected, as_fixnum(val));
}

static bool ast_contains_symbol_call(ID expr, const char *symbol_name) {
    if (!expr || !symbol_name) return false;

    if (TAG(expr) == CLJ_AST_CALL) {
        CljASTCall *call = as_ast_call(expr);
        if (!call) return false;

        if (call->op && TAG(call->op) == CLJ_SYMBOL) {
            CljSymbol *op = as_symbol(call->op);
            if (op && op->cname && strcmp(op->cname, symbol_name) == 0) {
                return true;
            }
        }

        if (call->args) {
            unsigned int argc = vector_count(call->args);
            for (unsigned int i = 0; i < argc; i++) {
                if (ast_contains_symbol_call(vector_nth(call->args, i), symbol_name)) return true;
            }
        }
        return false;
    }

    if (TAG(expr) == CLJ_VECTOR_PERSISTENT || TAG(expr) == CLJ_VECTOR_TRANSIENT) {
        CljPersistentVector *vec = as_vector(expr);
        if (!vec) return false;
        unsigned int count = vector_count(vec);
        for (unsigned int i = 0; i < count; i++) {
            if (ast_contains_symbol_call(vector_nth(vec, i), symbol_name)) return true;
        }
        return false;
    }

    if (!is_list_type(TAG(expr))) return false;
    CljList *list = as_list(expr);
    while (list) {
        if (ast_contains_symbol_call(list->first, symbol_name)) return true;
        if (!list->rest || !is_list_type(TAG(list->rest))) break;
        list = as_list(list->rest);
    }
    return false;
}

TEST(test_record_high_level_lookup_paths) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = eval_string(
        "(do "
        "  (defrecord Point [x y]) "
        "  (let [p (->Point 10 20)] "
        "    [(get p :x) (:y p) (p :x) (count p)]))",
        g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);

    assert_vector_fixnum(result, 0, 10);
    assert_vector_fixnum(result, 1, 20);
    assert_vector_fixnum(result, 2, 10);
    assert_vector_fixnum(result, 3, 2);
}

TEST(test_record_high_level_map_constructor_and_map_predicate) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = eval_string(
        "(do "
        "  (defrecord User [id name]) "
        "  (let [u (map->User {:id 7 :name \"Ada\"})] "
        "    [(map? u) (get u :id) (:name u)]))",
        g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_UINT(3, vector_count(vec));

    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(vec, 0));
    TEST_ASSERT_TRUE(is_fixnum(vector_nth(vec, 1)));
    TEST_ASSERT_EQUAL_INT(7, as_fixnum(vector_nth(vec, 1)));

    ID name_val = vector_nth(vec, 2);
    TEST_ASSERT_NOT_NULL(name_val);
    TEST_ASSERT_TRUE(TAG(name_val) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("Ada", string_data(name_val));
}

TEST(test_record_high_level_assoc_dissoc_semantics) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = eval_string(
        "(do "
        "  (defrecord Person [name age]) "
        "  (let [p (->Person \"A\" 10) "
        "        p2 (assoc p :age 11) "
        "        d1 (dissoc p :age)] "
        "    [(= \"Record\" (name (type p2))) "
        "     (not= \"Record\" (name (type d1)))]))",
        g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));

    for (unsigned int i = 0; i < 2; i++) {
        TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(vec, i));
    }
}

TEST(test_record_unknown_assoc_key_throws_not_implemented) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    bool exception_caught = false;
    TRY {
        (void)eval_string(
            "(do "
            "  (defrecord PersonAssoc [name age]) "
            "  (assoc (->PersonAssoc \"A\" 10) :extra 99))",
            g_test_eval_state);
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_NOT_IMPLEMENTED, ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(exception_caught);
}

TEST(test_record_unknown_dissoc_key_throws_not_implemented) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    bool exception_caught = false;
    TRY {
        (void)eval_string(
            "(do "
            "  (defrecord PersonDissoc [name age]) "
            "  (dissoc (->PersonDissoc \"A\" 10) :missing))",
            g_test_eval_state);
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_NOT_IMPLEMENTED, ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(exception_caught);
}

TEST(test_record_map_constructor_extra_key_throws_not_implemented) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    bool exception_caught = false;
    TRY {
        (void)eval_string(
            "(do "
            "  (defrecord PersonMapCtor [name age]) "
            "  (map->PersonMapCtor {:name \"A\" :age 10 :extra 99}))",
            g_test_eval_state);
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_NOT_IMPLEMENTED, ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(exception_caught);
}

TEST(test_record_descriptor_keeps_field_order_once_per_type) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID type_symbol = intern_symbol_global("DescriptorCheck");
    ID kw_a = intern_symbol_global(":a");
    ID kw_b = intern_symbol_global(":b");

    CljPersistentVector *fields = AUTORELEASE(make_vector(2, STRONG));
    TEST_ASSERT_NOT_NULL(fields);
    vector_conj_inplace(&fields, kw_a);
    vector_conj_inplace(&fields, kw_b);

    CljRecordDescriptor *desc = record_register_descriptor(type_symbol, fields);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_NOT_NULL(desc->field_keys);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(desc->field_keys));
    TEST_ASSERT_EQUAL_PTR(kw_a, vector_nth(desc->field_keys, 0));
    TEST_ASSERT_EQUAL_PTR(kw_b, vector_nth(desc->field_keys, 1));

    CljPersistentVector *vals1 = AUTORELEASE(make_vector(2, STRONG));
    CljPersistentVector *vals2 = AUTORELEASE(make_vector(2, STRONG));
    TEST_ASSERT_NOT_NULL(vals1);
    TEST_ASSERT_NOT_NULL(vals2);
    vector_conj_inplace(&vals1, fixnum(10));
    vector_conj_inplace(&vals1, fixnum(20));
    vector_conj_inplace(&vals2, fixnum(30));
    vector_conj_inplace(&vals2, fixnum(40));

    CljPersistentRecord *r1 = AUTORELEASE(record_create(type_symbol, vals1));
    CljPersistentRecord *r2 = AUTORELEASE(record_create(type_symbol, vals2));
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_EQUAL_PTR(r1->descriptor, r2->descriptor);
    TEST_ASSERT_EQUAL_PTR(desc, r1->descriptor);

    ID r1_a = record_get_by_index((ID)r1, 0);
    ID r1_b = record_get_by_index((ID)r1, 1);
    TEST_ASSERT_TRUE(is_fixnum(r1_a));
    TEST_ASSERT_TRUE(is_fixnum(r1_b));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(r1_a));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(r1_b));
}

TEST(test_record_optimizer_rewrites_constant_key_lookup_to_index_lookup) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID fn_obj = eval_string(
        "(do "
        "  (defrecord FastR [x y]) "
        "  (fn fast-r-x [] (:x (->FastR 1 2))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_obj);
    TEST_ASSERT_TRUE(TAG(fn_obj) == CLJ_CLOSURE);

    CljFunction *fn = as_function(fn_obj);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_NOT_NULL(fn->body);
    TEST_ASSERT_TRUE(TAG(fn->body) == CLJ_AST_CALL);

    CljASTCall *body_call = as_ast_call(fn->body);
    TEST_ASSERT_NOT_NULL(body_call);
    TEST_ASSERT_NOT_NULL(body_call->op);
    TEST_ASSERT_TRUE(TAG(body_call->op) == CLJ_SYMBOL);

    CljSymbol *op_sym = as_symbol(body_call->op);
    TEST_ASSERT_NOT_NULL(op_sym);
    TEST_ASSERT_NOT_NULL(op_sym->cname);
    TEST_ASSERT_EQUAL_STRING("record-get-index", op_sym->cname);

    TEST_ASSERT_NOT_NULL(body_call->args);
    TEST_ASSERT_EQUAL_UINT(3, vector_count(body_call->args));
    ID idx = vector_nth(body_call->args, 1);
    TEST_ASSERT_TRUE(is_fixnum(idx));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(idx));

    ID result = eval_string(
        "(do (defrecord FastR2 [x y]) ((fn fast-r-x [] (:x (->FastR2 1 2)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

TEST(test_record_optimizer_rewrites_let_bound_lookup_to_index_lookup) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID fn_obj = eval_string(
        "(do "
        "  (defrecord FastLetR [x y]) "
        "  (fn fast-let-r [] "
        "    (let [r (->FastLetR 3 4) "
        "          alias r] "
        "      (:x alias))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_obj);
    TEST_ASSERT_TRUE(TAG(fn_obj) == CLJ_CLOSURE);

    CljFunction *fn = as_function(fn_obj);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_NOT_NULL(fn->body);
    TEST_ASSERT_TRUE(ast_contains_symbol_call(fn->body, "record-get-index"));

    ID result = eval_string(
        "(do "
        "  (defrecord FastLetRRun [x y]) "
        "  ((fn [] (let [r (->FastLetRRun 3 4) alias r] (:x alias)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

TEST(test_record_optimizer_rewrites_loop_binding_without_recur) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID fn_obj = eval_string(
        "(do "
        "  (defrecord FastLoopNoRecur [x y]) "
        "  (fn fast-loop-no-recur [] "
        "    (loop [r (->FastLoopNoRecur 5 6)] "
        "      (:x r))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_obj);
    TEST_ASSERT_TRUE(TAG(fn_obj) == CLJ_CLOSURE);

    CljFunction *fn = as_function(fn_obj);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_NOT_NULL(fn->body);
    TEST_ASSERT_TRUE(ast_contains_symbol_call(fn->body, "record-get-index"));

    ID result = eval_string(
        "(do "
        "  (defrecord FastLoopNoRecurRun [x y]) "
        "  ((fn [] (loop [r (->FastLoopNoRecurRun 5 6)] (:x r)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(result));
}

TEST(test_record_optimizer_does_not_rewrite_loop_binding_with_recur) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID fn_obj = eval_string(
        "(do "
        "  (defrecord FastLoopRecur [x y]) "
        "  (fn fast-loop-recur [] "
        "    (loop [r (->FastLoopRecur 1 2) done false] "
        "      (if done "
        "        (:x r) "
        "        (recur {:x 9 :y 8} true)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_obj);
    TEST_ASSERT_TRUE(TAG(fn_obj) == CLJ_CLOSURE);

    CljFunction *fn = as_function(fn_obj);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_NOT_NULL(fn->body);
    TEST_ASSERT_FALSE(ast_contains_symbol_call(fn->body, "record-get-index"));

    ID result = eval_string(
        "(do "
        "  (defrecord FastLoopRecurRun [x y]) "
        "  ((fn [] "
        "      (loop [r (->FastLoopRecurRun 1 2) done false] "
        "        (if done "
        "          (:x r) "
        "          (recur {:x 9 :y 8} true))))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(9, as_fixnum(result));
}

TEST(test_record_slotref_runtime_hint_revalidates_on_shape_change) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID fn_obj = eval_string(
        "(do "
        "  (defrecord HintA [x y]) "
        "  (defrecord HintB [y x]) "
        "  (fn hint-shape-change [] "
        "    (loop [r (->HintA 1 2) done false] "
        "      (if done "
        "        (:x r) "
        "        (recur (->HintB 7 8) true)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_obj);
    TEST_ASSERT_TRUE(TAG(fn_obj) == CLJ_CLOSURE);

    CljFunction *fn = as_function(fn_obj);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_NOT_NULL(fn->body);
    TEST_ASSERT_FALSE(ast_contains_symbol_call(fn->body, "record-get-index"));

    ID result = eval_string(
        "(do "
        "  (defrecord HintARun [x y]) "
        "  (defrecord HintBRun [y x]) "
        "  ((fn [] "
        "      (loop [r (->HintARun 1 2) done false] "
        "        (if done "
        "          (:x r) "
        "          (recur (->HintBRun 7 8) true))))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(8, as_fixnum(result));
}

TEST(test_record_assoc_cow_rc_one_updates_in_place) {
    ID type_symbol = intern_symbol_global("RecordCowInPlace");
    ID kw_a = intern_symbol_global(":a");
    ID kw_b = intern_symbol_global(":b");

    CljPersistentVector *fields = AUTORELEASE(make_vector(2, STRONG));
    TEST_ASSERT_NOT_NULL(fields);
    vector_conj_inplace(&fields, kw_a);
    vector_conj_inplace(&fields, kw_b);
    TEST_ASSERT_NOT_NULL(record_register_descriptor(type_symbol, fields));

    CljPersistentVector *vals = AUTORELEASE(make_vector(2, STRONG));
    TEST_ASSERT_NOT_NULL(vals);
    vector_conj_inplace(&vals, fixnum(1));
    vector_conj_inplace(&vals, fixnum(2));

    CljPersistentRecord *record = AUTORELEASE(record_create(type_symbol, vals));
    TEST_ASSERT_NOT_NULL(record);
    TEST_ASSERT_EQUAL_INT(1, retain_count((ID)record));

    ID updated = record_assoc((ID)record, kw_a, fixnum(11));
    TEST_ASSERT_NOT_NULL(updated);
    TEST_ASSERT_EQUAL_PTR((ID)record, updated);
    TEST_ASSERT_EQUAL_INT(2, retain_count((ID)record));

    ID a_val = record_get_sentinel(updated, kw_a, NOT_FOUND);
    ID b_val = record_get_sentinel(updated, kw_b, NOT_FOUND);
    TEST_ASSERT_TRUE(is_fixnum(a_val));
    TEST_ASSERT_TRUE(is_fixnum(b_val));
    TEST_ASSERT_EQUAL_INT(11, as_fixnum(a_val));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(b_val));
}

TEST(test_record_assoc_cow_rc_gt_one_returns_copy) {
    ID type_symbol = intern_symbol_global("RecordCowCopy");
    ID kw_a = intern_symbol_global(":a");
    ID kw_b = intern_symbol_global(":b");

    CljPersistentVector *fields = AUTORELEASE(make_vector(2, STRONG));
    TEST_ASSERT_NOT_NULL(fields);
    vector_conj_inplace(&fields, kw_a);
    vector_conj_inplace(&fields, kw_b);
    TEST_ASSERT_NOT_NULL(record_register_descriptor(type_symbol, fields));

    CljPersistentVector *vals = AUTORELEASE(make_vector(2, STRONG));
    TEST_ASSERT_NOT_NULL(vals);
    vector_conj_inplace(&vals, fixnum(1));
    vector_conj_inplace(&vals, fixnum(2));

    CljPersistentRecord *record = AUTORELEASE(record_create(type_symbol, vals));
    TEST_ASSERT_NOT_NULL(record);
    RETAIN(record);
    TEST_ASSERT_EQUAL_INT(2, retain_count((ID)record));

    ID updated = record_assoc((ID)record, kw_a, fixnum(42));
    TEST_ASSERT_NOT_NULL(updated);
    TEST_ASSERT_NOT_EQUAL((ID)record, updated);

    ID old_a = record_get_sentinel((ID)record, kw_a, NOT_FOUND);
    ID new_a = record_get_sentinel(updated, kw_a, NOT_FOUND);
    ID new_b = record_get_sentinel(updated, kw_b, NOT_FOUND);
    TEST_ASSERT_TRUE(is_fixnum(old_a));
    TEST_ASSERT_TRUE(is_fixnum(new_a));
    TEST_ASSERT_TRUE(is_fixnum(new_b));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(old_a));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(new_a));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(new_b));

    RELEASE(record);
}

TEST(test_record_assoc_via_eval_does_not_leak_descriptor_retains) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID setup = eval_string(
        "(do "
        "  (defrecord AssocLeak [x1 y1 x2 y2 x3 y3]) "
        "  (def assoc-leak-atom (atom (->AssocLeak 1 2 3 4 5 6))) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, setup);

    ID type_symbol = intern_symbol_global("AssocLeak");
    CljRecordDescriptor *desc = record_descriptor_lookup(type_symbol);
    TEST_ASSERT_NOT_NULL(desc);

    int baseline_rc = retain_count((ID)desc);
    for (int i = 0; i < 128; i++) {
        WITH_AUTORELEASE_POOL({
            ID updated = eval_string(
                "(swap! assoc-leak-atom "
                "  (fn [p] "
                "    (assoc p "
                "      :x1 11 :y1 12 "
                "      :x2 13 :y2 14 "
                "      :x3 15 :y3 16)))",
                g_test_eval_state);
            TEST_ASSERT_NOT_NULL(updated);
            TEST_ASSERT_TRUE(TAG(updated) == CLJ_RECORD);
        });
    }

    ID final_value = eval_string("@assoc-leak-atom", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(final_value);
    TEST_ASSERT_TRUE(TAG(final_value) == CLJ_RECORD);
    TEST_ASSERT_EQUAL_INT(baseline_rc, retain_count((ID)desc));
}

TEST(test_record_descriptor_create_rejects_non_symbol_type_name) {
    ID bad_type_name = make_string("NotASymbol");
    TEST_ASSERT_NOT_NULL(bad_type_name);

    CljPersistentVector *fields = make_vector(1, STRONG);
    TEST_ASSERT_NOT_NULL(fields);
    vector_conj_inplace(&fields, intern_symbol_global(":field-a"));

    bool exception_caught = false;
    TRY {
        (void)record_descriptor_create(bad_type_name, fields);
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_ILLEGAL_ARGUMENT, ex->type);
    } END_TRY

    RELEASE(fields);
    RELEASE(bad_type_name);
    TEST_ASSERT_TRUE(exception_caught);
}

TEST(test_record_keys_returns_pool_safe_alias_for_manual_descriptor) {
    ID type_name = (ID)test_record_type_symbol(&SYM_TEST_RECORD_TYPE_KEYS, "ManualTypeKeys");
    ID key_a = make_string("field-a");
    ID key_b = make_string("field-b");

    CljPersistentVector *fields = make_vector(2, STRONG);
    TEST_ASSERT_NOT_NULL(fields);
    vector_conj_inplace(&fields, key_a);
    vector_conj_inplace(&fields, key_b);

    CljRecordDescriptor *desc = record_descriptor_create(type_name, fields);
    TEST_ASSERT_NOT_NULL(desc);

    CljPersistentRecord *record = record_create_with_descriptor(desc, NULL);
    TEST_ASSERT_NOT_NULL(record);

    int fields_rc_before = retain_count((ID)fields);
    ID keys = record_keys((ID)record);
    TEST_ASSERT_NOT_NULL(keys);
    TEST_ASSERT_EQUAL_PTR((ID)fields, keys);
    TEST_ASSERT_EQUAL_INT(fields_rc_before + 1, retain_count((ID)fields));

    RELEASE(record);
    RELEASE(desc);
    RELEASE(fields);
    RELEASE(key_a);
    RELEASE(key_b);

    TEST_ASSERT_TRUE(TAG(keys) == CLJ_VECTOR_PERSISTENT);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(keys));

    ID out_a = vector_nth(keys, 0);
    ID out_b = vector_nth(keys, 1);
    TEST_ASSERT_TRUE(TAG(out_a) == CLJ_STRING);
    TEST_ASSERT_TRUE(TAG(out_b) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("field-a", string_data(out_a));
    TEST_ASSERT_EQUAL_STRING("field-b", string_data(out_b));
}

TEST(test_record_value_accessors_return_expected_aliases_for_manual_descriptor) {
    ID type_name = (ID)test_record_type_symbol(&SYM_TEST_RECORD_TYPE_VALUES, "ManualTypeValues");
    ID key_name = make_string("field-a");
    ID value_name = make_string("value-a");

    CljPersistentVector *fields = make_vector(1, STRONG);
    TEST_ASSERT_NOT_NULL(fields);
    vector_conj_inplace(&fields, key_name);

    CljRecordDescriptor *desc = record_descriptor_create(type_name, fields);
    TEST_ASSERT_NOT_NULL(desc);

    CljPersistentVector *vals = make_vector(1, STRONG);
    TEST_ASSERT_NOT_NULL(vals);
    vector_conj_inplace(&vals, value_name);

    CljPersistentRecord *record = record_create_with_descriptor(desc, vals);
    TEST_ASSERT_NOT_NULL(record);

    ID key_out = record_key_at_index((ID)record, 0);
    ID value_out_index = record_get_by_index((ID)record, 0);
    ID value_out_lookup = record_get_sentinel((ID)record, key_name, NOT_FOUND);

    TEST_ASSERT_NOT_NULL(key_out);
    TEST_ASSERT_NOT_NULL(value_out_index);
    TEST_ASSERT_NOT_NULL(value_out_lookup);
    TEST_ASSERT_EQUAL_PTR(type_name, record->descriptor->type_symbol);
    TEST_ASSERT_EQUAL_PTR(key_name, key_out);
    TEST_ASSERT_EQUAL_PTR(value_name, value_out_index);
    TEST_ASSERT_EQUAL_PTR(value_name, value_out_lookup);

    TEST_ASSERT_TRUE(TAG(key_out) == CLJ_STRING);
    TEST_ASSERT_TRUE(TAG(value_out_index) == CLJ_STRING);
    TEST_ASSERT_TRUE(TAG(value_out_lookup) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("ManualTypeValues", as_symbol(record->descriptor->type_symbol)->cname);
    TEST_ASSERT_EQUAL_STRING("field-a", string_data(key_out));

    RELEASE(record);
    RELEASE(vals);
    RELEASE(desc);
    RELEASE(fields);
    RELEASE(key_name);
    RELEASE(value_name);

    TEST_ASSERT_EQUAL_STRING("value-a", string_data(value_out_index));
    TEST_ASSERT_EQUAL_STRING("value-a", string_data(value_out_lookup));
}

TEST(test_make_map_from_record_returns_owned_map_without_leaking_when_released) {
    ID type_name = (ID)test_record_type_symbol(&SYM_TEST_RECORD_TYPE_MAP, "ManualTypeMap");
    ID key_name = make_string("field-a");
    ID value_name = make_string("value-a");

    CljPersistentVector *fields = make_vector(1, STRONG);
    TEST_ASSERT_NOT_NULL(fields);
    vector_conj_inplace(&fields, key_name);

    CljRecordDescriptor *desc = record_descriptor_create(type_name, fields);
    TEST_ASSERT_NOT_NULL(desc);

    CljPersistentVector *vals = make_vector(1, STRONG);
    TEST_ASSERT_NOT_NULL(vals);
    vector_conj_inplace(&vals, value_name);

    CljPersistentRecord *record = record_create_with_descriptor(desc, vals);
    TEST_ASSERT_NOT_NULL(record);

    int key_rc_before = retain_count(key_name);
    int value_rc_before = retain_count(value_name);

    for (int i = 0; i < 128; i++) {
        ID map_obj = (ID)make_map_from_record((ID)record);
        TEST_ASSERT_NOT_NULL(map_obj);
        TEST_ASSERT_TRUE(TAG(map_obj) == CLJ_MAP_PERSISTENT);
        TEST_ASSERT_EQUAL_PTR(value_name, map_get_sentinel(map_obj, key_name, NULL));
        RELEASE(map_obj);
    }

    TEST_ASSERT_EQUAL_INT(key_rc_before, retain_count(key_name));
    TEST_ASSERT_EQUAL_INT(value_rc_before, retain_count(value_name));

    RELEASE(record);
    RELEASE(vals);
    RELEASE(desc);
    RELEASE(fields);
    RELEASE(key_name);
    RELEASE(value_name);
}
