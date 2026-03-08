#include "tests_common.h"
#include "../event_loop.h"
#include "../record.h"
#include "../symbol.h"
#include "../vector.h"

TEST(test_gfx_collision_contract_registers_record_descriptors) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string("(do (require 'tiny-fx.gfx) true)", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);

    ID t_rule = intern_symbol_global("CollisionRule");
    ID t_event = intern_symbol_global("CollisionEvent");
    ID t_spatial_rule = intern_symbol_global("SpatialRule");
    ID t_spatial_event = intern_symbol_global("SpatialEvent");
    ID t_aabb = intern_symbol_global("Aabb");
    CljRecordDescriptor *d_rule = record_descriptor_lookup(t_rule);
    CljRecordDescriptor *d_event = record_descriptor_lookup(t_event);
    CljRecordDescriptor *d_spatial_rule = record_descriptor_lookup(t_spatial_rule);
    CljRecordDescriptor *d_spatial_event = record_descriptor_lookup(t_spatial_event);
    CljRecordDescriptor *d_aabb = record_descriptor_lookup(t_aabb);
    TEST_ASSERT_NOT_NULL(d_rule);
    TEST_ASSERT_NOT_NULL(d_event);
    TEST_ASSERT_NOT_NULL(d_spatial_rule);
    TEST_ASSERT_NOT_NULL(d_spatial_event);
    TEST_ASSERT_NOT_NULL(d_aabb);
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
    TEST_ASSERT_EQUAL_UINT(7, vector_count(d_spatial_rule->field_keys));
    TEST_ASSERT_EQUAL_UINT(12, vector_count(d_spatial_event->field_keys));
    TEST_ASSERT_EQUAL_UINT(4, vector_count(d_aabb->field_keys));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":source"), vector_nth(d_spatial_event->field_keys, 0));
}

TEST(test_gfx_collision_contract_normalize_rule_defaults) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (let [r (tiny-fx.gfx/normalize-spatial-rule {:id :r1 :a-id 1 :b-id 2})] "
        "    [(:slot r) (:kind r) (:radius r) (:channel r)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(4, vector_count(v));

    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":game"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":collision"), vector_nth(v, 1));
    TEST_ASSERT_TRUE(is_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_NULL(vector_nth(v, 3));
}

TEST(test_gfx_collision_contract_phase_mask_normalization_enter_exit_only) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (let [r1 (tiny-fx.gfx/normalize-collision-rule "
        "             {:id :r1 :a-id 1 :b-id 2 :phase-mask [:enter :foo]}) "
        "        r2 (tiny-fx.gfx/normalize-collision-rule "
        "             {:id :r2 :a-id 1 :b-id 2 :phase-mask []})] "
        "    [(:phase-mask r1) (:phase-mask r2)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(v));

    ID r1_mask = vector_nth(v, 0);
    TEST_ASSERT_TRUE(TAG(r1_mask) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *r1_mask_vec = as_vector(r1_mask);
    TEST_ASSERT_NOT_NULL(r1_mask_vec);
    TEST_ASSERT_EQUAL_UINT(1, vector_count(r1_mask_vec));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enter"), vector_nth(r1_mask_vec, 0));

    ID r2_mask = vector_nth(v, 1);
    TEST_ASSERT_TRUE(TAG(r2_mask) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *r2_mask_vec = as_vector(r2_mask);
    TEST_ASSERT_NOT_NULL(r2_mask_vec);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(r2_mask_vec));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enter"), vector_nth(r2_mask_vec, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":exit"), vector_nth(r2_mask_vec, 1));
}

TEST(test_gfx_collision_contract_disabled_rule_defaults_to_no_runtime_side_effects) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (let [r (tiny-fx.gfx/normalize-collision-rule "
        "            {:id :r-disabled :a-id 7 :b-id 9 :enabled false})] "
        "    [(:slot r) (:enabled r) (:cooldown-ms r) (:phase-mask r)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(4, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":game"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(clj_false, vector_nth(v, 1));
    TEST_ASSERT_TRUE(is_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 2)));

    ID mask = vector_nth(v, 3);
    TEST_ASSERT_TRUE(TAG(mask) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *mask_vec = as_vector(mask);
    TEST_ASSERT_NOT_NULL(mask_vec);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(mask_vec));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enter"), vector_nth(mask_vec, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":exit"), vector_nth(mask_vec, 1));
}

TEST(test_gfx_collision_contract_normalize_spatial_rule_preserves_proximity_fields) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (let [r (tiny-fx.gfx/normalize-spatial-rule "
        "            {:id :hear :slot :game :kind :proximity :a-id 10 :b-id 20 :radius 24 :channel :hearing})] "
        "    [(:slot r) (:kind r) (:a-id r) (:b-id r) (:radius r) (:channel r)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(6, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":game"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":proximity"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(vector_nth(v, 3)));
    TEST_ASSERT_EQUAL_INT(24, as_fixnum(vector_nth(v, 4)));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":hearing"), vector_nth(v, 5));
}

TEST(test_gfx_collision_contract_callback_set_clear_and_invoke_shape) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (def collision-contract-cb (fn collision-contract-cb [event] (:phase event))) "
        "  (let [assigned (tiny-fx.gfx/set-collision-callback! collision-contract-cb) "
        "        v1 (tiny-fx.gfx/invoke-collision-callback! {:phase :enter}) "
        "        _ (tiny-fx.gfx/set-collision-callback! nil) "
        "        v2 (tiny-fx.gfx/invoke-collision-callback! nil)] "
        "    [(fn? assigned) v1 (nil? v2)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(3, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enter"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 2));
}

TEST(test_gfx_collision_contract_callback_rejects_non_function_values) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (let [_ (tiny-fx.gfx/set-collision-callback! nil) "
        "        rejected? (try (do (tiny-fx.gfx/set-collision-callback! 7) false) "
        "                       (catch Exception e true)) "
        "        v (tiny-fx.gfx/invoke-collision-callback! nil)] "
        "    (and rejected? (nil? v))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(out && out != clj_false);
}

TEST(test_gfx_collision_contract_demo_callback_mutates_scene_state_explicitly) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (require 'tiny-fx.gfx) "
        "  (tiny-fx.game-demo/create-demo-bundle) "
        "  (tiny-fx.game-demo/configure-collision-toggle-callback!) "
        "  (let [player0 (get (:root @tiny-fx.game-demo/game-scene-state) 3002) "
        "        before-x1 (:x1 player0) "
        "        before-sx (:sx (nth (nth (:keyframes (:t player0)) 0) 1)) "
        "        ret1 (tiny-fx.gfx/invoke-collision-callback! {:kind :collision :phase :enter}) "
        "        player1 (get (:root @tiny-fx.game-demo/game-scene-state) 3002) "
        "        after-enter-x1 (:x1 player1) "
        "        after-enter-sx (:sx (nth (nth (:keyframes (:t player1)) 0) 1)) "
        "        ret2 (tiny-fx.gfx/invoke-collision-callback! {:kind :collision :phase :exit}) "
        "        player2 (get (:root @tiny-fx.game-demo/game-scene-state) 3002) "
        "        after-exit-x1 (:x1 player2) "
        "        after-exit-sx (:sx (nth (nth (:keyframes (:t player2)) 0) 1))] "
        "    (and (= -16 before-x1) "
        "         (= -16 after-enter-x1) "
        "         (= -16 after-exit-x1) "
        "         (= 1 before-sx) "
        "         (= 0.75 after-enter-sx) "
        "         (= 1 after-exit-sx) "
        "         (nil? ret1) "
        "         (nil? ret2))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(out && out != clj_false);
}

TEST(test_gfx_collision_contract_runloop_dispatch_ignores_callback_return_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    event_loop_clear();

    ID dispatch_fn = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (def collision-contract-runloop-marker (atom nil)) "
        "  (tiny-fx.gfx/set-collision-callback! "
        "    (fn collision-contract-runloop-cb [event] "
        "      (reset! collision-contract-runloop-marker (:phase event)) "
        "      777)) "
        "  tiny-fx.gfx/invoke-collision-callback!)",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(dispatch_fn);
    TEST_ASSERT_TRUE(TAG(dispatch_fn) == CLJ_FUNC || TAG(dispatch_fn) == CLJ_CLOSURE);

    ID event_payload = eval_string("{:phase :enter}", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(event_payload);
    TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call(dispatch_fn, event_payload));
    TEST_ASSERT_TRUE(event_loop_has_pending_tasks());
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE(ran);
    TEST_ASSERT_FALSE(event_loop_has_pending_tasks());

    ID marker = eval_string("@collision-contract-runloop-marker", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(marker);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enter"), marker);

    ID direct = eval_string("(tiny-fx.gfx/invoke-collision-callback! {:phase :exit})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(direct);
    TEST_ASSERT_TRUE(is_fixnum(direct));
    TEST_ASSERT_EQUAL_INT(777, as_fixnum(direct));

    ID clear_ok = eval_string("(tiny-fx.gfx/set-collision-callback! nil)", g_test_eval_state);
    TEST_ASSERT_NULL(clear_ok);
    event_loop_clear();
}

TEST(test_gfx_collision_contract_runloop_dispatch_preserves_spatial_envelope_fields) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    event_loop_clear();

    ID dispatch_fn = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (def collision-envelope-marker (atom nil)) "
        "  (tiny-fx.gfx/set-collision-callback! "
        "    (fn collision-envelope-cb [event] "
        "      (reset! collision-envelope-marker "
        "              [(:source event) (:kind event) (:phase event) (:channel event)]) "
        "      :ignored)) "
        "  tiny-fx.gfx/invoke-collision-callback!)",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(dispatch_fn);
    TEST_ASSERT_TRUE(TAG(dispatch_fn) == CLJ_FUNC || TAG(dispatch_fn) == CLJ_CLOSURE);

    ID event_payload = eval_string("{:source :spatial :kind :proximity :phase :enter :channel :hearing}",
                                   g_test_eval_state);
    TEST_ASSERT_NOT_NULL(event_payload);
    TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call(dispatch_fn, event_payload));
    TEST_ASSERT_TRUE(event_loop_has_pending_tasks());
    TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));

    ID marker = eval_string("(= @collision-envelope-marker [:spatial :proximity :enter :hearing])",
                            g_test_eval_state);
    TEST_ASSERT_EQUAL(clj_true, marker);

    ID clear_ok = eval_string("(tiny-fx.gfx/set-collision-callback! nil)", g_test_eval_state);
    TEST_ASSERT_NULL(clear_ok);
    event_loop_clear();
}

TEST(test_gfx_scene_update_nodes_applies_batched_changes) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (let [tree {:id :root :children "
        "              [{:id :a :x 10 :y 20} "
        "               {:id :b :x 30 :y 40} "
        "               {:id :c :x 50 :y 60}]} "
        "        t2 (tiny-fx.gfx/update-nodes tree "
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
        "  (require 'tiny-fx.gfx) "
        "  (let [tree {:id :root :x 5} "
        "        t2 (tiny-fx.gfx/update-nodes tree {})] "
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
        "  (require 'tiny-fx.gfx) "
        "  (let [tree {:id :root :children "
        "              [{:id :g1 :children "
        "                [{:id :deep :val 1}]} "
        "               {:id :leaf :val 2}]} "
        "        t2 (tiny-fx.gfx/update-nodes tree "
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
        "  (require 'tiny-fx.gfx) "
        "  [(tiny-fx.gfx/web-hex->color \"#00FFFF\") "
        "   (tiny-fx.gfx/web-hex->color \"#FFFFFF\") "
        "   (tiny-fx.gfx/web-hex->color \"#FF00FF\") "
        "   (tiny-fx.gfx/web-hex->color \"#FF0000\")])",
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
        "  (require 'tiny-fx.gfx) "
        "  [(tiny-fx.gfx/color 0x00FFFF) "
        "   (tiny-fx.gfx/color 0xFFFFFF) "
        "   (tiny-fx.gfx/color 0xFF00FF) "
        "   (tiny-fx.gfx/color 0xFF0000)])",
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
        "  (require 'tiny-fx.gfx) "
        "  [(tiny-fx.gfx/color nil) "
        "   (tiny-fx.gfx/color -1) "
        "   (tiny-fx.gfx/color 16777216) "
        "   (tiny-fx.gfx/color 3.14)])",
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
        "  (require 'tiny-fx.gfx) "
        "  [(tiny-fx.gfx/web-hex->color nil) "
        "   (tiny-fx.gfx/web-hex->color \"\") "
        "   (tiny-fx.gfx/web-hex->color \"#FFF\") "
        "   (tiny-fx.gfx/web-hex->color \"#GG00FF\")])",
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
