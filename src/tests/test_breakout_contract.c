#include "tests_common.h"

TEST(test_breakout_contract_namespaces_load) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (require 'tiny-breakout.audio) "
        "  (require 'tiny-breakout.levels) "
        "  (require 'tiny-breakout.runtime) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_core_input_flow_title_to_play_creates_segment) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (tiny-breakout.core/init-state) "
        "        s1 (tiny-breakout.core/apply-input s0 {:launch true} 100 nil) "
        "        s2 (tiny-breakout.core/apply-input s1 {:launch true} 200 nil) "
        "        seg (:ball-segment s2)] "
        "    [(:phase s0) (:phase s1) (:phase s2) (map? seg) (:start-ms seg) (:end-ms seg)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(6, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":title"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":play"), vector_nth(v, 2));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 3));
    TEST_ASSERT_TRUE(as_fixnum(vector_nth(v, 4)) >= 200);
    TEST_ASSERT_TRUE(as_fixnum(vector_nth(v, 5)) > as_fixnum(vector_nth(v, 4)));
}

TEST(test_breakout_contract_paddle_clamps_at_bounds) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) :phase :serve :paddle-x 0) "
        "        s1 (tiny-breakout.core/apply-input s0 {:dx -1} 16 nil) "
        "        s2 (assoc s0 :paddle-x 280) "
        "        s3 (tiny-breakout.core/apply-input s2 {:dx 8} 16 nil)] "
        "    [(:paddle-x s1) (:paddle-x s3)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(280, as_fixnum(vector_nth(v, 1)));
}

TEST(test_breakout_contract_serve_keeps_ball_attached_to_paddle) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (tiny-breakout.core/apply-input (tiny-breakout.core/init-state) {:launch true} 16 nil) "
        "        s1 (tiny-breakout.core/apply-input (assoc s0 :paddle-x 120) {:dx 0} 16 nil)] "
        "    [(:phase s1) (:ball-x s1) (:ball-y s1) (:ball-segment s1)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_INT(140, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(218, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_TRUE(vector_nth(v, 3) == NULL);
}

TEST(test_breakout_contract_segment_end_bottom_out_decrements_life_and_enters_serve) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) "
        "                  :phase :play :lives 3 "
        "                  :levels [{:id :l1 :bricks []}] "
        "                  :bricks [] "
        "                  :ball-segment {:id 7 :end-ms 500 :to-x 100 :to-y 241 :wall :bottom}) "
        "        s1 (tiny-breakout.core/apply-segment-end s0 7)] "
        "    [(:lives s1) (:phase s1) (:ball-y s1)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_INT(218, as_fixnum(vector_nth(v, 2)));
}

TEST(test_breakout_contract_paddle_collision_reanchors_ball_and_replans_segment) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) "
        "                  :phase :play :paddle-x 120 :ball-vx 2 :ball-vy 2 "
        "                  :ball-segment {:id 1 :start-ms 10 :end-ms 40 :to-x 150 :to-y 220 :wall :bottom}) "
        "        ev {:id :ball-vs-paddle "
        "            :phase :enter "
        "            :self-aabb {:min-x 138 :min-y 220 :max-x 142 :max-y 224} "
        "            :other-aabb {:min-x 120 :min-y 224 :max-x 160 :max-y 228}} "
        "        s1 (tiny-breakout.core/apply-spatial-event s0 ev 300)] "
        "    (and (= :play (:phase s1)) "
        "         (< (:ball-vy s1) 0) "
        "         (= 220 (:ball-y s1)) "
        "         (= :paddle-hit (first (:events s1))) "
        "         (map? (:ball-segment s1)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_brick_collision_removes_brick_scores_and_can_win) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [brick {:id 2001 :x 50 :y 50 :w 20 :h 10 :points 10} "
        "        s0 (-> (tiny-breakout.core/init-state) "
        "               (assoc :phase :play) "
        "               (assoc :score 0) "
        "               (assoc :levels [{:id :only :bricks [brick]}]) "
        "               (assoc :level-index 0) "
        "               (assoc :bricks [brick]) "
        "               (assoc :ball-vx 0) "
        "               (assoc :ball-vy 2) "
        "               (assoc :ball-segment {:id 1 :start-ms 10 :end-ms 40 :to-x 50 :to-y 55 :wall :bottom})) "
        "        ev {:id :ball-vs-brick "
        "            :phase :enter "
        "            :other 2001 "
        "            :self-aabb {:min-x 50 :min-y 49 :max-x 54 :max-y 53} "
        "            :other-aabb {:min-x 50 :min-y 50 :max-x 70 :max-y 60}} "
        "        s1 (tiny-breakout.core/apply-spatial-event s0 ev 300)] "
        "    [(:score s1) (count (:bricks s1)) (:phase s1) (first (:events s1))]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":victory"), vector_nth(v, 2));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":brick-hit"), vector_nth(v, 3));
}

TEST(test_breakout_contract_pause_toggle_anchors_rendered_ball_and_resumes_segment) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) "
        "                  :phase :play "
        "                  :ball-segment {:id 2 :start-ms 10 :end-ms 100 :to-x 70 :to-y 80 :wall :top}) "
        "        s1 (tiny-breakout.core/apply-input s0 {:pause true} 40 {:x 33 :y 44}) "
        "        s2 (tiny-breakout.core/apply-input s1 {:pause true} 60 nil)] "
        "    [(:phase s1) (:ball-x s1) (:ball-y s1) (:ball-segment s1) (:phase s2) (map? (:ball-segment s2))]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":pause"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_INT(33, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(44, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_TRUE(vector_nth(v, 3) == NULL);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":play"), vector_nth(v, 4));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 5));
}

TEST(test_breakout_contract_runtime_input_supports_digital_and_rotary) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/reset-runtime!) "
        "  (let [x0 (:paddle-x @tiny-breakout.runtime/state*) "
        "        _ (tiny-breakout.runtime/apply-input! {:left true}) "
        "        x1 (:paddle-x @tiny-breakout.runtime/state*) "
        "        _ (tiny-breakout.runtime/reset-runtime!) "
        "        y0 (:paddle-x @tiny-breakout.runtime/state*) "
        "        _ (tiny-breakout.runtime/apply-input! {:rotary-delta -1}) "
        "        y1 (:paddle-x @tiny-breakout.runtime/state*)] "
        "    [(- x1 x0) (- y1 y0)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(v));
    TEST_ASSERT_EQUAL_INT(-4, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(-4, as_fixnum(vector_nth(v, 1)));
}

TEST(test_breakout_contract_scene_build_returns_entity_map_root_with_spatial_rules) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [brick {:id 2001 :x 40 :y 32 :w 20 :h 10 :points 10} "
        "        s (assoc (tiny-breakout.core/init-state) :bricks [brick]) "
        "        frame (tiny-breakout.scene/build-scene s) "
        "        root (:root frame) "
        "        index (:index frame) "
        "        rules (:collision-rules frame) "
        "        paddle (get index 1002) "
        "        ball (get index 1003) "
        "        brick-node (get index 2001) "
        "        paddle-rule (nth rules 0) "
        "        brick-rule (nth rules 1)] "
        "    (and (= true (:visible frame)) "
        "         (= 1000 (:id root)) "
        "         (map? index) "
        "         (= [1001 1002 1003 1004 1005 2001] (:children root)) "
        "         (= 2 (count rules)) "
        "         (= :ball-vs-paddle (:id paddle-rule)) "
        "         (= :ball-vs-brick (:id brick-rule)) "
        "         (= 1003 (:self paddle-rule)) "
        "         (= 1002 (:other paddle-rule)) "
        "         (= 1003 (:self brick-rule)) "
        "         (= 2001 (:other brick-rule)) "
        "         (= :breakout/ball (:id (:prototype ball))) "
        "         (= :breakout/paddle (:id (:prototype paddle))) "
        "         (= :breakout/brick (:id (:prototype brick-node))))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_scene_ball_segment_projects_absolute_timelines) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [s (assoc (tiny-breakout.core/init-state) "
        "                 :phase :play "
        "                 :ball-x 10 :ball-y 20 "
        "                 :ball-segment {:id 4 :start-ms 123 :end-ms 234 :to-x 44 :to-y 66 :wall :right}) "
        "        frame (tiny-breakout.scene/build-scene s) "
        "        ball (get (:index frame) 1003)] "
        "    (and (= [[123 10] [234 44]] (:keyframes (:x ball))) "
        "         (= [[123 20] [234 66]] (:keyframes (:y ball))))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_scene_uses_exact_terminal_overlay_texts) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [lose (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :game-over)) "
        "        win (tiny-breakout.scene/build-scene (assoc (tiny-breakout.core/init-state) :phase :victory)) "
        "        lose-overlay (get (:index lose) 1005) "
        "        win-overlay (get (:index win) 1005)] "
        "    (and (= \"Game Over\" (:text lose-overlay)) "
        "         (= \"You win!\" (:text win-overlay)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_config_exposes_viewer_slots_and_atoms_without_native_runtime_flag) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config) "
        "        slots (:slots cfg) "
        "        game-atom (:game-scene-atom cfg)] "
        "    (and (= :tiny-breakout (:entry cfg)) "
        "         (nil? (:host-runtime cfg)) "
        "         (= 1 (count slots)) "
        "         (atom? game-atom) "
        "         (fn? (:spatial-callback cfg)) "
        "         (= game-atom (:atom (first slots))) "
        "         (= true (:visible (deref game-atom))))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_runtime_apply_input_mutates_state_discretely) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-breakout.runtime) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config) "
        "        s0 @tiny-breakout.runtime/state* "
        "        _ (tiny-breakout.runtime/apply-input! {:launch true}) "
        "        s1 @tiny-breakout.runtime/state* "
        "        _ (tiny-breakout.runtime/apply-input! {:right true}) "
        "        s2 @tiny-breakout.runtime/state* "
        "        scene @(:game-scene-atom cfg)] "
        "    (and (= :title (:phase s0)) "
        "         (= :serve (:phase s1)) "
        "         (> (:paddle-x s2) (:paddle-x s1)) "
        "         (= true (:visible scene)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_spatial_callback_dispatches_generic_spatial_watchers) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.event) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config) "
        "        seen (atom []) "
        "        _ (tiny-clj.event/on {:source :spatial :id :ball-vs-paddle} "
        "            (fn [event] "
        "              (swap! seen conj [(:id event) (:phase event)]) "
        "              nil)) "
        "        _ ((:spatial-callback cfg) "
        "            {:source :spatial :id :ball-vs-paddle :rule {:id :ball-vs-paddle} "
        "             :phase :enter "
        "             :self-aabb {:min-x 10 :min-y 20 :max-x 14 :max-y 24} "
        "             :other-aabb {:min-x 0 :min-y 24 :max-x 40 :max-y 28}}) "
        "        _ (tiny-clj.event/on {:source :spatial :id :ball-vs-paddle} nil)] "
        "    (= [[:ball-vs-paddle :enter]] @seen)))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}
