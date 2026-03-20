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

TEST(test_breakout_contract_audio_events_resolve_to_playable_sfx) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.audio) "
        "  (let [cues (tiny-breakout.audio/events->cues [:brick-hit :level-clear :victory]) "
        "        played (tiny-breakout.audio/play-events! [:brick-hit :victory])] "
        "    (and (= [:sfx/brick-hit :sfx/level-clear :sfx/victory] cues) "
        "         (= 2 (count played)) "
        "         (map? (nth played 0)) "
        "         (contains? (nth played 0) :status) "
        "         (contains? (nth played 0) :duration-ms) "
        "         (map? (nth played 1)) "
        "         (contains? (nth played 1) :status) "
        "         (contains? (nth played 1) :duration-ms))))",
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
        "        seg (:ball-segment s1)] "
        "    [(:phase s0) (:phase s1) (map? seg) (:start-ms seg) (:end-ms seg)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(5, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":title"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":play"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, 2));
    TEST_ASSERT_TRUE(as_fixnum(vector_nth(v, 3)) >= 100);
    TEST_ASSERT_TRUE(as_fixnum(vector_nth(v, 4)) > as_fixnum(vector_nth(v, 3)));
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
        "  (let [s0 (assoc (tiny-breakout.core/init-state) :phase :serve :paddle-x 140) "
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
        "  (def breakout-test-x0 (:paddle-x @tiny-breakout.runtime/state*)) "
        "  (tiny-breakout.runtime/apply-input! {:left true}) "
        "  (def breakout-test-x1 (:paddle-x @tiny-breakout.runtime/state*)) "
        "  (tiny-breakout.runtime/reset-runtime!) "
        "  (def breakout-test-y0 (:paddle-x @tiny-breakout.runtime/state*)) "
        "  (tiny-breakout.runtime/apply-input! {:rotary-delta -1}) "
        "  (def breakout-test-y1 (:paddle-x @tiny-breakout.runtime/state*)) "
        "  [(- breakout-test-x1 breakout-test-x0) "
        "   (- breakout-test-y1 breakout-test-y0)])",
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
        "  (def breakout-test-brick {:id 2001 :x 40 :y 32 :w 20 :h 10 :points 10}) "
        "  (def breakout-test-state "
        "    (assoc (tiny-breakout.core/init-state) :bricks [breakout-test-brick])) "
        "  (def breakout-test-frame (tiny-breakout.scene/build-scene breakout-test-state)) "
        "  (def breakout-test-root-id (:root breakout-test-frame)) "
        "  (def breakout-test-index (:index breakout-test-frame)) "
        "  (def breakout-test-root (get breakout-test-index breakout-test-root-id)) "
        "  (def breakout-test-rules (:collision-rules breakout-test-frame)) "
        "  (def breakout-test-paddle (get breakout-test-index 1002)) "
        "  (def breakout-test-ball (get breakout-test-index 1003)) "
        "  (def breakout-test-lives-label (get breakout-test-index 1006)) "
        "  (def breakout-test-lives-value (get breakout-test-index 1007)) "
        "  (def breakout-test-brick-node (get breakout-test-index 2001)) "
        "  (def breakout-test-paddle-rule (nth breakout-test-rules 0)) "
        "  (def breakout-test-brick-rule (nth breakout-test-rules 1)) "
        "  (and (= true (:visible breakout-test-frame)) "
        "       (= 'root breakout-test-root-id) "
        "       (= 'root (:id breakout-test-root)) "
        "       (map? breakout-test-index) "
        "       (= [1001 1002 1003 1004 1005 1006 1007 2001] (:children breakout-test-root)) "
        "       (= 2 (count breakout-test-rules)) "
        "       (= :ball-vs-paddle (:id breakout-test-paddle-rule)) "
        "       (= :ball-vs-brick (:id breakout-test-brick-rule)) "
        "       (= 1003 (:self breakout-test-paddle-rule)) "
        "       (= 1002 (:other breakout-test-paddle-rule)) "
        "       (= 1003 (:self breakout-test-brick-rule)) "
        "       (= 2001 (:other breakout-test-brick-rule)) "
        "       (= \"Lives:\" (:text breakout-test-lives-label)) "
        "       (= \"3\" (:text breakout-test-lives-value)) "
        "       (= 226 (:x breakout-test-lives-label)) "
        "       (= 286 (:x breakout-test-lives-value)) "
        "       (= :breakout/ball (:id (:prototype breakout-test-ball))) "
        "       (= :breakout/paddle (:id (:prototype breakout-test-paddle))) "
        "       (= :breakout/brick (:id (:prototype breakout-test-brick-node)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_title_scene_hides_level_bricks_until_launch) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (def breakout-title-frame (tiny-breakout.scene/build-scene (tiny-breakout.core/init-state))) "
        "  (def breakout-title-index (:index breakout-title-frame)) "
        "  (def breakout-title-root (get breakout-title-index (:root breakout-title-frame))) "
        "  (def breakout-title-overlay (get breakout-title-index 1005)) "
        "  (and (= [1001 1002 1003 1004 1005 1006 1007] (:children breakout-title-root)) "
        "       (= \"Breakout\" (:text breakout-title-overlay)) "
        "       (= nil (get breakout-title-index 2001))))",
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
        "         (= :play (:phase s1)) "
        "         (> (:paddle-x s2) (:paddle-x s1)) "
        "         (= true (:visible scene)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_fire_button_simulation_reaches_breakout_runtime) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-clj.gpio/simulate! 13 1) "
        "  (tiny-clj.deployment/breakout-host-config) "
        "  (tiny-clj.gpio/simulate! 13 0) "
        "  (Thread/sleep 30) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (tiny-clj.gpio/simulate! 13 1) "
        "  (Thread/sleep 30) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (= :play (:phase @tiny-breakout.runtime/state*)))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_left_button_moves_until_button_up) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-clj.gpio/simulate! 14 1) "
        "  (tiny-clj.deployment/breakout-host-config) "
        "  (let [x0 (:paddle-x @tiny-breakout.runtime/state*) "
        "        _ (tiny-clj.gpio/simulate! 14 0) "
        "        _ (Thread/sleep 35) "
        "        _ (dotimes [_ 8] (run-next-task)) "
        "        moving-scene @tiny-breakout.runtime/scene* "
        "        moving-paddle (get (:index moving-scene) 1002) "
        "        moving-x (:x moving-paddle) "
        "        _ (Thread/sleep 70) "
        "        _ (tiny-clj.gpio/simulate! 14 1) "
        "        _ (Thread/sleep 35) "
        "        _ (dotimes [_ 8] (run-next-task)) "
        "        s1 @tiny-breakout.runtime/state* "
        "        stopped-scene @tiny-breakout.runtime/scene* "
        "        stopped-paddle (get (:index stopped-scene) 1002)] "
        "    (and (map? moving-x) "
        "         (contains? moving-x :keyframes) "
        "         (< (:paddle-x s1) (- x0 4)) "
        "         (number? (:x stopped-paddle)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_level_clear_stops_paddle_motion) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [s0 (-> (tiny-breakout.core/init-state) "
        "               (assoc :phase :play) "
        "               (assoc :level-index 0) "
        "               (assoc :bricks [{:id 2001 :x 10 :y 10 :w 32 :h 12 :points 7}]) "
        "               (assoc :paddle-x 120) "
        "               (assoc :paddle-motion {:dir 1 :start-ms 100 :end-ms 300 :to-x 280}) "
        "               (assoc :ball-x 10) "
        "               (assoc :ball-y 6) "
        "               (assoc :ball-vx 2) "
        "               (assoc :ball-vy 2) "
        "               (assoc :events [])) "
        "        event {:source :spatial :id :ball-vs-brick :rule {:id :ball-vs-brick} :phase :enter "
        "               :self-aabb {:min-x 10 :min-y 6 :max-x 14 :max-y 10} "
        "               :other 2001 "
        "               :other-aabb {:min-x 10 :min-y 10 :max-x 42 :max-y 22}} "
        "        s1 (tiny-breakout.core/apply-spatial-event s0 event 200) "
        "        frame (tiny-breakout.scene/build-scene s1) "
        "        paddle (get (:index frame) 1002)] "
        "    (and (= :level-clear (:phase s1)) "
        "         (nil? (:paddle-motion s1)) "
        "         (number? (:x paddle)))))",
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
