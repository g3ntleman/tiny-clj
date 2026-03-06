#include "tests_common.h"
#include "../event_loop.h"
#include "../record.h"
#include "../symbol.h"
#include "../vector.h"

TEST(test_gfx_collision_contract_registers_record_descriptors) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string("(do (require 'tiny-gfx.scene) true)", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);

    ID t_rule = intern_symbol_global("CollisionRule");
    ID t_event = intern_symbol_global("CollisionEvent");
    CljRecordDescriptor *d_rule = record_descriptor_lookup(t_rule);
    CljRecordDescriptor *d_event = record_descriptor_lookup(t_event);
    TEST_ASSERT_NOT_NULL(d_rule);
    TEST_ASSERT_NOT_NULL(d_event);
    TEST_ASSERT_NOT_NULL(d_rule->field_keys);
    TEST_ASSERT_NOT_NULL(d_event->field_keys);
    TEST_ASSERT_EQUAL_UINT(7, vector_count(d_rule->field_keys));
    TEST_ASSERT_EQUAL_UINT(7, vector_count(d_event->field_keys));

    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":id"), vector_nth(d_rule->field_keys, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":slot"), vector_nth(d_rule->field_keys, 1));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":a-id"), vector_nth(d_rule->field_keys, 2));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":b-id"), vector_nth(d_rule->field_keys, 3));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":phase-mask"), vector_nth(d_rule->field_keys, 4));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enabled"), vector_nth(d_rule->field_keys, 5));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":cooldown-ms"), vector_nth(d_rule->field_keys, 6));
}

TEST(test_gfx_collision_contract_normalize_rule_defaults) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.scene) "
        "  (let [r (tiny-gfx.scene/normalize-collision-rule {:id :r1 :a-id 1 :b-id 2})] "
        "    [(:slot r) (:enabled r) (:cooldown-ms r) (:phase-mask r)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(4, vector_count(v));

    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":game"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 1));
    TEST_ASSERT_TRUE(is_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 2)));
    ID phase_mask = vector_nth(v, 3);
    TEST_ASSERT_TRUE(TAG(phase_mask) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *mask_vec = as_vector(phase_mask);
    TEST_ASSERT_NOT_NULL(mask_vec);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(mask_vec));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enter"), vector_nth(mask_vec, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":exit"), vector_nth(mask_vec, 1));
}

TEST(test_gfx_collision_contract_phase_mask_normalization_and_enabled_false) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.scene) "
        "  (let [r1 (tiny-gfx.scene/normalize-collision-rule "
        "             {:id :r1 :a-id 1 :b-id 2 :phase-mask [:stay :foo] :enabled false}) "
        "        r2 (tiny-gfx.scene/normalize-collision-rule "
        "             {:id :r2 :a-id 1 :b-id 2 :phase-mask []})] "
        "    [(:phase-mask r1) (:enabled r1) (:phase-mask r2)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(3, vector_count(v));

    ID r1_mask = vector_nth(v, 0);
    TEST_ASSERT_TRUE(TAG(r1_mask) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *r1_mask_vec = as_vector(r1_mask);
    TEST_ASSERT_NOT_NULL(r1_mask_vec);
    TEST_ASSERT_EQUAL_UINT(1, vector_count(r1_mask_vec));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":stay"), vector_nth(r1_mask_vec, 0));
    TEST_ASSERT_EQUAL_PTR(clj_false, vector_nth(v, 1));

    ID r2_mask = vector_nth(v, 2);
    TEST_ASSERT_TRUE(TAG(r2_mask) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *r2_mask_vec = as_vector(r2_mask);
    TEST_ASSERT_NOT_NULL(r2_mask_vec);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(r2_mask_vec));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enter"), vector_nth(r2_mask_vec, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":exit"), vector_nth(r2_mask_vec, 1));
}

TEST(test_gfx_collision_contract_callback_set_clear_and_invoke_shape) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.collision) "
        "  (def collision-contract-cb (fn collision-contract-cb [] 1234)) "
        "  (let [assigned (tiny-gfx.collision/set-collision-callback! collision-contract-cb) "
        "        v1 (tiny-gfx.collision/invoke-collision-callback!) "
        "        _ (tiny-gfx.collision/set-collision-callback! nil) "
        "        v2 (tiny-gfx.collision/invoke-collision-callback!)] "
        "    [(fn? assigned) v1 (nil? v2)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(3, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 0));
    TEST_ASSERT_TRUE(is_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(1234, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 2));
}

TEST(test_gfx_collision_contract_callback_rejects_non_function_values) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.collision) "
        "  (let [_ (tiny-gfx.collision/set-collision-callback! nil) "
        "        rejected? (try (do (tiny-gfx.collision/set-collision-callback! 7) false) "
        "                       (catch Exception e true)) "
        "        v (tiny-gfx.collision/invoke-collision-callback!)] "
        "    (and rejected? (nil? v))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(out && out != clj_false);
}

TEST(test_gfx_collision_contract_demo_callback_mutates_scene_state_explicitly) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.host-viewer-demo) "
        "  (require 'tiny-gfx.collision) "
        "  (tiny-gfx.host-viewer-demo/create-demo-bundle) "
        "  (tiny-gfx.host-viewer-demo/configure-collision-toggle-callback!) "
        "  (let [before (:x1 (get (:root @tiny-gfx.host-viewer-demo/game-scene-state) 3002)) "
        "        ret (tiny-gfx.collision/invoke-collision-callback!) "
        "        after (:x1 (get (:root @tiny-gfx.host-viewer-demo/game-scene-state) 3002))] "
        "    [before after (nil? ret)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(3, vector_count(v));
    TEST_ASSERT_TRUE(is_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_TRUE(is_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(56, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(60, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 2));
}

TEST(test_gfx_collision_contract_runloop_dispatch_ignores_callback_return_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    event_loop_clear();

    ID dispatch_fn = eval_string(
        "(do "
        "  (require 'tiny-gfx.collision) "
        "  (def collision-contract-runloop-marker (atom 0)) "
        "  (tiny-gfx.collision/set-collision-callback! "
        "    (fn collision-contract-runloop-cb [] "
        "      (reset! collision-contract-runloop-marker 1) "
        "      777)) "
        "  tiny-gfx.collision/invoke-collision-callback!)",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(dispatch_fn);
    TEST_ASSERT_TRUE(TAG(dispatch_fn) == CLJ_FUNC || TAG(dispatch_fn) == CLJ_CLOSURE);

    event_loop_enqueue(RETAIN(dispatch_fn), NULL);
    TEST_ASSERT_TRUE(event_loop_has_pending_tasks());
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE(ran);
    TEST_ASSERT_FALSE(event_loop_has_pending_tasks());

    ID marker = eval_string("@collision-contract-runloop-marker", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(marker);
    TEST_ASSERT_TRUE(is_fixnum(marker));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(marker));

    ID direct = eval_string("(tiny-gfx.collision/invoke-collision-callback!)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(direct);
    TEST_ASSERT_TRUE(is_fixnum(direct));
    TEST_ASSERT_EQUAL_INT(777, as_fixnum(direct));

    ID clear_ok = eval_string("(tiny-gfx.collision/set-collision-callback! nil)", g_test_eval_state);
    TEST_ASSERT_NULL(clear_ok);
    event_loop_clear();
}

TEST(test_gfx_scene_update_nodes_applies_batched_changes) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.scene) "
        "  (let [tree {:id :root :children "
        "              [{:id :a :x 10 :y 20} "
        "               {:id :b :x 30 :y 40} "
        "               {:id :c :x 50 :y 60}]} "
        "        t2 (tiny-gfx.scene/update-nodes tree "
        "             {:a (fn [n] (assoc n :x 99)) "
        "              :c (fn [n] (assoc n :y 0))})] "
        "    [(:x (nth (:children t2) 0)) "
        "     (:y (nth (:children t2) 0)) "
        "     (:x (nth (:children t2) 1)) "
        "     (:y (nth (:children t2) 1)) "
        "     (:x (nth (:children t2) 2)) "
        "     (:y (nth (:children t2) 2))]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(6, vector_count(v));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(40, as_fixnum(vector_nth(v, 3)));
    TEST_ASSERT_EQUAL_INT(50, as_fixnum(vector_nth(v, 4)));
    TEST_ASSERT_EQUAL_INT( 0, as_fixnum(vector_nth(v, 5)));
}

TEST(test_gfx_scene_update_nodes_empty_updates_returns_unchanged) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.scene) "
        "  (let [tree {:id :root :x 5} "
        "        t2 (tiny-gfx.scene/update-nodes tree {})] "
        "    (:x t2)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(is_fixnum(out));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(out));
}

TEST(test_gfx_scene_update_nodes_nested_groups) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.scene) "
        "  (let [tree {:id :root :children "
        "              [{:id :g1 :children "
        "                [{:id :deep :val 1}]} "
        "               {:id :leaf :val 2}]} "
        "        t2 (tiny-gfx.scene/update-nodes tree "
        "             {:deep (fn [n] (assoc n :val 100)) "
        "              :leaf (fn [n] (assoc n :val 200))})] "
        "    [(:val (first (:children (first (:children t2))))) "
        "     (:val (nth (:children t2) 1))]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(v));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(200, as_fixnum(vector_nth(v, 1)));
}

TEST(test_gfx_scene_web_hex_color_conversion) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.scene) "
        "  [(tiny-gfx.scene/web-hex->color \"#00FFFF\") "
        "   (tiny-gfx.scene/web-hex->color \"#FFFFFF\") "
        "   (tiny-gfx.scene/web-hex->color \"#FF00FF\") "
        "   (tiny-gfx.scene/web-hex->color \"#FF0000\")])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(4, vector_count(v));
    TEST_ASSERT_EQUAL_INT(2047, as_fixnum(vector_nth(v, 0)));  // cyan
    TEST_ASSERT_EQUAL_INT(65535, as_fixnum(vector_nth(v, 1))); // white
    TEST_ASSERT_EQUAL_INT(63519, as_fixnum(vector_nth(v, 2))); // magenta
    TEST_ASSERT_EQUAL_INT(63488, as_fixnum(vector_nth(v, 3))); // red
}

TEST(test_gfx_scene_rgb888_int_color_conversion) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.scene) "
        "  [(tiny-gfx.scene/color 0x00FFFF) "
        "   (tiny-gfx.scene/color 0xFFFFFF) "
        "   (tiny-gfx.scene/color 0xFF00FF) "
        "   (tiny-gfx.scene/color 0xFF0000)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(4, vector_count(v));
    TEST_ASSERT_EQUAL_INT(2047, as_fixnum(vector_nth(v, 0)));  // cyan
    TEST_ASSERT_EQUAL_INT(65535, as_fixnum(vector_nth(v, 1))); // white
    TEST_ASSERT_EQUAL_INT(63519, as_fixnum(vector_nth(v, 2))); // magenta
    TEST_ASSERT_EQUAL_INT(63488, as_fixnum(vector_nth(v, 3))); // red
}

TEST(test_gfx_scene_rgb888_int_color_invalid_input) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.scene) "
        "  [(tiny-gfx.scene/color nil) "
        "   (tiny-gfx.scene/color -1) "
        "   (tiny-gfx.scene/color 16777216) "
        "   (tiny-gfx.scene/color 3.14)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(4, vector_count(v));
    TEST_ASSERT_NULL(vector_nth(v, 0));
    TEST_ASSERT_NULL(vector_nth(v, 1));
    TEST_ASSERT_NULL(vector_nth(v, 2));
    TEST_ASSERT_NULL(vector_nth(v, 3));
}

TEST(test_gfx_scene_web_hex_color_invalid_input) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-gfx.scene) "
        "  [(tiny-gfx.scene/web-hex->color nil) "
        "   (tiny-gfx.scene/web-hex->color \"\") "
        "   (tiny-gfx.scene/web-hex->color \"#FFF\") "
        "   (tiny-gfx.scene/web-hex->color \"#GG00FF\")])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(4, vector_count(v));
    TEST_ASSERT_NULL(vector_nth(v, 0));
    TEST_ASSERT_NULL(vector_nth(v, 1));
    TEST_ASSERT_NULL(vector_nth(v, 2));
    TEST_ASSERT_NULL(vector_nth(v, 3));
}
