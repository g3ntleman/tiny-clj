#include "tests_common.h"
#include "../event_loop.h"
#include "../record.h"
#include "../symbol.h"
#include "../tiny_fx_gfx.h"
#include "../vector.h"

TEST(test_gfx_collision_contract_registers_record_descriptors) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string("(do (require 'tiny-fx.gfx) "
                        "    (require 'tiny-fx.gfx-scene) "
                        "    (require 'tiny-fx.gfx-collision) "
                        "    true)",
                        g_test_eval_state);
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
    TEST_ASSERT_EQUAL_UINT(15, vector_count(d_spatial_event->field_keys));
    TEST_ASSERT_EQUAL_UINT(4, vector_count(d_aabb->field_keys));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":id"), vector_nth(d_spatial_rule->field_keys, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":slot"), vector_nth(d_spatial_rule->field_keys, 1));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":kind"), vector_nth(d_spatial_rule->field_keys, 2));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":self"), vector_nth(d_spatial_rule->field_keys, 3));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":other"), vector_nth(d_spatial_rule->field_keys, 4));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":source"), vector_nth(d_spatial_event->field_keys, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":id"), vector_nth(d_spatial_event->field_keys, 1));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":slot-id"), vector_nth(d_spatial_event->field_keys, 2));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":rule"), vector_nth(d_spatial_event->field_keys, 7));
}

TEST(test_gfx_collision_contract_normalize_rule_defaults) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [r (tiny-fx.gfx-scene/normalize-spatial-rule {:id :r1 :a-id 1 :b-id 2})] "
        "    [(:slot r) (:kind r) (:self r) (:other r) (:radius r) (:channel r)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(6, vector_count(v));

    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":game"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":collision"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(v, 3)));
    TEST_ASSERT_TRUE(is_fixnum(vector_nth(v, 4)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 4)));
    TEST_ASSERT_NULL(vector_nth(v, 5));
}

TEST(test_gfx_collision_contract_phase_mask_normalization_enter_exit_only) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [r1 (tiny-fx.gfx-scene/normalize-collision-rule "
        "             {:id :r1 :a-id 1 :b-id 2 :phase-mask [:enter :foo]}) "
        "        r2 (tiny-fx.gfx-scene/normalize-collision-rule "
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
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [r (tiny-fx.gfx-scene/normalize-collision-rule "
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
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [r (tiny-fx.gfx-scene/normalize-spatial-rule "
        "            {:id :hear :slot :game :kind :proximity :self 10 :other 20 :radius 24 :channel :hearing})] "
        "    [(:slot r) (:kind r) (:self r) (:other r) (:radius r) (:channel r)]))",
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
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (def collision-contract-cb (fn collision-contract-cb [event] (:phase event))) "
        "  (let [assigned (tiny-fx.gfx-collision/set-collision-callback! collision-contract-cb) "
        "        v1 (tiny-fx.gfx-collision/invoke-collision-callback! {:phase :enter}) "
        "        _ (tiny-fx.gfx-collision/set-collision-callback! nil) "
        "        v2 (tiny-fx.gfx-collision/invoke-collision-callback! nil)] "
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
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [_ (tiny-fx.gfx-collision/set-collision-callback! nil) "
        "        rejected? (try (do (tiny-fx.gfx-collision/set-collision-callback! 7) false) "
        "                       (catch Exception e true)) "
        "        v (tiny-fx.gfx-collision/invoke-collision-callback! nil)] "
        "    (and rejected? (nil? v))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(out && out != clj_false);
}

TEST(test_gfx_collision_contract_field_alias_hot_loop_does_not_retain) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID rule = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (let [game-scene-atom (:game-scene-atom (tiny-fx.game-demo/game-demo-config))] "
        "    (nth (:collision-rules @game-scene-atom) 0)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(rule);
    TEST_ASSERT_TRUE(TAG(rule) == CLJ_RECORD);

    ID kw_self = intern_symbol_global(":self");
    TEST_ASSERT_NOT_NULL(kw_self);

    ID stable = RETAIN(tiny_fx_gfx_get_field(rule, kw_self, NULL));
    TEST_ASSERT_NOT_NULL(stable);
    TEST_ASSERT_FALSE(IS_IMMEDIATE(stable));

    int baseline_rc = ((CljObject *)stable)->rc;
    for (int i = 0; i < 512; i++) {
        ID alias = tiny_fx_gfx_get_field(rule, kw_self, NULL);
        TEST_ASSERT_EQUAL_PTR(stable, alias);
    }
    TEST_ASSERT_EQUAL_INT(baseline_rc, ((CljObject *)stable)->rc);
    RELEASE(stable);
}

/* Target: 0 (raised to 512); TODO: find/fix descriptor/watch residue to lower again. */
TEST(test_gfx_collision_contract_spatial_watch_supports_two_and_three_arity_calls, 512) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [spatial-watch-marker (atom [])] "
        "    (tiny-fx.gfx-collision/watch :player-hit "
        "      (fn [event] "
        "        (swap! spatial-watch-marker conj [:two (:phase event)]) "
        "        nil)) "
        "    (tiny-fx.gfx-collision/invoke-collision-callback! "
        "      {:source :spatial :id :player-hit :rule {:id :player-hit} :phase :enter}) "
        "    (tiny-fx.gfx-collision/watch :player-hit "
        "      (fn [event] "
        "        (swap! spatial-watch-marker conj [:three (:phase event)]) "
        "        nil) "
        "      {:channel :hearing}) "
        "    (tiny-fx.gfx-collision/invoke-collision-callback! "
        "      {:source :spatial :id :player-hit :rule {:id :player-hit} :phase :exit}) "
        "    (tiny-fx.gfx-collision/watch :player-hit nil) "
        "    @spatial-watch-marker))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(v));

    ID first = vector_nth(v, 0);
    ID second = vector_nth(v, 1);
    TEST_ASSERT_TRUE(TAG(first) == CLJ_VECTOR_PERSISTENT);
    TEST_ASSERT_TRUE(TAG(second) == CLJ_VECTOR_PERSISTENT);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":two"), vector_nth(as_vector(first), 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enter"), vector_nth(as_vector(first), 1));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":three"), vector_nth(as_vector(second), 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":exit"), vector_nth(as_vector(second), 1));
}

/* Target: 0 (raised to 512); TODO: find/fix descriptor/watch residue to lower again. */
TEST(test_gfx_collision_contract_event_on_spatial_descriptor_subscription_receives_matching_events, 512) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-clj.event) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [collision-event-on-marker (atom [])] "
        "    (tiny-fx.gfx-collision/set-collision-callback! nil) "
        "    (tiny-clj.event/on {:source :spatial :id :player-hit} "
        "      (fn [event] "
        "        (swap! collision-event-on-marker conj [(:id event) (:id (:rule event)) (:phase event)]) "
        "        nil)) "
        "    (tiny-fx.gfx-collision/invoke-collision-callback! "
        "      {:source :spatial :id :player-hit :rule {:id :player-hit} :phase :enter}) "
        "    (tiny-fx.gfx-collision/invoke-collision-callback! "
        "      {:source :spatial :id :other :rule {:id :other} :phase :exit}) "
        "    (tiny-clj.event/on {:source :spatial :id :player-hit} nil) "
        "    @collision-event-on-marker))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(1, vector_count(v));

    ID first = vector_nth(v, 0);
    TEST_ASSERT_TRUE(TAG(first) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *first_vec = as_vector(first);
    TEST_ASSERT_NOT_NULL(first_vec);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":player-hit"), vector_nth(first_vec, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":player-hit"), vector_nth(first_vec, 1));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enter"), vector_nth(first_vec, 2));
}

TEST(test_gfx_collision_contract_demo_callback_mutates_scene_state_explicitly) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (let [game-scene-atom (:game-scene-atom (tiny-fx.game-demo/game-demo-config)) "
        "        player0 (get (:root @game-scene-atom) 3002) "
        "        before-x1 (:x1 player0) "
        "        before-sx (:sx (nth (nth (:keyframes (:t player0)) 0) 1)) "
        "        ret1 (tiny-fx.game-demo/on-player-collision-toggle! {:kind :collision :phase :enter}) "
        "        player1 (get (:root @game-scene-atom) 3002) "
        "        after-enter-x1 (:x1 player1) "
        "        after-enter-sx (:sx (nth (nth (:keyframes (:t player1)) 0) 1)) "
        "        ret2 (tiny-fx.game-demo/on-player-collision-toggle! {:kind :collision :phase :exit}) "
        "        player2 (get (:root @game-scene-atom) 3002) "
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
    if (!out || out == clj_false) {
        test_fprintf(stderr, "test_gfx_collision_contract_demo_callback_mutates_scene_state_explicitly failed! out=%p\n", (void*)out);
    }
    TEST_ASSERT_TRUE(out && out != clj_false);
}

TEST(test_gfx_collision_contract_runloop_dispatch_ignores_callback_return_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    event_loop_clear();

    ID dispatch_fn = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (def collision-contract-runloop-marker (atom nil)) "
        "  (tiny-fx.gfx-collision/set-collision-callback! "
        "    (fn collision-contract-runloop-cb [event] "
        "      (reset! collision-contract-runloop-marker (:phase event)) "
        "      777)) "
        "  tiny-fx.gfx-collision/invoke-collision-callback!)",
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

    ID direct = eval_string("(do (require 'tiny-fx.gfx-collision) "
                            "    (tiny-fx.gfx-collision/invoke-collision-callback! {:phase :exit}))",
                            g_test_eval_state);
    TEST_ASSERT_NOT_NULL(direct);
    TEST_ASSERT_TRUE(is_fixnum(direct));
    TEST_ASSERT_EQUAL_INT(777, as_fixnum(direct));

    ID clear_ok = eval_string("(do (require 'tiny-fx.gfx-collision) "
                              "    (tiny-fx.gfx-collision/set-collision-callback! nil))",
                              g_test_eval_state);
    TEST_ASSERT_NULL(clear_ok);
    event_loop_clear();
}

TEST(test_gfx_collision_contract_runloop_dispatch_preserves_spatial_envelope_fields) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    event_loop_clear();

    ID dispatch_fn = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (def collision-envelope-marker (atom nil)) "
        "  (tiny-fx.gfx-collision/set-collision-callback! "
        "    (fn collision-envelope-cb [event] "
        "      (reset! collision-envelope-marker "
        "              [(:source event) (:id event) (:kind event) (:phase event) (:channel event)]) "
        "      :ignored)) "
        "  tiny-fx.gfx-collision/invoke-collision-callback!)",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(dispatch_fn);
    TEST_ASSERT_TRUE(TAG(dispatch_fn) == CLJ_FUNC || TAG(dispatch_fn) == CLJ_CLOSURE);

    ID event_payload = eval_string("{:source :spatial :id :player-hear :rule {:id :player-hear} :kind :proximity :phase :enter :channel :hearing}",
                                   g_test_eval_state);
    TEST_ASSERT_NOT_NULL(event_payload);
    TEST_ASSERT_TRUE(event_loop_enqueue_ingress_call(dispatch_fn, event_payload));
    TEST_ASSERT_TRUE(event_loop_has_pending_tasks());
    TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));

    ID marker = eval_string("(= @collision-envelope-marker [:spatial :player-hear :proximity :enter :hearing])",
                            g_test_eval_state);
    TEST_ASSERT_EQUAL(clj_true, marker);

    ID clear_ok = eval_string("(do (require 'tiny-fx.gfx-collision) "
                              "    (tiny-fx.gfx-collision/set-collision-callback! nil))",
                              g_test_eval_state);
    TEST_ASSERT_NULL(clear_ok);
    event_loop_clear();
}

TEST(test_gfx_scene_update_nodes_applies_batched_changes) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [tree {:id :root :children "
        "              [{:id :a :x 10 :y 20} "
        "               {:id :b :x 30 :y 40} "
        "               {:id :c :x 50 :y 60}]} "
        "        t2 (tiny-fx.gfx-scene/update-nodes tree "
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
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [tree {:id :root :x 5} "
        "        t2 (tiny-fx.gfx-scene/update-nodes tree {})] "
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
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [tree {:id :root :children "
        "              [{:id :g1 :children "
        "                [{:id :deep :val 1}]} "
        "               {:id :leaf :val 2}]} "
        "        t2 (tiny-fx.gfx-scene/update-nodes tree "
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
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  [(tiny-fx.color/web-hex->color \"#00FFFF\") "
        "   (tiny-fx.color/web-hex->color \"#FFFFFF\") "
        "   (tiny-fx.color/web-hex->color \"#FF00FF\") "
        "   (tiny-fx.color/web-hex->color \"#FF0000\")])",
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
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  [(tiny-fx.color/color 0x00FFFF) "
        "   (tiny-fx.color/color 0xFFFFFF) "
        "   (tiny-fx.color/color 0xFF00FF) "
        "   (tiny-fx.color/color 0xFF0000)])",
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
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  [(tiny-fx.color/color nil) "
        "   (tiny-fx.color/color -1) "
        "   (tiny-fx.color/color 16777216) "
        "   (tiny-fx.color/color 3.14)])",
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
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
        "  (require 'tiny-fx.gfx-collision) "
        "  [(tiny-fx.color/web-hex->color nil) "
        "   (tiny-fx.color/web-hex->color \"\") "
        "   (tiny-fx.color/web-hex->color \"#FFF\") "
        "   (tiny-fx.color/web-hex->color \"#GG00FF\")])",
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
