#include "test_breakout_helpers.h"

TEST(test_breakout_contract_namespaces_load) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (require 'tiny-breakout.input) "
        "  (require 'tiny-breakout.audio) "
        "  (require 'tiny-breakout.levels) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_core_state_flow_title_to_play) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (tiny-breakout.core/init-state) "
        "        s1 (tiny-breakout.core/step-state s0 {:dx 0 :launch true :pause false} 16) "
        "        s2 (tiny-breakout.core/step-state s1 {:dx 0 :launch true :pause false} 16)] "
        "    [(:phase s0) (:phase s1) (:phase s2)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(3, vector_count(v));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":title"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":play"), vector_nth(v, 2));
}

TEST(test_breakout_contract_paddle_clamps_at_bounds) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) :phase :serve :paddle-x 0) "
        "        s1 (tiny-breakout.core/step-state s0 {:dx -1} 16) "
        "        s2 (assoc s0 :paddle-x 280) "
        "        s3 (tiny-breakout.core/step-state s2 {:dx 8} 16)] "
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
        "  (let [s0 (tiny-breakout.core/step-state (tiny-breakout.core/init-state) {:launch true} 16) "
        "        s1 (tiny-breakout.core/step-state (assoc s0 :paddle-x 120) {:dx 0} 16)] "
        "    [(:phase s1) (:ball-x s1) (:ball-y s1)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_INT(140, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(218, as_fixnum(vector_nth(v, 2)));
}

TEST(test_breakout_contract_wall_reflection_keeps_ball_inside_bounds) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) "
        "                  :phase :play :ball-x 317 :ball-y -1 :ball-vx 2 :ball-vy -2) "
        "        s1 (tiny-breakout.core/step-state s0 {:dx 0 :pause false} 16)] "
        "    [(:ball-x s1) (:ball-y s1) (:ball-vx s1) (:ball-vy s1)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(316, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(-2, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(v, 3)));
}

TEST(test_breakout_contract_brick_hit_removes_one_brick_and_scores_once) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [brick {:id 2001 :x 50 :y 50 :w 20 :h 10 :points 10} "
        "        s-base (tiny-breakout.core/init-state) "
        "        s-a (assoc s-base :phase :play :score 0) "
        "        s-b (assoc s-a :levels [{:id :only :bricks [brick]}] :level-index 0 :bricks [brick]) "
        "        s0 (assoc s-b :ball-x 50 :ball-y 49 :ball-vx 0 :ball-vy 2) "
        "        s1 (tiny-breakout.core/step-state s0 {:dx 0} 16)] "
        "    [(:score s1) (count (:bricks s1)) (:phase s1) (:events s1)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":victory"), vector_nth(v, 2));
    ID events = vector_nth(v, 3);
    TEST_ASSERT_TRUE(TAG(events) == CLJ_VECTOR_PERSISTENT);
}

TEST(test_breakout_contract_bottom_out_decrements_life_and_enters_serve) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) "
        "                  :phase :play :lives 3 :levels [{:id :l1 :bricks []}] "
        "                  :bricks [] :ball-y 239 :ball-vx 0 :ball-vy 3) "
        "        s1 (tiny-breakout.core/step-state s0 {:dx 0} 16)] "
        "    [(:lives s1) (:phase s1) (:ball-y s1)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_INT(218, as_fixnum(vector_nth(v, 2)));
}

TEST(test_breakout_contract_zero_lives_enters_game_over_and_restart_returns_serve) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) "
        "                  :phase :play :lives 1 :ball-y 239 :ball-vx 0 :ball-vy 3) "
        "        s1 (tiny-breakout.core/step-state s0 {:dx 0} 16) "
        "        s2 (tiny-breakout.core/step-state s1 {:launch true} 16)] "
        "    [(:phase s1) (:phase s2) (:lives s2) (:score s2)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":game-over"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":serve"), vector_nth(v, 1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 3)));
}

TEST(test_breakout_contract_pause_toggle_and_pause_suppresses_events) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (let [s0 (assoc (tiny-breakout.core/init-state) :phase :play) "
        "        s1 (tiny-breakout.core/step-state s0 {:pause true} 16) "
        "        s2 (tiny-breakout.core/step-state s1 {:dx 0} 16) "
        "        s3 (tiny-breakout.core/step-state s2 {:pause true} 16)] "
        "    [(:phase s1) (count (:events s2)) (:phase s3)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":pause"), vector_nth(v, 0));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":play"), vector_nth(v, 2));
}

TEST(test_breakout_contract_level_clear_advances_and_victory_overlay_is_exact) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.core) "
        "  (require 'tiny-breakout.scene) "
        "  (let [brick {:id 2001 :x 50 :y 50 :w 20 :h 10 :points 10} "
        "        levels [{:id :l1 :bricks [brick]} {:id :l2 :bricks []}] "
        "        s-base (tiny-breakout.core/init-state) "
        "        s-a (assoc s-base :phase :play :levels levels :level-index 0 :bricks [brick]) "
        "        s0 (assoc s-a :ball-x 50 :ball-y 49 :ball-vx 0 :ball-vy 2) "
        "        s1 (tiny-breakout.core/step-state s0 {:dx 0} 16) "
        "        s2 (tiny-breakout.core/step-state s1 {:launch true} 16) "
        "        victory-scene (tiny-breakout.scene/build-scene (assoc s1 :phase :victory)) "
        "        overlay (get (:index victory-scene) 1005)] "
        "    (and (= :level-clear (:phase s1)) "
        "         (= :serve (:phase s2)) "
        "         (= \"You win!\" (:text overlay)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_input_normalization_supports_digital_and_rotary) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-breakout.input) "
        "  (let [a (tiny-breakout.input/normalize-paddle-intent {:left true}) "
        "        b (tiny-breakout.input/normalize-paddle-intent {:rotary-delta -1})] "
        "    [(:dx a) (:dx b)]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_EQUAL_UINT(2, vector_count(v));
    TEST_ASSERT_EQUAL_INT(-1, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(-1, as_fixnum(vector_nth(v, 1)));
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

TEST(test_breakout_contract_audio_mapping_is_deterministic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.audio) "
        "  (= (tiny-breakout.audio/events->cues [:brick-hit :unknown :victory]) "
        "     [:sfx/brick-hit :cue/victory]))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_audio_defines_three_mini_sfx) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-breakout.audio) "
        "  (and (= 3 (count tiny-breakout.audio/sfx-library)) "
        "       (= :tiny-breakout/paddle-hit (:track-id (tiny-breakout.audio/sfx-spec :sfx/paddle-hit))) "
        "       (= :tiny-breakout/brick-hit (:track-id (tiny-breakout.audio/sfx-spec :sfx/brick-hit))) "
        "       (= :tiny-breakout/life-lost (:track-id (tiny-breakout.audio/sfx-spec :sfx/life-lost)))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_deployment_namespace_returns_breakout_entry) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (let [cfg (tiny-clj.deployment/breakout-demo-config)] "
        "    (and (= :tiny-breakout (:entry cfg)) "
        "         (= :title (:phase (:state cfg))) "
        "         (= true (:visible (:frame cfg))))))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_config_exposes_viewer_slots_and_atom) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (require 'tiny-clj.event) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config) "
        "        slots (:slots cfg) "
        "        game-atom (:game-scene-atom cfg) "
        "        state-atom (:game-state-atom cfg) "
        "        ok (and (= :tiny-breakout (:entry cfg)) "
        "                (= :native-breakout (:host-runtime cfg)) "
        "                (= 1 (count slots)) "
        "                (atom? game-atom) "
        "                (atom? state-atom) "
        "                (fn? (:spatial-callback cfg)) "
        "                (= game-atom (:atom (first slots))) "
        "                (= true (:visible (deref game-atom))))] "
        "    (tiny-clj.event/on {:source :button :id :left} nil) "
        "    (tiny-clj.event/on {:source :button :id :right} nil) "
        "    (tiny-clj.event/on {:source :button :id :fire} nil) "
        "    (tiny-clj.event/on {:source :button :id :y} nil) "
        "    ok))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_host_button_events_mutate_state_discretely) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.deployment) "
        "  (let [cfg (tiny-clj.deployment/breakout-host-config) "
        "        s0 @tiny-clj.deployment/breakout-host-state* "
        "        _ (tiny-clj.deployment/breakout-host-button-event! {:id :fire :kind :button/down}) "
        "        s1 @tiny-clj.deployment/breakout-host-state* "
        "        _ (tiny-clj.deployment/breakout-host-button-event! {:id :right :kind :button/down}) "
        "        s2 @tiny-clj.deployment/breakout-host-state* "
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
        "            {:source :spatial :id :ball-vs-paddle :rule {:id :ball-vs-paddle} :phase :enter}) "
        "        _ (tiny-clj.event/on {:source :spatial :id :ball-vs-paddle} nil)] "
        "    (= [[:ball-vs-paddle :enter]] @seen)))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, ok);
}

TEST(test_breakout_contract_namespace_files_use_tiny_breakout_prefix) {
    const char *files[] = {"core.clj", "scene.clj", "input.clj", "audio.clj", "levels.clj"};
    for (unsigned int i = 0; i < (unsigned int)(sizeof(files) / sizeof(files[0])); i++) {
        size_t len = 0;
        char *src = read_breakout_source(files[i], &len);
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "(ns tiny-breakout."),
                                     "all breakout files must declare tiny-breakout.* namespace");
        CLJ_FREE(src);
    }
}

TEST(test_breakout_contract_dependencies_are_limited_to_tiny_clj_and_tiny_fx) {
    const char *files[] = {"core.clj", "scene.clj", "input.clj", "audio.clj", "levels.clj"};
    for (unsigned int i = 0; i < (unsigned int)(sizeof(files) / sizeof(files[0])); i++) {
        size_t len = 0;
        char *src = read_breakout_source(files[i], &len);
        TEST_ASSERT_TRUE(len > 0);

        TEST_ASSERT_NULL_MESSAGE(strstr(src, "tiny-fx.game-demo"),
                                 "breakout namespaces must not depend on tiny-fx.game-demo");
        TEST_ASSERT_NULL_MESSAGE(strstr(src, "tiny-fx.startup"),
                                 "breakout namespaces must not depend on tiny-fx.startup");
        TEST_ASSERT_NULL_MESSAGE(strstr(src, "tiny-db."),
                                 "breakout namespaces must not depend on tiny-db namespaces");
        TEST_ASSERT_NULL_MESSAGE(strstr(src, "clojure.string"),
                                 "breakout namespaces must not depend on clojure.string");
        TEST_ASSERT_NULL_MESSAGE(strstr(src, "clojure.set"),
                                 "breakout namespaces must not depend on clojure.set");

        CLJ_FREE(src);
    }
}
