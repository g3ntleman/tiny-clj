#include "tests_common.h"
#include "../atom.h"
#include "../builtins_tiny_fx_gfx.h"
#include "../vector_scene_graph.h"
#include "../scene.h"
#include "../tiny_fx_gfx.h"
#include "../render_backend.h"
#include "../rendered_state_snapshot.h"
#include "../renderer_lifecycle.h"
#include "../viewer_collision.h"
#include "callbacks.h"
#include "record.h"
#include <time.h>
#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

#define TEST_W 64
#define TEST_H 48

static size_t count_color(const uint16_t *pixels, size_t n, uint16_t color) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++) {
        if (pixels[i] == color) {
            c++;
        }
    }
    return c;
}

static size_t count_not_color(const uint16_t *pixels, size_t n, uint16_t color) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++) {
        if (pixels[i] != color) {
            c++;
        }
    }
    return c;
}

static bool find_non_bg_bounds(const uint16_t *pixels, size_t w, size_t h, uint16_t bg,
                               size_t *out_min_x, size_t *out_min_y,
                               size_t *out_max_x, size_t *out_max_y) {
    bool found = false;
    size_t min_x = w;
    size_t min_y = h;
    size_t max_x = 0;
    size_t max_y = 0;
    for (size_t y = 0; y < h; y++) {
        for (size_t x = 0; x < w; x++) {
            if (pixels[y * w + x] == bg) {
                continue;
            }
            if (!found) {
                found = true;
                min_x = max_x = x;
                min_y = max_y = y;
            } else {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }
    if (!found) {
        return false;
    }
    if (out_min_x) *out_min_x = min_x;
    if (out_min_y) *out_min_y = min_y;
    if (out_max_x) *out_max_x = max_x;
    if (out_max_y) *out_max_y = max_y;
    return true;
}

static uint64_t monotonic_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static uint64_t benchmark_scene_record_render_ns(ID scene,
                                                 VgFrameBuffer *fb,
                                                 unsigned warmup_iterations,
                                                 unsigned measured_iterations) {
    if (!scene || !fb || measured_iterations == 0u) {
        return 0u;
    }
    for (unsigned i = 0; i < warmup_iterations; i++) {
        if (!vg_render_scene_record(scene, fb)) {
            return 0u;
        }
    }
    uint64_t start_ns = monotonic_now_ns();
    for (unsigned i = 0; i < measured_iterations; i++) {
        if (!vg_render_scene_record(scene, fb)) {
            return 0u;
        }
    }
    uint64_t end_ns = monotonic_now_ns();
    if (end_ns <= start_ns) {
        return 0u;
    }
    return end_ns - start_ns;
}

#if defined(__APPLE__) || defined(__linux__)
typedef struct {
    VgSlotChangeTracker *tracker;
    uint32_t seen[VG_SLOT_CHANGE_TRACKER_MAX_SLOTS];
    uint32_t current[VG_SLOT_CHANGE_TRACKER_MAX_SLOTS];
    uint32_t changed_mask;
} SlotChangeWaitThreadArgs;

static void *slot_change_wait_thread_main(void *arg) {
    SlotChangeWaitThreadArgs *args = (SlotChangeWaitThreadArgs *)arg;
    if (!args || !args->tracker) {
        return NULL;
    }
    args->changed_mask = vg_slot_change_tracker_wait_for_changes(args->tracker,
                                                                 args->seen,
                                                                 args->current,
                                                                 UINT32_MAX);
    return NULL;
}
#endif

TEST(test_vector_scene_graph_nested_group_transform_affects_child_line) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;

    VgNode line = {
        .id = 11,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.line = {.x1 = 0, .y1 = 0, .x2 = 10, .y2 = 0}
    };

    VgNode *children[] = {&line};
    VgTransform group_t = vg_transform_identity();
    group_t.tx = 7;
    group_t.ty = 5;
    VgNode group = {
        .id = 10,
        .type = VG_NODE_GROUP,
        .has_transform = true,
        .transform = group_t,
        .style = style,
        .data.group = {.children = children, .child_count = 1}
    };

    vg_render_scene(&group, &fb);

    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)5 * TEST_W + 7]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)5 * TEST_W + 17]);
}

TEST(test_vector_scene_graph_group_visible_false_skips_children) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle line_style = vg_style_default();
    line_style.stroke_color = 0xffffu;
    VgNode line = {
        .id = 111,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = line_style,
        .data.line = {.x1 = 4, .y1 = 6, .x2 = 18, .y2 = 6}
    };

    VgStyle hidden_group_style = vg_style_default();
    hidden_group_style.visible = false;
    VgNode *children[] = {&line};
    VgNode group = {
        .id = 110,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = hidden_group_style,
        .data.group = {.children = children, .child_count = 1}
    };

    vg_render_scene(&group, &fb);

    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 4]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 18]);
}

TEST(test_vector_scene_graph_renders_line_directly_from_clojure_records) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (->Scene "
        "    (->Group 100 nil nil true "
        "             [(->Line 101 nil (->Style 65535 1 true false 0 false 0) true 4 6 18 6)]) "
        "    nil nil nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 4]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 18]);
}

TEST(test_vector_scene_graph_renders_line_from_flat_entity_map_records) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (let [entities {'root (->Group 'root nil nil true [101]) "
        "                  101 (->Line 101 nil (->Style 65535 1 true false 0 false 0) true 4 6 18 6)}] "
        "    (->Scene entities nil nil nil)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 4]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 18]);
}

TEST(test_vector_scene_graph_flat_entity_map_nested_group_transform_inheritance) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (let [entities {'root (->Group 'root (->Transform 2 3 1 1 0) nil true [210]) "
        "                  210 (->Group 210 (->Transform 5 2 1 1 0) nil true [211]) "
        "                  211 (->Line 211 nil (->Style 65535 1 true false 0 false 0) true 0 0 10 0)}] "
        "    (->Scene entities nil nil nil)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)5 * TEST_W + 7]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)5 * TEST_W + 17]);
}

TEST(test_vector_scene_graph_flat_entity_map_missing_root_symbol_fails) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (let [entities {100 (->Group 100 nil nil true [])}] "
        "    (->Scene entities nil nil nil)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_FALSE(vg_render_scene_record(scene, &fb));
}

TEST(test_vector_scene_graph_flat_entity_map_missing_child_id_fails) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (let [entities {'root (->Group 'root nil nil true [999])}] "
        "    (->Scene entities nil nil nil)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_FALSE(vg_render_scene_record(scene, &fb));
}

TEST(test_vector_scene_graph_game_demo_bundle_uses_flat_entity_maps) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "        root0 (:root (nth bundle 0)) "
        "        root1 (:root (nth bundle 1)) "
        "        root2 (:root (nth bundle 2))] "
        "    (and (map? root0) (map? root1) (map? root2) "
        "         (contains? root0 'root) "
        "         (contains? root1 'root) "
        "         (contains? root2 'root))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_game_demo_create_demo_bundle_resets_fresh_game_scene) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID bundle0 = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (require 'tiny-fx.gfx) "
        "  (tiny-fx.game-demo/create-demo-bundle))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(bundle0);
    TEST_ASSERT_TRUE(is_vector(bundle0));
    CljPersistentVector *vec0 = as_vector(bundle0);
    TEST_ASSERT_NOT_NULL(vec0);
    TEST_ASSERT_TRUE(vector_count(vec0) >= 3);
    ID game0 = vector_nth(vec0, 2);
    TEST_ASSERT_NOT_NULL(game0);

    ID mutated_ok = eval_string(
        "(do "
        "  (tiny-fx.gfx/invoke-collision-callback! {:kind :collision :phase :enter}) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_TRUE(mutated_ok && mutated_ok != clj_false);

    ID bundle1 = eval_string("(tiny-fx.game-demo/create-demo-bundle)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(bundle1);
    TEST_ASSERT_TRUE(is_vector(bundle1));
    CljPersistentVector *vec1 = as_vector(bundle1);
    TEST_ASSERT_NOT_NULL(vec1);
    TEST_ASSERT_TRUE(vector_count(vec1) >= 3);
    ID game1 = vector_nth(vec1, 2);
    TEST_ASSERT_NOT_NULL(game1);
    TEST_ASSERT_TRUE(game0 != game1);

    ID reset_ok = eval_string(
        "(let [player (get (:root @tiny-fx.game-demo/game-scene-state) 3002) "
        "      t0 (nth (nth (:keyframes (:t player)) 0) 1)] "
        "  (= [1 1] [(:sx t0) (:sy t0)]))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(reset_ok && reset_ok != clj_false);
}

TEST(test_vector_scene_graph_game_demo_score_text_is_timeline_driven) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "        score-root (:root (nth bundle 1)) "
        "        score-text (get score-root 2001) "
        "        score-field (:text score-text)] "
        "    (and (contains? score-field :keyframes) "
        "         (contains? score-field :loop))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_game_demo_game_motion_is_timeline_driven) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "        game-root (:root (nth bundle 2)) "
        "        terrain (get game-root 3001) "
        "        player (get game-root 3002) "
        "        rocket-body (get game-root 3003) "
        "        rocket-nose (get game-root 3005)] "
        "    (and (contains? (:t terrain) :keyframes) "
        "         (contains? (:t terrain) :loop) "
        "         (contains? (:t player) :keyframes) "
        "         (contains? (:t player) :loop) "
        "         (contains? (:t rocket-body) :keyframes) "
        "         (contains? (:t rocket-body) :loop) "
        "         (contains? (:t rocket-nose) :keyframes) "
        "         (contains? (:t rocket-nose) :loop))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_game_demo_entities_use_local_foot_pivots) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "        game-root (:root (nth bundle 2)) "
        "        player (get game-root 3002) "
        "        proxy (get game-root 3006) "
        "        player-kf0 (nth (:keyframes (:t player)) 0) "
        "        player-kf1 (nth (:keyframes (:t player)) 1) "
        "        player-t0 (nth player-kf0 1) "
        "        player-t1 (nth player-kf1 1) "
        "        rocket-body (get game-root 3003) "
        "        rocket-kf0 (nth (:keyframes (:t rocket-body)) 0) "
        "        rocket-t0 (nth rocket-kf0 1) "
        "        rocket-nose (get game-root 3005)] "
        "    [(= [-16 0 0 -28 16 0] "
        "        [(:x1 player) (:y1 player) (:x2 player) (:y2 player) (:x3 player) (:y3 player)]) "
        "     (= [-16 0 0 -28 16 0] "
        "        [(:x1 proxy) (:y1 proxy) (:x2 proxy) (:y2 proxy) (:x3 proxy) (:y3 proxy)]) "
        "     (= [72 146 72 136] "
        "        [(:tx player-t0) (:ty player-t0) (:tx player-t1) (:ty player-t1)]) "
        "     (= [1 1] [(:sx player-t0) (:sy player-t0)]) "
        "     (= [[-12 -4] [8 -4] [8 -10] [-12 -10] "
        "         [-16 -14] [-20 -14] [-20 -9] [-13 -9] "
        "         [-13 -8] [-20 -8] [-20 -6] [-13 -6] "
        "         [-13 -5] [-20 -5] [-20 0] [-16 0]] "
        "        (:pts rocket-body)) "
        "     (= [346 126] [(:tx rocket-t0) (:ty rocket-t0)]) "
        "     (= [20 -7 10 -10 10 -4] "
        "        [(:x1 rocket-nose) (:y1 rocket-nose) "
        "         (:x2 rocket-nose) (:y2 rocket-nose) (:x3 rocket-nose) (:y3 rocket-nose)])"
        "     ]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(7, vector_count(v));
    for (uint32_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, i));
    }
}

TEST(test_vector_scene_graph_tiny_fx_runtime_game_demo_config_shape) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (let [cfg (tiny-fx.gfx/game-demo-config) "
        "        slots (:slots cfg) "
        "        slot-ids (mapv (fn [slot] (:id slot)) slots) "
        "        slot-atoms (mapv (fn [slot] (:atom slot)) slots) "
        "        spatial-callback (:spatial-callback cfg) "
        "        game-scene-atom (:game-scene-atom cfg) "
        "        game-scene @game-scene-atom "
        "        rules (:collision-rules game-scene) "
        "        rule1 (first rules) "
        "        rule2 (second rules)] "
        "    (and (= [:deco :score :game] slot-ids) "
        "         (= 3 (count slots)) "
        "         (= [tiny-fx.game-demo/deco-scene-state "
        "             tiny-fx.game-demo/score-scene-state "
        "             tiny-fx.game-demo/game-scene-state] "
        "            slot-atoms) "
        "         (fn? spatial-callback) "
        "         (= game-scene @game-scene-atom) "
        "         (= 2 (count rules)) "
        "         (= :collision (:kind rule1)) "
        "         (nil? (:channel rule1)) "
        "         (= 0 (:radius rule1)) "
        "         (= :proximity (:kind rule2)) "
        "         (= :hearing (:channel rule2)) "
        "         (= 24 (:radius rule2)) "
        "         (= 3003 (:a-id rule1)) "
        "         (= 3006 (:b-id rule1)) "
        "         (= 3003 (:a-id rule2)) "
        "         (= 3006 (:b-id rule2)))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_game_demo_player_entity_matches_tri_type_hash) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    TEST_ASSERT_TRUE(tiny_fx_gfx_ensure_schema(g_test_eval_state));

    ID bundle = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (tiny-fx.game-demo/create-demo-bundle))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_TRUE(is_vector(bundle));
    CljPersistentVector *vec = as_vector(bundle);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_TRUE(vector_count(vec) >= 3);

    ID game_scene_obj = vector_nth(vec, 2);
    TEST_ASSERT_NOT_NULL(game_scene_obj);
    FrameScene *game_scene = (FrameScene *)game_scene_obj;
    TEST_ASSERT_TRUE(is_map(game_scene->root));

    ID player = map_get_sentinel(game_scene->root, fixnum(3002), NULL);
    TEST_ASSERT_NOT_NULL(player);
    TEST_ASSERT_TRUE(is_record(player));

    const VgRecordSchema *schema = tiny_fx_gfx_schema();
    TEST_ASSERT_NOT_NULL(schema);
    TEST_ASSERT_TRUE(as_record(player)->descriptor != NULL);
    uint32_t player_type_hash = clj_hash(as_record(player)->descriptor->type_symbol);
    TEST_ASSERT_EQUAL_UINT32(schema->h_tri, player_type_hash);
}

TEST(test_vector_scene_graph_game_demo_collision_callback_toggles_player_scale_in_clojure) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (require 'tiny-fx.gfx) "
        "  (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "        game0 (nth bundle 2) "
        "        p0 (get (:root game0) 3002) "
        "        h0 (get (:root game0) 3006)] "
        "    (tiny-fx.gfx/set-collision-callback! nil) "
        "    (let [disabled? (nil? (tiny-fx.gfx/invoke-collision-callback! nil)) "
        "          _ (tiny-fx.game-demo/configure-collision-toggle-callback!) "
        "          enter? (nil? (tiny-fx.gfx/invoke-collision-callback! {:kind :collision :phase :enter})) "
        "          p1 (get (:root @tiny-fx.game-demo/game-scene-state) 3002) "
        "          h1 (get (:root @tiny-fx.game-demo/game-scene-state) 3006) "
        "          t1 (nth (nth (:keyframes (:t p1)) 0) 1) "
        "          ht1 (nth (nth (:keyframes (:t h1)) 0) 1) "
        "          exit? (nil? (tiny-fx.gfx/invoke-collision-callback! {:kind :collision :phase :exit})) "
        "          p2 (get (:root @tiny-fx.game-demo/game-scene-state) 3002) "
        "          h2 (get (:root @tiny-fx.game-demo/game-scene-state) 3006) "
        "          t0 (nth (nth (:keyframes (:t p0)) 0) 1) "
        "          t2 (nth (nth (:keyframes (:t p2)) 0) 1) "
        "          ht2 (nth (nth (:keyframes (:t h2)) 0) 1)] "
        "      [disabled? enter? exit? "
        "       (= [-16 0 0 -28 16 0] "
        "          [(:x1 p0) (:y1 p0) (:x2 p0) (:y2 p0) (:x3 p0) (:y3 p0)]) "
        "       (= [(:x1 p0) (:y1 p0) (:x2 p0) (:y2 p0) (:x3 p0) (:y3 p0)] "
        "          [(:x1 p1) (:y1 p1) (:x2 p1) (:y2 p1) (:x3 p1) (:y3 p1)]) "
        "       (= [(:x1 p0) (:y1 p0) (:x2 p0) (:y2 p0) (:x3 p0) (:y3 p0)] "
        "          [(:x1 p2) (:y1 p2) (:x2 p2) (:y2 p2) (:x3 p2) (:y3 p2)]) "
        "       (= [1 1] [(:sx t0) (:sy t0)]) "
        "       (= 0.75 (:sx t1)) "
        "       (> (:sy t1) 0.70) "
        "       (< (:sy t1) 0.72) "
        "       (= [1 1] [(:sx t2) (:sy t2)]) "
        "       (= [(:x1 h0) (:y1 h0) (:x2 h0) (:y2 h0) (:x3 h0) (:y3 h0)] "
        "          [(:x1 h1) (:y1 h1) (:x2 h1) (:y2 h1) (:x3 h1) (:y3 h1)]) "
        "       (= [(:x1 h0) (:y1 h0) (:x2 h0) (:y2 h0) (:x3 h0) (:y3 h0)] "
        "          [(:x1 h2) (:y1 h2) (:x2 h2) (:y2 h2) (:x3 h2) (:y3 h2)]) "
        "       (= [1 1] [(:sx ht1) (:sy ht1)]) "
        "       (= [1 1] [(:sx ht2) (:sy ht2)])"
        "       ])))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(15, vector_count(v));
    for (uint32_t i = 0; i < 15; i++) {
        TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, i));
    }
}

TEST(test_vector_scene_graph_game_demo_hidden_collision_proxy_captures_rendered_aabb) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    TEST_ASSERT_TRUE(tiny_fx_gfx_ensure_schema(g_test_eval_state));
    vg_rendered_state_reset_all();

    ID game_scene = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (nth (tiny-fx.game-demo/create-demo-bundle) 2))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(game_scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgRenderSlotState slot_state = {0};
    uint32_t dirty_pixels = 0u;
    vg_rendered_state_capture_begin(2u, 77u, 0u);
    bool rendered = vg_render_frame_slot_record_if_changed_at_ms(game_scene, &slot_state, &fb, 77u, 0u, &dirty_pixels);
    if (rendered) {
        vg_rendered_state_capture_commit();
    } else {
        vg_rendered_state_capture_discard();
    }
    TEST_ASSERT_TRUE(rendered);

    VgRenderedEntityState proxy_state = {0};
    TEST_ASSERT_TRUE(vg_rendered_state_query_entity(2u, (uintptr_t)fixnum(3006), &proxy_state));
    TEST_ASSERT_TRUE(proxy_state.has_world_aabb);
    TEST_ASSERT_EQUAL_INT(56, proxy_state.world_aabb.min_x);
    TEST_ASSERT_EQUAL_INT(88, proxy_state.world_aabb.max_x);
    TEST_ASSERT_EQUAL_INT(118, proxy_state.world_aabb.min_y);
    TEST_ASSERT_EQUAL_INT(146, proxy_state.world_aabb.max_y);

    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_game_demo_gpio_press_triggers_demo_melody_once) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (tiny-fx.game-demo/create-demo-bundle) "
        "  (gpio-simulate! 1 1) "
        "  (run-next-task) "
        "  (gpio-simulate! 1 0) "
        "  (run-next-task) "
        "  (let [count @tiny-fx.game-demo/demo-melody-trigger-count* "
        "        status (tiny-fx.audio/audio-host-status!)] "
        "    (and (= 1 count) "
        "         (contains? status :tick-running))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_tiny_fx_collision_callback_reconfiguration) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx) "
        "  (def collision-test-cb-a (fn collision-test-cb-a [event] 11)) "
        "  (def collision-test-cb-b (fn collision-test-cb-b [event] 22)) "
        "  (let [_ (tiny-fx.gfx/set-collision-callback! collision-test-cb-a) "
        "        v1 (tiny-fx.gfx/invoke-collision-callback! {:phase :enter}) "
        "        _ (tiny-fx.gfx/set-collision-callback! collision-test-cb-b) "
        "        v2 (tiny-fx.gfx/invoke-collision-callback! {:phase :exit}) "
        "        _ (tiny-fx.gfx/set-collision-callback! nil) "
        "        v3 (tiny-fx.gfx/invoke-collision-callback! nil)] "
        "    (and (= 11 v1) (= 22 v2) (nil? v3))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_game_demo_contains_blinking_stars) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "        game-root (:root (nth bundle 2)) "
        "        terrain (get game-root 3001) "
        "        stars-group (get game-root 3020) "
        "        s1 (get game-root 3021) "
        "        s2 (get game-root 3022) "
        "        s3 (get game-root 3023) "
        "        s4 (get game-root 3024) "
        "        s5 (get game-root 3025) "
        "        s6 (get game-root 3026)] "
        "    (and (= [3021 3022 3023 3024 3025 3026] (:children stars-group)) "
        "         (= 3020 (first (:children (get game-root 'root)))) "
        "         (contains? (:t stars-group) :keyframes) "
        "         (contains? (:t stars-group) :loop) "
        "         (> (first (second (:keyframes (:t stars-group)))) "
        "            (first (second (:keyframes (:t terrain))))) "
        "         (contains? (:style s1) :keyframes) "
        "         (contains? (:style s2) :keyframes) "
        "         (contains? (:style s3) :keyframes) "
        "         (contains? (:style s4) :keyframes) "
        "         (contains? (:style s5) :keyframes) "
        "         (contains? (:style s6) :keyframes) "
        "         (contains? s1 :pts) "
        "         (contains? s2 :pts) "
        "         (contains? s3 :pts) "
        "         (contains? s4 :pts) "
        "         (contains? s5 :pts) "
        "         (contains? s6 :pts) "
        "         (= 5 (count (:pts s1))) "
        "         (= 5 (count (:pts s2))) "
        "         (= 5 (count (:pts s3))) "
        "         (= 5 (count (:pts s4))) "
        "         (= 5 (count (:pts s5))) "
        "         (= 5 (count (:pts s6))) "
        "         (not= (:keyframes (:style s1)) (:keyframes (:style s2))) "
        "         (not= (:keyframes (:style s1)) (:keyframes (:style s3))) "
        "         (not= (:keyframes (:style s1)) (:keyframes (:style s4))) "
        "         (not= (:keyframes (:style s1)) (:keyframes (:style s5))) "
        "         (not= (:keyframes (:style s1)) (:keyframes (:style s6))) "
        "         (= 0 (:stroke_color (second (second (:keyframes (:style s1)))))) "
        "         (= 0 (:stroke_color (second (second (:keyframes (:style s2)))))) "
        "         (= 0 (:stroke_color (second (second (:keyframes (:style s3)))))) "
        "         (= 0 (:stroke_color (second (second (:keyframes (:style s4)))))) "
        "         (= 0 (:stroke_color (second (second (:keyframes (:style s5)))))) "
        "         (= 0 (:stroke_color (second (second (:keyframes (:style s6)))))))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_merge_nested_record_maps_regression) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (let [style (tiny-fx.game-demo/style {:stroke-color 65535 :stroke-width 1}) "
        "        root (tiny-fx.game-demo/group {:id 'root :style style :children [3021 3022]}) "
        "        star-a (tiny-fx.game-demo/tri {:id 3021 :style style :x1 8 :y1 8 :x2 10 :y2 4 :x3 12 :y3 8}) "
        "        star-b (tiny-fx.game-demo/tri {:id 3022 :style style :x1 18 :y1 8 :x2 20 :y2 4 :x3 22 :y3 8}) "
        "        base {'root root} "
        "        stars {3021 star-a 3022 star-b} "
        "        merged (merge base stars)] "
        "    (and (contains? merged 'root) "
        "         (contains? merged 3021) "
        "         (contains? merged 3022))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_timeline_numeric_interpolation_moves_line) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Timeline [keyframes loop]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (let [x1-t (->Timeline [[0 4] [100 14]] false) "
        "        x2-t (->Timeline [[0 18] [100 28]] false)] "
        "    (->Scene "
        "      (->Line 101 nil (->Style 65535 1 true false 0 false 0) true x1-t 6 x2-t 6) "
        "      nil nil nil)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record_at_ms(scene, &fb, 50u));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 9]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 23]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 4]);
}

TEST(test_vector_scene_graph_timeline_transform_interpolation_moves_line) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Timeline [keyframes loop]) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (let [t (->Timeline [[0 (->Transform 0 0 1 1 0)] "
        "                       [100 (->Transform 20 0 1 1 0)]] false)] "
        "    (->Scene "
        "      (->Line 101 t (->Style 65535 1 true false 0 false 0) true 0 6 10 6) "
        "      nil nil nil)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record_at_ms(scene, &fb, 50u));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 10]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 20]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 0]);
}

TEST(test_vector_scene_graph_timeline_loop_wraps_phase) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Timeline [keyframes loop]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (let [x1-t (->Timeline [[0 4] [100 14]] true) "
        "        x2-t (->Timeline [[0 18] [100 28]] true)] "
        "    (->Scene "
        "      (->Line 101 nil (->Style 65535 1 true false 0 false 0) true x1-t 6 x2-t 6) "
        "      nil nil nil)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record_at_ms(scene, &fb, 150u));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 9]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 23]);
}

TEST(test_vector_scene_graph_record_group_visible_false_skips_children) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (->Scene "
        "    (->Group 120 nil nil false "
        "             [(->Line 121 nil (->Style 65535 1 true false 0 false 0) true 4 6 18 6)]) "
        "    nil nil nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 4]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 18]);
}

TEST(test_vector_scene_graph_renders_nested_record_transform_inheritance) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (->Scene "
        "    (->Group 200 (->Transform 7 5 1 1 0) nil true "
        "             [(->Line 201 nil (->Style 65535 1 true false 0 false 0) true 0 0 10 0)]) "
        "    nil nil nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)5 * TEST_W + 7]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)5 * TEST_W + 17]);
}

TEST(test_vector_scene_graph_scene_record_applies_shared_clip_and_erase_rect) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (->Scene "
        "    (->Group 300 nil nil true "
        "             [(->Line 301 nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10)]) "
        "    [20 8 10 6] "
        "    63488 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x1234u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));

    /* Shared clip/erase rect: inside area gets erase color first. */
    TEST_ASSERT_EQUAL_HEX16(0xf800u, pixels[(size_t)9 * TEST_W + 22]);
    /* Clip-Rect begrenzt die Linie */
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)10 * TEST_W + 10]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 24]);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)10 * TEST_W + 35]);
    /* Outside shared clip/erase area remains untouched. */
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)2 * TEST_W + 2]);
}

TEST(test_vector_scene_graph_decode_frame_scene_slot_record) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 401 nil (->Style 65535 1 true false 0 false 0) true 2 3 8 3) "
        "    [1 2 10 12] "
        "    3 true true 0 1 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    VgRenderSlot slot;
    TEST_ASSERT_TRUE(vg_decode_frame_slot_record(scene, &slot));
    TEST_ASSERT_EQUAL_INT(1, slot.clip_rect.x);
    TEST_ASSERT_EQUAL_INT(2, slot.clip_rect.y);
    TEST_ASSERT_EQUAL_INT(10, slot.clip_rect.w);
    TEST_ASSERT_EQUAL_INT(12, slot.clip_rect.h);
    TEST_ASSERT_EQUAL_INT(3, slot.z);
    TEST_ASSERT_TRUE(slot.visible);
    TEST_ASSERT_TRUE(slot.opaque);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, slot.clear_color);
    TEST_ASSERT_EQUAL_INT(1, slot.guard_px);
    TEST_ASSERT_NOT_NULL(slot.root);
}

TEST(test_vector_scene_graph_render_frame_scene_slot_record_if_changed) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 501 nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10) "
        "    [20 8 10 6] "
        "    0 true true 0 0 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x1234u);

    VgRenderSlotState state = {0};
    uint32_t dirty_pixels = 0u;
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_if_changed(scene, &state, &fb, 1u, &dirty_pixels));
    TEST_ASSERT_EQUAL_UINT32(60u, dirty_pixels);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)10 * TEST_W + 10]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 24]);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)10 * TEST_W + 35]);

    dirty_pixels = 123u;
    TEST_ASSERT_FALSE(vg_render_frame_slot_record_if_changed(scene, &state, &fb, 1u, &dirty_pixels));
    TEST_ASSERT_EQUAL_UINT32(0u, dirty_pixels);
}

TEST(test_vector_scene_graph_render_frame_scene_slot_record_if_changed_skips_when_slot_invisible) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 511 nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10) "
        "    [20 8 10 6] "
        "    0 false true 0 0 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x1234u);

    VgRenderSlotState state = {0};
    uint32_t dirty_pixels = 0u;
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_if_changed(scene, &state, &fb, 1u, &dirty_pixels));
    TEST_ASSERT_EQUAL_UINT32(60u, dirty_pixels);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)9 * TEST_W + 22]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 24]);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, pixels[(size_t)2 * TEST_W + 2]);
}

TEST(test_vector_scene_graph_render_frame_scene_slot_record_tracks_has_animation_flag) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID static_scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 521 nil (->Style 65535 1 true false 0 false 0) true 4 10 20 10) "
        "    [0 8 30 6] "
        "    0 true true 0 0 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(static_scene);

    ID animated_scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Timeline [keyframes loop]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 522 nil (->Style 65535 1 true false 0 false 0) true "
        "            (->Timeline [[0 4] [100 14]] false) 10 20 10) "
        "    [0 8 30 6] "
        "    0 true true 0 0 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(animated_scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgRenderSlotState state = {0};
    uint32_t dirty_pixels = 0u;
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_if_changed_at_ms(static_scene, &state, &fb, 1u, 0u, &dirty_pixels));
    TEST_ASSERT_FALSE(state.has_animation);

    memset(&state, 0, sizeof(state));
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_if_changed_at_ms(animated_scene, &state, &fb, 1u, 0u, &dirty_pixels));
    TEST_ASSERT_TRUE(state.has_animation);
}

TEST(test_vector_scene_graph_render_frame_scene_slot_record_force_render_ticks_animation_without_snapshot_change) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Timeline [keyframes loop]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 531 nil (->Style 65535 1 true false 0 false 0) true "
        "            (->Timeline [[0 4] [100 14]] false) 10 20 10) "
        "    [0 8 30 6] "
        "    0 true true 0 0 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgRenderSlotState state = {0};
    uint32_t dirty_pixels = 0u;
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_at_ms(scene, &state, &fb, 1u, 0u, false, &dirty_pixels));
    TEST_ASSERT_TRUE(state.has_animation);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 4]);

    dirty_pixels = 123u;
    TEST_ASSERT_FALSE(vg_render_frame_slot_record_at_ms(scene, &state, &fb, 1u, 50u, false, &dirty_pixels));
    TEST_ASSERT_EQUAL_UINT32(0u, dirty_pixels);

    TEST_ASSERT_TRUE(vg_render_frame_slot_record_at_ms(scene, &state, &fb, 1u, 50u, true, &dirty_pixels));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 9]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 4]);
}

TEST(test_vector_scene_graph_render_frame_scene_slot_record_reports_dirty_rect_union) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID first_scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 541 nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10) "
        "    [20 8 10 6] "
        "    0 true true 0 1 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_scene);

    ID moved_scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 541 nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10) "
        "    [24 11 12 5] "
        "    0 true true 0 2 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(moved_scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x1234u);

    VgRenderSlotState state = {0};
    VgRenderFrameSlotResult result = {0};
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_result_at_ms(first_scene, &state, &fb, 1u, 0u, false, &result));
    TEST_ASSERT_TRUE(result.rendered);
    TEST_ASSERT_EQUAL_INT(19, result.dirty_rect.x);
    TEST_ASSERT_EQUAL_INT(7, result.dirty_rect.y);
    TEST_ASSERT_EQUAL_INT(12, result.dirty_rect.w);
    TEST_ASSERT_EQUAL_INT(8, result.dirty_rect.h);

    memset(&result, 0, sizeof(result));
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_result_at_ms(moved_scene, &state, &fb, 2u, 0u, false, &result));
    TEST_ASSERT_TRUE(result.rendered);
    TEST_ASSERT_EQUAL_INT(19, result.dirty_rect.x);
    TEST_ASSERT_EQUAL_INT(7, result.dirty_rect.y);
    TEST_ASSERT_EQUAL_INT(19, result.dirty_rect.w);
    TEST_ASSERT_EQUAL_INT(11, result.dirty_rect.h);
}

typedef struct {
    uint16_t pixels[TEST_W * TEST_H];
    uint16_t width;
    uint16_t height;
    uint32_t begin_frame_calls;
    uint32_t submit_rect_calls;
    uint32_t end_frame_calls;
    VgBackendRect last_rect;
    uint32_t last_frame_id;
} TestBackendCapture;

static bool test_backend_begin_frame(void *ctx, uint32_t frame_id) {
    TestBackendCapture *capture = (TestBackendCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->begin_frame_calls++;
    capture->last_frame_id = frame_id;
    return true;
}

static bool test_backend_submit_rect(void *ctx,
                                     VgBackendRect rect,
                                     const uint16_t *rgb565_pixels,
                                     uint16_t stride_px) {
    TestBackendCapture *capture = (TestBackendCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    TEST_ASSERT_NOT_NULL(rgb565_pixels);
    capture->submit_rect_calls++;
    capture->last_rect = rect;
    for (int16_t row = 0; row < rect.h; row++) {
        size_t dst_off = (size_t)(rect.y + row) * capture->width + (size_t)rect.x;
        size_t src_off = (size_t)row * (size_t)stride_px;
        memcpy(&capture->pixels[dst_off], &rgb565_pixels[src_off], (size_t)rect.w * sizeof(uint16_t));
    }
    return true;
}

static bool test_backend_end_frame(void *ctx, uint32_t frame_id) {
    TestBackendCapture *capture = (TestBackendCapture *)ctx;
    TEST_ASSERT_NOT_NULL(capture);
    capture->end_frame_calls++;
    capture->last_frame_id = frame_id;
    return true;
}

TEST(test_vector_scene_graph_render_backend_submit_clip_rect_clips_and_forwards_stride) {
    uint16_t src_pixels[TEST_W * TEST_H];
    for (size_t i = 0; i < TEST_W * TEST_H; i++) {
        src_pixels[i] = (uint16_t)i;
    }

    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, src_pixels, TEST_W * TEST_H));

    TestBackendCapture capture = {0};
    capture.width = TEST_W;
    capture.height = TEST_H;

    const VgBackendOps ops = {
        .begin_frame = test_backend_begin_frame,
        .submit_rect = test_backend_submit_rect,
        .end_frame = test_backend_end_frame,
    };
    VgBackend backend = {
        .ops = &ops,
        .ctx = &capture,
    };

    TEST_ASSERT_TRUE(vg_backend_begin_frame(&backend, 17u));
    TEST_ASSERT_TRUE(vg_backend_submit_clip_rect(&backend, &fb, (VgClipRect){-2, 3, 5, 2}));
    TEST_ASSERT_TRUE(vg_backend_end_frame(&backend, 17u));

    TEST_ASSERT_EQUAL_UINT32(1u, capture.begin_frame_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.submit_rect_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, capture.end_frame_calls);
    TEST_ASSERT_EQUAL_UINT32(17u, capture.last_frame_id);
    TEST_ASSERT_EQUAL_INT(0, capture.last_rect.x);
    TEST_ASSERT_EQUAL_INT(3, capture.last_rect.y);
    TEST_ASSERT_EQUAL_INT(3, capture.last_rect.w);
    TEST_ASSERT_EQUAL_INT(2, capture.last_rect.h);
    TEST_ASSERT_EQUAL_UINT16(src_pixels[(size_t)3 * TEST_W + 0], capture.pixels[(size_t)3 * TEST_W + 0]);
    TEST_ASSERT_EQUAL_UINT16(src_pixels[(size_t)3 * TEST_W + 2], capture.pixels[(size_t)3 * TEST_W + 2]);
    TEST_ASSERT_EQUAL_UINT16(src_pixels[(size_t)4 * TEST_W + 1], capture.pixels[(size_t)4 * TEST_W + 1]);
}

TEST(test_vector_scene_graph_slot_change_tracker_publish_and_wait_reports_changed_mask) {
    VgSlotChangeTracker tracker;
    TEST_ASSERT_TRUE(vg_slot_change_tracker_init(&tracker, 3));

    uint32_t seen[3] = {0, 0, 0};
    uint32_t current[3] = {0, 0, 0};

    TEST_ASSERT_EQUAL_HEX32(0u, vg_slot_change_tracker_wait_for_changes(&tracker, seen, current, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, current[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, current[1]);
    TEST_ASSERT_EQUAL_UINT32(0u, current[2]);

    uint32_t gen = 0;
    TEST_ASSERT_TRUE(vg_slot_change_tracker_publish(&tracker, 1, &gen));
    TEST_ASSERT_EQUAL_UINT32(1u, gen);

    uint32_t mask = vg_slot_change_tracker_wait_for_changes(&tracker, seen, current, 0u);
    TEST_ASSERT_EQUAL_HEX32((1u << 1), mask);
    TEST_ASSERT_EQUAL_UINT32(0u, current[0]);
    TEST_ASSERT_EQUAL_UINT32(1u, current[1]);
    TEST_ASSERT_EQUAL_UINT32(0u, current[2]);

    seen[0] = current[0];
    seen[1] = current[1];
    seen[2] = current[2];

    TEST_ASSERT_TRUE(vg_slot_change_tracker_publish(&tracker, 2, NULL));
    TEST_ASSERT_TRUE(vg_slot_change_tracker_publish(&tracker, 1, &gen));
    TEST_ASSERT_EQUAL_UINT32(2u, gen);

    mask = vg_slot_change_tracker_wait_for_changes(&tracker, seen, current, 0u);
    TEST_ASSERT_EQUAL_HEX32((1u << 1) | (1u << 2), mask);
    TEST_ASSERT_EQUAL_UINT32(0u, current[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, current[1]);
    TEST_ASSERT_EQUAL_UINT32(1u, current[2]);

    vg_slot_change_tracker_destroy(&tracker);
}

TEST(test_vector_scene_graph_slot_change_tracker_single_slot_publish_rerenders_only_that_slot) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID scenes = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  [(->FrameScene (->Line 541 nil (->Style 65535 1 true false 0 false 0) true 2 4 14 4) [0 0 20 10] 0 true true 0 0 nil) "
        "   (->FrameScene (->Line 542 nil (->Style 65535 1 true false 0 false 0) true 2 8 14 8) [0 0 20 12] 1 true true 0 0 nil) "
        "   (->FrameScene (->Line 543 nil (->Style 65535 1 true false 0 false 0) true 2 12 14 12) [0 0 20 14] 2 true true 0 0 nil)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scenes);
    TEST_ASSERT_TRUE(is_vector(scenes));

    CljPersistentVector *scene_vec = as_vector(scenes);
    TEST_ASSERT_NOT_NULL(scene_vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(scene_vec));

    ID slot_scene_0 = vector_nth(scene_vec, 0);
    ID slot_scene_1 = vector_nth(scene_vec, 1);
    ID slot_scene_2 = vector_nth(scene_vec, 2);
    TEST_ASSERT_NOT_NULL(slot_scene_0);
    TEST_ASSERT_NOT_NULL(slot_scene_1);
    TEST_ASSERT_NOT_NULL(slot_scene_2);

    VgSlotChangeTracker tracker;
    TEST_ASSERT_TRUE(vg_slot_change_tracker_init(&tracker, 3));

    uint32_t gen0 = 0u;
    uint32_t gen1 = 0u;
    uint32_t gen2 = 0u;
    TEST_ASSERT_TRUE(vg_slot_change_tracker_publish(&tracker, 0u, &gen0));
    TEST_ASSERT_TRUE(vg_slot_change_tracker_publish(&tracker, 1u, &gen1));
    TEST_ASSERT_TRUE(vg_slot_change_tracker_publish(&tracker, 2u, &gen2));
    TEST_ASSERT_EQUAL_UINT32(1u, gen0);
    TEST_ASSERT_EQUAL_UINT32(1u, gen1);
    TEST_ASSERT_EQUAL_UINT32(1u, gen2);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgRenderSlotState slot_states[3] = {0};
    uint32_t seen[3] = {0u, 0u, 0u};
    uint32_t current[3] = {0u, 0u, 0u};

    uint32_t mask = vg_slot_change_tracker_wait_for_changes(&tracker, seen, current, 0u);
    TEST_ASSERT_EQUAL_HEX32((1u << 0) | (1u << 1) | (1u << 2), mask);
    uint32_t rendered_slots = 0u;
    ID slot_scenes[3] = {slot_scene_0, slot_scene_1, slot_scene_2};
    for (uint8_t i = 0; i < 3u; i++) {
        if ((mask & (1u << i)) == 0u) {
            continue;
        }
        uint32_t dirty = 0u;
        if (vg_render_frame_slot_record_if_changed_at_ms(slot_scenes[i],
                                                          &slot_states[i],
                                                          &fb,
                                                          current[i],
                                                          0u,
                                                          &dirty)) {
            rendered_slots++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3u, rendered_slots);

    seen[0] = current[0];
    seen[1] = current[1];
    seen[2] = current[2];
    uint32_t snapshot_slot0 = slot_states[0].snapshot_id;
    uint32_t snapshot_slot1 = slot_states[1].snapshot_id;
    uint32_t snapshot_slot2 = slot_states[2].snapshot_id;

    TEST_ASSERT_TRUE(vg_slot_change_tracker_publish(&tracker, 1u, &gen1));
    TEST_ASSERT_EQUAL_UINT32(2u, gen1);

    memset(current, 0, sizeof(current));
    mask = vg_slot_change_tracker_wait_for_changes(&tracker, seen, current, 0u);
    TEST_ASSERT_EQUAL_HEX32((1u << 1), mask);
    rendered_slots = 0u;
    for (uint8_t i = 0; i < 3u; i++) {
        if ((mask & (1u << i)) == 0u) {
            continue;
        }
        uint32_t dirty = 0u;
        if (vg_render_frame_slot_record_if_changed_at_ms(slot_scenes[i],
                                                          &slot_states[i],
                                                          &fb,
                                                          current[i],
                                                          20u,
                                                          &dirty)) {
            rendered_slots++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(1u, rendered_slots);
    TEST_ASSERT_EQUAL_UINT32(snapshot_slot0, slot_states[0].snapshot_id);
    TEST_ASSERT_TRUE(slot_states[1].snapshot_id != snapshot_slot1);
    TEST_ASSERT_EQUAL_UINT32(snapshot_slot2, slot_states[2].snapshot_id);

    vg_slot_change_tracker_destroy(&tracker);
}

TEST(test_vector_scene_graph_slot_change_tracker_wait_timeout_returns_without_changes) {
    VgSlotChangeTracker tracker;
    TEST_ASSERT_TRUE(vg_slot_change_tracker_init(&tracker, 2));

    uint32_t seen[2] = {0, 0};
    uint32_t current[2] = {99, 99};

    uint32_t mask = vg_slot_change_tracker_wait_for_changes(&tracker, seen, current, 2u);
    TEST_ASSERT_EQUAL_HEX32(0u, mask);
    TEST_ASSERT_EQUAL_UINT32(0u, current[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, current[1]);

    vg_slot_change_tracker_destroy(&tracker);
}

#if defined(__APPLE__) || defined(__linux__)
TEST(test_vector_scene_graph_slot_change_tracker_wait_blocks_until_single_slot_publish) {
    VgSlotChangeTracker tracker;
    TEST_ASSERT_TRUE(vg_slot_change_tracker_init(&tracker, 3));

    SlotChangeWaitThreadArgs args;
    memset(&args, 0, sizeof(args));
    args.tracker = &tracker;

    pthread_t waiter;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&waiter, NULL, slot_change_wait_thread_main, &args));

    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 5 * 1000 * 1000;
    (void)nanosleep(&ts, NULL);

    uint32_t generation = 0u;
    TEST_ASSERT_TRUE(vg_slot_change_tracker_publish(&tracker, 2u, &generation));
    TEST_ASSERT_EQUAL_UINT32(1u, generation);

    TEST_ASSERT_EQUAL_INT(0, pthread_join(waiter, NULL));
    TEST_ASSERT_EQUAL_HEX32((1u << 2), args.changed_mask);
    TEST_ASSERT_EQUAL_UINT32(0u, args.current[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, args.current[1]);
    TEST_ASSERT_EQUAL_UINT32(1u, args.current[2]);

    vg_slot_change_tracker_destroy(&tracker);
}
#endif

TEST(test_vector_scene_graph_fixed_transform_compose_apply_px_scale_translate_exact) {
    VgTransform parent = vg_transform_identity();
    parent.tx = 10;
    parent.ty = -2;
    parent.sx = 2 * VG_SCALE_ONE;
    parent.sy = 3 * VG_SCALE_ONE;

    VgTransform local = vg_transform_identity();
    local.tx = 4;
    local.ty = 5;

    VgTransformFixed parent_f = vg_transform_fixed_from_transform(parent);
    VgTransformFixed local_f = vg_transform_fixed_from_transform(local);
    VgTransformFixed composed = vg_transform_fixed_compose(parent_f, local_f);

    int out_x = 0;
    int out_y = 0;
    vg_transform_fixed_apply_px(composed, 7, 8, &out_x, &out_y);

    TEST_ASSERT_EQUAL_INT(32, out_x);
    TEST_ASSERT_EQUAL_INT(37, out_y);
}

TEST(test_vector_scene_graph_fixed_transform_cardinal_rotation_is_exact) {
    int one_fp = vg_transform_fixed_identity().m00;
    VgTransform t = vg_transform_identity();
    t.tx = 10;
    t.ty = 20;
    t.sx = VG_SCALE_ONE;
    t.sy = VG_SCALE_ONE;
    t.rot_deg = -90;

    VgTransformFixed tf = vg_transform_fixed_from_transform(t);
    TEST_ASSERT_EQUAL_INT(0, tf.m00);
    TEST_ASSERT_EQUAL_INT(one_fp, tf.m01);
    TEST_ASSERT_EQUAL_INT(-one_fp, tf.m10);
    TEST_ASSERT_EQUAL_INT(0, tf.m11);

    int out_x = 0;
    int out_y = 0;
    vg_transform_fixed_apply_px(tf, 3, 4, &out_x, &out_y);
    TEST_ASSERT_EQUAL_INT(14, out_x);
    TEST_ASSERT_EQUAL_INT(17, out_y);
}

TEST(test_vector_scene_graph_anim_fixed_progress_ease_and_lerp_are_deterministic) {
    int32_t p0 = vg_anim_progress_q13(0u, 1000u);
    int32_t p_half = vg_anim_progress_q13(500u, 1000u);
    int32_t p_end = vg_anim_progress_q13(1000u, 1000u);
    int32_t p_over = vg_anim_progress_q13(1200u, 1000u);
    int32_t p_zero_dur = vg_anim_progress_q13(1u, 0u);

    TEST_ASSERT_EQUAL_INT32(0, p0);
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE / 2, p_half);
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE, p_end);
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE, p_over);
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE, p_zero_dur);

    TEST_ASSERT_EQUAL_INT32(0, vg_anim_ease_q13(VG_ANIM_EASE_LINEAR, -1));
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE, vg_anim_ease_q13(VG_ANIM_EASE_LINEAR, VG_SCALE_ONE + 1));
    TEST_ASSERT_EQUAL_INT32(p_half, vg_anim_ease_q13(VG_ANIM_EASE_LINEAR, p_half));

    int32_t eased_half = vg_anim_ease_q13(VG_ANIM_EASE_OUT_CUBIC, p_half);
    TEST_ASSERT_TRUE(eased_half > p_half); /* ease-out should advance faster in first half */
    TEST_ASSERT_EQUAL_INT32(0, vg_anim_ease_q13(VG_ANIM_EASE_OUT_CUBIC, 0));
    TEST_ASSERT_EQUAL_INT32(VG_SCALE_ONE, vg_anim_ease_q13(VG_ANIM_EASE_OUT_CUBIC, VG_SCALE_ONE));

    int32_t from = 10 * VG_SCALE_ONE;
    int32_t to = 20 * VG_SCALE_ONE;
    int32_t mid_lin = vg_anim_lerp_q13(from, to, p_half);
    int32_t mid_eased = vg_anim_lerp_q13(from, to, eased_half);
    TEST_ASSERT_EQUAL_INT32(15 * VG_SCALE_ONE, mid_lin);
    TEST_ASSERT_TRUE(mid_eased > mid_lin);

    /* Determinism: same input -> same result. */
    TEST_ASSERT_EQUAL_INT32(eased_half, vg_anim_ease_q13(VG_ANIM_EASE_OUT_CUBIC, p_half));
    TEST_ASSERT_EQUAL_INT32(mid_eased, vg_anim_lerp_q13(from, to, eased_half));
}

TEST(test_vector_scene_graph_anim_transform_state_converges_and_is_deterministic) {
    VgTransform initial = vg_transform_identity();
    VgTransform target = vg_transform_identity();
    target.tx = 48;
    target.ty = -12;
    target.sx = (VG_SCALE_ONE * 3) / 2;
    target.sy = (VG_SCALE_ONE * 5) / 4;
    target.rot_deg = 30;

    VgAnimTransformState state_a = {0};
    VgAnimTransformState state_b = {0};
    vg_anim_transform_state_reset(&state_a, initial, 120u, VG_ANIM_EASE_OUT_CUBIC);
    vg_anim_transform_state_reset(&state_b, initial, 120u, VG_ANIM_EASE_OUT_CUBIC);
    vg_anim_transform_state_set_target(&state_a, target);
    vg_anim_transform_state_set_target(&state_b, target);

    VgTransform prev = initial;
    for (int i = 0; i < 32; i++) {
        VgTransform step_a = vg_anim_transform_state_step(&state_a, 16u);
        VgTransform step_b = vg_anim_transform_state_step(&state_b, 16u);
        TEST_ASSERT_EQUAL_INT16(step_a.tx, step_b.tx);
        TEST_ASSERT_EQUAL_INT16(step_a.ty, step_b.ty);
        TEST_ASSERT_EQUAL_INT32(step_a.sx, step_b.sx);
        TEST_ASSERT_EQUAL_INT32(step_a.sy, step_b.sy);
        TEST_ASSERT_EQUAL_INT16(step_a.rot_deg, step_b.rot_deg);

        TEST_ASSERT_TRUE(step_a.tx >= prev.tx);
        TEST_ASSERT_TRUE(step_a.ty <= prev.ty);
        TEST_ASSERT_TRUE(step_a.rot_deg >= prev.rot_deg);
        prev = step_a;
    }

    VgTransform settled = vg_anim_transform_state_step(&state_a, 200u);
    TEST_ASSERT_EQUAL_INT16(target.tx, settled.tx);
    TEST_ASSERT_EQUAL_INT16(target.ty, settled.ty);
    TEST_ASSERT_EQUAL_INT32(target.sx, settled.sx);
    TEST_ASSERT_EQUAL_INT32(target.sy, settled.sy);
    TEST_ASSERT_EQUAL_INT16(target.rot_deg, settled.rot_deg);
}

TEST(test_vector_scene_graph_anim_transform_state_is_interrupt_safe) {
    VgTransform initial = vg_transform_identity();
    VgAnimTransformState state = {0};
    vg_anim_transform_state_reset(&state, initial, 100u, VG_ANIM_EASE_LINEAR);

    VgTransform target_right = vg_transform_identity();
    target_right.tx = 100;
    vg_anim_transform_state_set_target(&state, target_right);
    VgTransform mid = vg_anim_transform_state_step(&state, 50u);
    TEST_ASSERT_TRUE(mid.tx > 0);
    TEST_ASSERT_TRUE(mid.tx < 100);

    VgTransform target_left = vg_transform_identity();
    target_left.tx = 0;
    vg_anim_transform_state_set_target(&state, target_left);
    VgTransform backtrack = vg_anim_transform_state_step(&state, 50u);
    TEST_ASSERT_TRUE(backtrack.tx < mid.tx);
    TEST_ASSERT_TRUE(backtrack.tx > 0);

    VgTransform settled = vg_anim_transform_state_step(&state, 200u);
    TEST_ASSERT_EQUAL_INT16(0, settled.tx);
}

TEST(test_vector_scene_graph_anim_transform_state_zero_response_snaps_immediately) {
    VgTransform initial = vg_transform_identity();
    VgAnimTransformState state = {0};
    vg_anim_transform_state_reset(&state, initial, 0u, VG_ANIM_EASE_OUT_QUAD);

    VgTransform target = vg_transform_identity();
    target.tx = -17;
    target.ty = 9;
    target.rot_deg = -45;
    target.sx = VG_SCALE_ONE / 2;
    target.sy = VG_SCALE_ONE * 2;
    vg_anim_transform_state_set_target(&state, target);

    VgTransform out = vg_anim_transform_state_step(&state, 1u);
    TEST_ASSERT_EQUAL_INT16(target.tx, out.tx);
    TEST_ASSERT_EQUAL_INT16(target.ty, out.ty);
    TEST_ASSERT_EQUAL_INT16(target.rot_deg, out.rot_deg);
    TEST_ASSERT_EQUAL_INT32(target.sx, out.sx);
    TEST_ASSERT_EQUAL_INT32(target.sy, out.sy);
}

TEST(test_vector_scene_graph_anim_fixed_in_out_quad_symmetry_samples) {
    int32_t t1 = VG_SCALE_ONE / 4;
    int32_t t2 = VG_SCALE_ONE - t1;
    int32_t y1 = vg_anim_ease_q13(VG_ANIM_EASE_IN_OUT_QUAD, t1);
    int32_t y2 = vg_anim_ease_q13(VG_ANIM_EASE_IN_OUT_QUAD, t2);

    /* Symmetry around 0.5: y(1-t) ~= 1-y(t) in fixed arithmetic. */
    TEST_ASSERT_INT_WITHIN(1, VG_SCALE_ONE - y1, y2);
}

TEST(test_vector_scene_graph_deterministic_frame_checksum_for_mixed_scene) {
    uint16_t pixels_a[TEST_W * TEST_H];
    uint16_t pixels_b[TEST_W * TEST_H];
    VgFrameBuffer fb_a;
    VgFrameBuffer fb_b;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_a, TEST_W, TEST_H, pixels_a, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_b, TEST_W, TEST_H, pixels_b, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_a, 0x0000u);
    vg_framebuffer_clear(&fb_b, 0x0000u);

    VgStyle white = vg_style_default();
    white.stroke_color = 0xffffu;
    VgStyle green = vg_style_default();
    green.stroke_color = 0x07e0u;
    green.stroke_width = 2;
    VgStyle red = vg_style_default();
    red.stroke_color = 0xf800u;

    VgPoint pts[] = {{10, 25}, {20, 30}, {30, 24}, {40, 30}};

    VgNode line = {
        .id = 21,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = white,
        .data.line = {.x1 = 2, .y1 = 2, .x2 = 30, .y2 = 12}
    };
    VgNode rect = {
        .id = 22,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = green,
        .data.rect = {.x = 6, .y = 18, .w = 20, .h = 10}
    };
    VgNode tri = {
        .id = 23,
        .type = VG_NODE_TRI,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = red,
        .data.tri = {.x1 = 45, .y1 = 6, .x2 = 52, .y2 = 20, .x3 = 37, .y3 = 20}
    };
    VgNode poly = {
        .id = 24,
        .type = VG_NODE_POLYLINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = white,
        .data.polyline = {.points = pts, .point_count = 4, .closed = false}
    };
    VgNode text = {
        .id = 25,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = white,
        .data.text = {.x = 34, .y = 30, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "HI"}
    };

    VgNode *children[] = {&line, &rect, &tri, &poly, &text};
    VgNode root = {
        .id = 20,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = children, .child_count = 5}
    };

    vg_render_scene(&root, &fb_a);
    vg_render_scene(&root, &fb_b);

    uint32_t checksum_a = vg_framebuffer_checksum(&fb_a);
    uint32_t checksum_b = vg_framebuffer_checksum(&fb_b);
    TEST_ASSERT_EQUAL_HEX32(checksum_a, checksum_b);
}

TEST(test_vector_scene_graph_clipped_render_limits_written_pixels) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;

    VgNode line = {
        .id = 26,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.line = {.x1 = 0, .y1 = 10, .x2 = 63, .y2 = 10}
    };

    VgClipRect clip = {.x = 20, .y = 8, .w = 10, .h = 6};
    vg_render_scene_clipped(&line, &fb, clip);

    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 10]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 24]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 35]);
}

TEST(test_vector_scene_graph_render_slot_if_changed_skips_unchanged_and_clears_moved_union) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x1234u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;

    VgNode line = {
        .id = 27,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.line = {.x1 = 2, .y1 = 2, .x2 = 8, .y2 = 2}
    };

    VgRenderSlot slot = {
        .root = &line,
        .clip_rect = {.x = 0, .y = 0, .w = 16, .h = 16},
        .z = 0,
        .visible = true,
        .opaque = true,
        .clear_color = 0x0000u,
        .guard_px = 0
    };
    VgRenderSlotState state = {0};

    TEST_ASSERT_TRUE(vg_render_slot_if_changed(&slot, &state, &fb, 1u));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)2 * TEST_W + 4]);
    uint32_t after_first = vg_framebuffer_checksum(&fb);

    TEST_ASSERT_FALSE(vg_render_slot_if_changed(&slot, &state, &fb, 1u));
    uint32_t after_second = vg_framebuffer_checksum(&fb);
    TEST_ASSERT_EQUAL_HEX32(after_first, after_second);

    line.has_transform = true;
    line.transform = vg_transform_identity();
    line.transform.tx = 20;
    slot.clip_rect.x = 20;
    slot.clip_rect.w = 16;

    TEST_ASSERT_TRUE(vg_render_slot_if_changed(&slot, &state, &fb, 2u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)2 * TEST_W + 4]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)2 * TEST_W + 24]);
}

TEST(test_vector_scene_graph_thick_line_changes_coverage) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle thin = vg_style_default();
    thin.stroke_color = 0xffffu;
    thin.stroke_width = 1;
    VgStyle thick = thin;
    thick.stroke_width = 4;

    VgNode line_thin = {
        .id = 31,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = thin,
        .data.line = {.x1 = 4, .y1 = 10, .x2 = 58, .y2 = 10}
    };
    VgNode line_thick = line_thin;
    line_thick.id = 32;
    line_thick.style = thick;
    line_thick.data.line.y1 = 26;
    line_thick.data.line.y2 = 26;

    VgNode *children[] = {&line_thin, &line_thick};
    VgNode root = {
        .id = 30,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = children, .child_count = 2}
    };

    vg_render_scene(&root, &fb);

    size_t colored = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(colored > 160);
}

TEST(test_vector_scene_graph_patch_updates_transform_visibility_style_and_text) {
    uint16_t pixels_before[TEST_W * TEST_H];
    uint16_t pixels_after[TEST_W * TEST_H];
    VgFrameBuffer fb_before;
    VgFrameBuffer fb_after;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_before, TEST_W, TEST_H, pixels_before, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_after, TEST_W, TEST_H, pixels_after, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_before, 0x0000u);
    vg_framebuffer_clear(&fb_after, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    VgNode text = {
        .id = 41,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 4, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "A"}
    };

    VgNode line = {
        .id = 42,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.line = {.x1 = 2, .y1 = 34, .x2 = 18, .y2 = 34}
    };

    VgNode *children[] = {&text, &line};
    VgNode root = {
        .id = 40,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = children, .child_count = 2}
    };

    vg_render_scene(&root, &fb_before);
    uint32_t before = vg_framebuffer_checksum(&fb_before);

    VgPatch tpatch = {.id = 42, .type = VG_PATCH_TRANSFORM};
    tpatch.value.transform = vg_transform_identity();
    tpatch.value.transform.tx = 20;
    tpatch.value.transform.ty = -8;
    TEST_ASSERT_TRUE(vg_scene_apply_patch(&root, &tpatch));

    VgPatch spatch = {.id = 42, .type = VG_PATCH_STYLE};
    spatch.value.style = style;
    spatch.value.style.stroke_color = 0xf800u;
    spatch.value.style.stroke_width = 3;
    TEST_ASSERT_TRUE(vg_scene_apply_patch(&root, &spatch));

    VgPatch vpatch = {.id = 41, .type = VG_PATCH_VISIBILITY};
    vpatch.value.visible = false;
    TEST_ASSERT_TRUE(vg_scene_apply_patch(&root, &vpatch));

    VgPatch txpatch = {.id = 41, .type = VG_PATCH_TEXT};
    txpatch.value.text = "CHANGED";
    TEST_ASSERT_TRUE(vg_scene_apply_patch(&root, &txpatch));

    vg_render_scene(&root, &fb_after);
    uint32_t after = vg_framebuffer_checksum(&fb_after);
    TEST_ASSERT_NOT_EQUAL(before, after);
}

TEST(test_vector_scene_graph_aa_only_when_bg_given_for_1px_lines) {
    uint16_t pixels_noaa[TEST_W * TEST_H];
    uint16_t pixels_aa[TEST_W * TEST_H];
    VgFrameBuffer fb_noaa;
    VgFrameBuffer fb_aa;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_noaa, TEST_W, TEST_H, pixels_noaa, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_aa, TEST_W, TEST_H, pixels_aa, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_noaa, 0x0000u);
    vg_framebuffer_clear(&fb_aa, 0x0000u);

    VgStyle noaa = vg_style_default();
    noaa.stroke_color = 0xffffu;
    noaa.stroke_width = 1;
    noaa.has_bg_color = false;

    VgStyle aa = noaa;
    aa.has_bg_color = true;
    aa.bg_color = 0x0000u;

    VgNode line_noaa = {
        .id = 51,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = noaa,
        .data.line = {.x1 = 5, .y1 = 5, .x2 = 58, .y2 = 37}
    };

    VgNode line_aa = line_noaa;
    line_aa.id = 52;
    line_aa.style = aa;

    vg_render_scene(&line_noaa, &fb_noaa);
    vg_render_scene(&line_aa, &fb_aa);

    size_t aa_intermediate = 0;
    for (size_t i = 0; i < TEST_W * TEST_H; i++) {
        uint16_t p = pixels_aa[i];
        if (p != 0x0000u && p != 0xffffu) {
            aa_intermediate++;
        }
    }
    TEST_ASSERT_TRUE(aa_intermediate > 0);

    size_t noaa_intermediate = 0;
    for (size_t i = 0; i < TEST_W * TEST_H; i++) {
        uint16_t p = pixels_noaa[i];
        if (p != 0x0000u && p != 0xffffu) {
            noaa_intermediate++;
        }
    }
    TEST_ASSERT_EQUAL_size_t(0, noaa_intermediate);

    size_t aa_ink = count_not_color(pixels_aa, TEST_W * TEST_H, 0x0000u);
    size_t noaa_ink = count_not_color(pixels_noaa, TEST_W * TEST_H, 0x0000u);
    TEST_ASSERT_TRUE(aa_ink > noaa_ink);
}

TEST(test_vector_scene_graph_hv_mono_has_inter_glyph_gap) {
    uint16_t pixels_a[TEST_W * TEST_H];
    uint16_t pixels_aa[TEST_W * TEST_H];
    VgFrameBuffer fb_a;
    VgFrameBuffer fb_aa;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_a, TEST_W, TEST_H, pixels_a, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_aa, TEST_W, TEST_H, pixels_aa, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_a, 0x0000u);
    vg_framebuffer_clear(&fb_aa, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;

    VgNode text_a = {
        .id = 61,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 0, .y = 0, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "A"}
    };
    VgNode text_aa = text_a;
    text_aa.id = 62;
    text_aa.data.text.text = "AA";

    vg_render_scene(&text_a, &fb_a);
    vg_render_scene(&text_aa, &fb_aa);

    size_t min_x_a = 0, min_y_a = 0, max_x_a = 0, max_y_a = 0;
    size_t min_x_aa = 0, min_y_aa = 0, max_x_aa = 0, max_y_aa = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_a, TEST_W, TEST_H, 0x0000u, &min_x_a, &min_y_a, &max_x_a, &max_y_a));
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_aa, TEST_W, TEST_H, 0x0000u, &min_x_aa, &min_y_aa, &max_x_aa, &max_y_aa));

    size_t width_a = max_x_a - min_x_a + 1;
    size_t width_aa = max_x_aa - min_x_aa + 1;
    TEST_ASSERT_TRUE(width_aa > width_a + 2);
}

TEST(test_vector_scene_graph_hv_mono_problem_glyphs_not_half_height) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;

    VgNode text = {
        .id = 62,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "MWXYR"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    size_t height = max_y - min_y + 1;
    // Regression intent: avoid accidentally squashing glyphs to half height.
    TEST_ASSERT_TRUE(height >= 7);
}

TEST(test_vector_scene_graph_punctuation_dot_and_colon_are_compact_blobs) {
    uint16_t pixels_dot[TEST_W * TEST_H];
    uint16_t pixels_colon[TEST_W * TEST_H];
    VgFrameBuffer fb_dot;
    VgFrameBuffer fb_colon;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_dot, TEST_W, TEST_H, pixels_dot, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_colon, TEST_W, TEST_H, pixels_colon, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_dot, 0x0000u);
    vg_framebuffer_clear(&fb_colon, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;

    VgNode dot = {
        .id = 71,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "."}
    };
    VgNode colon = dot;
    colon.id = 72;
    colon.data.text.text = ":";

    vg_render_scene(&dot, &fb_dot);
    vg_render_scene(&colon, &fb_colon);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_dot, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));
    size_t dot_w = max_x - min_x + 1;
    size_t dot_h = max_y - min_y + 1;
    /* Arcade period cell is rendered as a compact blob at the cell center. */
    TEST_ASSERT_TRUE(min_x >= 3);
    TEST_ASSERT_TRUE(dot_w <= 8);
    TEST_ASSERT_TRUE(dot_h >= 1);
    TEST_ASSERT_TRUE(count_not_color(pixels_dot, TEST_W * TEST_H, 0x0000u) >= 1);

    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_colon, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));
    size_t colon_w = max_x - min_x + 1;
    size_t colon_h = max_y - min_y + 1;
    /* Arcade colon: two compact blobs (one per filled cell). */
    TEST_ASSERT_TRUE(colon_w <= 10);
    TEST_ASSERT_TRUE(colon_h >= 4);
    TEST_ASSERT_TRUE(count_not_color(pixels_colon, TEST_W * TEST_H, 0x0000u) >= 2);
}

TEST(test_vector_scene_graph_comma_sits_lower_than_period_and_not_left) {
    uint16_t pixels_dot[TEST_W * TEST_H];
    uint16_t pixels_comma[TEST_W * TEST_H];
    VgFrameBuffer fb_dot;
    VgFrameBuffer fb_comma;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_dot, TEST_W, TEST_H, pixels_dot, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_comma, TEST_W, TEST_H, pixels_comma, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_dot, 0x0000u);
    vg_framebuffer_clear(&fb_comma, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;

    VgNode dot = {
        .id = 73,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "."}
    };
    VgNode comma = dot;
    comma.id = 74;
    comma.data.text.text = ",";

    vg_render_scene(&dot, &fb_dot);
    vg_render_scene(&comma, &fb_comma);

    size_t dot_min_x = 0, dot_min_y = 0, dot_max_x = 0, dot_max_y = 0;
    size_t comma_min_x = 0, comma_min_y = 0, comma_max_x = 0, comma_max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_dot, TEST_W, TEST_H, 0x0000u,
                                        &dot_min_x, &dot_min_y, &dot_max_x, &dot_max_y));
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels_comma, TEST_W, TEST_H, 0x0000u,
                                        &comma_min_x, &comma_min_y, &comma_max_x, &comma_max_y));

    TEST_ASSERT_TRUE(comma_max_y > dot_max_y);
    TEST_ASSERT_TRUE(comma_min_x >= dot_min_x);
}

TEST(test_vector_scene_graph_additional_ascii_punctuation_avoids_box_fallback) {
    typedef struct {
        const char *text;
        size_t max_width;
        size_t min_height;
        size_t min_ink;
    } GlyphExpect;

    const GlyphExpect cases[] = {
        {";", 10, 6, 5},
        {"!", 4, 8, 6},
        {"?", 6, 8, 9},
        {"(", 4, 9, 8},
        {")", 4, 9, 8},
    };

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;

    VgNode text = {
        .id = 81,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = NULL}
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        vg_framebuffer_clear(&fb, 0x0000u);
        text.id = 81u + (uint32_t)i;
        text.data.text.text = cases[i].text;
        vg_render_scene(&text, &fb);

        size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));
        size_t w = max_x - min_x + 1;
        size_t h = max_y - min_y + 1;
        size_t ink = count_not_color(pixels, TEST_W * TEST_H, 0x0000u);

        // Default fallback is a 6x11 box; these glyphs should be narrower and shaped.
        TEST_ASSERT_TRUE(w <= cases[i].max_width);
        TEST_ASSERT_TRUE(h >= cases[i].min_height);
        TEST_ASSERT_TRUE(ink >= cases[i].min_ink);
    }
}

TEST(test_vector_scene_graph_compact_punctuation_disables_aa_fringe_even_with_bg) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_color = true;
    style.bg_color = 0x0000u;

    VgNode text = {
        .id = 91,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = NULL}
    };

    const char *cases[] = {".", ":"};
    for (size_t ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ci++) {
        vg_framebuffer_clear(&fb, 0x0000u);
        text.id = 91u + (uint32_t)ci;
        text.data.text.text = cases[ci];
        vg_render_scene(&text, &fb);

        size_t intermediate = 0;
        size_t ink = 0;
        for (size_t i = 0; i < TEST_W * TEST_H; i++) {
            uint16_t p = pixels[i];
            if (p == 0x0000u) continue;
            ink++;
            if (p != 0xffffu) {
                intermediate++;
            }
        }
        TEST_ASSERT_TRUE(ink > 0);
        TEST_ASSERT_EQUAL_size_t(0, intermediate);
    }
}

TEST(test_vector_scene_graph_arcade_text_disables_aa_fringe_even_with_bg) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_color = true;
    style.bg_color = 0x0000u;

    VgNode text = {
        .id = 95,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "FPS: 59.9"}
    };

    vg_framebuffer_clear(&fb, 0x0000u);
    vg_render_scene(&text, &fb);

    size_t intermediate = 0;
    size_t ink = 0;
    for (size_t i = 0; i < TEST_W * TEST_H; i++) {
        uint16_t p = pixels[i];
        if (p == 0x0000u) continue;
        ink++;
        if (p != 0xffffu) {
            intermediate++;
        }
    }

    TEST_ASSERT_TRUE(ink > 0);
    TEST_ASSERT_EQUAL_size_t(0, intermediate);
}

TEST(test_vector_scene_graph_arcade_z_scale_1_5_diagonal_stays_within_expected_bounds) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_color = true;
    style.bg_color = 0x0000u;

    VgNode text = {
        .id = 96,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE * 3 / 2, .rot_deg = 0, .text = "Z"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    TEST_ASSERT_EQUAL_size_t(13, (max_x - min_x + 1));
    TEST_ASSERT_EQUAL_size_t(13, (max_y - min_y + 1));
}

TEST(test_vector_scene_graph_slash_backslash_scale_1_5_are_not_one_pixel_too_low) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_color = true;
    style.bg_color = 0x0000u;

    VgNode text = {
        .id = 97,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE * 3 / 2, .rot_deg = 0, .text = NULL}
    };

    const char *cases[] = {"/", "\\"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        vg_framebuffer_clear(&fb, 0x0000u);
        text.id = 97u + (uint32_t)i;
        text.data.text.text = cases[i];
        vg_render_scene(&text, &fb);

        size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));
        TEST_ASSERT_TRUE(min_x >= 3);
        TEST_ASSERT_EQUAL_size_t(16, max_y);
    }
}

TEST(test_vector_scene_graph_arcade_exclamation_has_detached_dot) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_color = true;
    style.bg_color = 0x0000u;

    VgNode text = {
        .id = 98,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "!"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    bool seen_ink = false;
    bool seen_gap_after_ink = false;
    bool seen_ink_after_gap = false;
    for (size_t y = min_y; y <= max_y; y++) {
        bool row_has_ink = false;
        for (size_t x = min_x; x <= max_x; x++) {
            if (pixels[y * TEST_W + x] != 0x0000u) {
                row_has_ink = true;
                break;
            }
        }
        if (row_has_ink) {
            if (seen_gap_after_ink) {
                seen_ink_after_gap = true;
            }
            seen_ink = true;
        } else if (seen_ink && !seen_ink_after_gap) {
            seen_gap_after_ink = true;
        }
    }

    TEST_ASSERT_TRUE(seen_gap_after_ink);
    TEST_ASSERT_TRUE(seen_ink_after_gap);
}

TEST(test_vector_scene_graph_colon_blobs_are_vertically_aligned) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_bg_color = true;
    style.bg_color = 0x0000u;

    VgNode text = {
        .id = 92,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = ":"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    size_t mid_y = (min_y + max_y) / 2;
    size_t top_sum_x = 0, top_count = 0;
    size_t bot_sum_x = 0, bot_count = 0;
    for (size_t y = min_y; y <= max_y; y++) {
        for (size_t x = min_x; x <= max_x; x++) {
            if (pixels[y * TEST_W + x] == 0x0000u) continue;
            if (y <= mid_y) {
                top_sum_x += x;
                top_count++;
            } else {
                bot_sum_x += x;
                bot_count++;
            }
        }
    }

    TEST_ASSERT_TRUE(top_count > 0);
    TEST_ASSERT_TRUE(bot_count > 0);
    int top_cx = (int)((top_sum_x + (top_count / 2)) / top_count);
    int bot_cx = (int)((bot_sum_x + (bot_count / 2)) / bot_count);
    int dx = top_cx - bot_cx;
    if (dx < 0) dx = -dx;
    TEST_ASSERT_TRUE(dx <= 1);
}

TEST(test_vector_scene_graph_arcade_j_has_bottom_bar_shape) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;

    VgNode text = {
        .id = 93,
        .type = VG_NODE_VTEXT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.text = {.x = 2, .y = 2, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = "J"}
    };

    vg_render_scene(&text, &fb);

    size_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    TEST_ASSERT_TRUE(find_non_bg_bounds(pixels, TEST_W, TEST_H, 0x0000u, &min_x, &min_y, &max_x, &max_y));

    size_t top_row_ink = 0;
    size_t bottom_row_ink = 0;
    for (size_t x = min_x; x <= max_x; x++) {
        if (pixels[min_y * TEST_W + x] != 0x0000u) top_row_ink++;
        if (pixels[max_y * TEST_W + x] != 0x0000u) bottom_row_ink++;
    }

    // Arcade J has the horizontal bar at the bottom (renderer y grows downward).
    TEST_ASSERT_TRUE(bottom_row_ink > top_row_ink);
}

/* ---------- M4b: Solid Fill Color Tests ---------- */

TEST(test_vector_scene_graph_filled_rect_produces_interior_pixels) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_color = 0x07e0u; /* green */

    VgNode rect = {
        .id = 1001,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 10, .y = 10, .w = 20, .h = 12}
    };

    vg_render_scene(&rect, &fb);

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0x07e0u);
    size_t stroke_count = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(fill_count > 0);
    TEST_ASSERT_TRUE(stroke_count > 0);
    TEST_ASSERT_TRUE(fill_count > stroke_count);
}

TEST(test_vector_scene_graph_filled_tri_produces_interior_pixels) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_color = 0xf800u; /* red */

    VgNode tri = {
        .id = 1002,
        .type = VG_NODE_TRI,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.tri = {.x1 = 32, .y1 = 4, .x2 = 50, .y2 = 40, .x3 = 14, .y3 = 40}
    };

    vg_render_scene(&tri, &fb);

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0xf800u);
    size_t stroke_count = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(fill_count > 0);
    TEST_ASSERT_TRUE(stroke_count > 0);
    TEST_ASSERT_TRUE(fill_count > stroke_count);
}

TEST(test_vector_scene_graph_filled_closed_polyline_produces_interior_pixels) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_color = 0x001fu; /* blue */

    VgPoint pts[] = {{10, 5}, {50, 5}, {50, 35}, {10, 35}};

    VgNode poly = {
        .id = 1003,
        .type = VG_NODE_POLYLINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.polyline = {.points = pts, .point_count = 4, .closed = true}
    };

    vg_render_scene(&poly, &fb);

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0x001fu);
    size_t stroke_count = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(fill_count > 0);
    TEST_ASSERT_TRUE(stroke_count > 0);
    TEST_ASSERT_TRUE(fill_count > stroke_count);
}

TEST(test_vector_scene_graph_open_polyline_does_not_fill) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_color = 0x001fu;

    VgPoint pts[] = {{10, 5}, {50, 5}, {50, 35}, {10, 35}};

    VgNode poly = {
        .id = 1004,
        .type = VG_NODE_POLYLINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.polyline = {.points = pts, .point_count = 4, .closed = false}
    };

    vg_render_scene(&poly, &fb);

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0x001fu);
    TEST_ASSERT_EQUAL_size_t(0, fill_count);
}

TEST(test_vector_scene_graph_fill_paint_order_fill_under_stroke) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 2;
    style.has_fill = true;
    style.fill_color = 0xf800u; /* red fill */

    VgNode rect = {
        .id = 1005,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 10, .y = 10, .w = 30, .h = 20}
    };

    vg_render_scene(&rect, &fb);

    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 10]);
    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0xf800u);
    size_t stroke_count = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(fill_count > 0);
    TEST_ASSERT_TRUE(stroke_count > 0);
}

TEST(test_vector_scene_graph_no_fill_when_has_fill_false) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = false;
    style.fill_color = 0xf800u;

    VgNode rect = {
        .id = 1006,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 10, .y = 10, .w = 20, .h = 12}
    };

    vg_render_scene(&rect, &fb);

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0xf800u);
    TEST_ASSERT_EQUAL_size_t(0, fill_count);
}

TEST(test_vector_scene_graph_filled_rect_clipped) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_color = 0x07e0u; /* green */

    VgNode rect = {
        .id = 1007,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 5, .y = 5, .w = 50, .h = 35}
    };

    VgClipRect clip = {.x = 20, .y = 15, .w = 10, .h = 10};
    vg_render_scene_clipped(&rect, &fb, clip);

    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 10]);
    size_t fill_in_clip = 0;
    for (int y = 15; y < 25; y++) {
        for (int x = 20; x < 30; x++) {
            if (pixels[y * TEST_W + x] != 0x0000u) fill_in_clip++;
        }
    }
    TEST_ASSERT_TRUE(fill_in_clip > 0);
    size_t outside_clip = count_not_color(pixels, TEST_W * TEST_H, 0x0000u) - fill_in_clip;
    TEST_ASSERT_EQUAL_size_t(0, outside_clip);
}

TEST(test_vector_scene_graph_filled_rect_deterministic_checksum) {
    uint16_t pixels_a[TEST_W * TEST_H];
    uint16_t pixels_b[TEST_W * TEST_H];
    VgFrameBuffer fb_a, fb_b;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_a, TEST_W, TEST_H, pixels_a, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb_b, TEST_W, TEST_H, pixels_b, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb_a, 0x0000u);
    vg_framebuffer_clear(&fb_b, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_color = 0x07e0u;

    VgNode rect = {
        .id = 1008,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 8, .y = 6, .w = 25, .h = 18}
    };

    vg_render_scene(&rect, &fb_a);
    vg_render_scene(&rect, &fb_b);

    TEST_ASSERT_EQUAL_HEX32(vg_framebuffer_checksum(&fb_a), vg_framebuffer_checksum(&fb_b));
}

TEST(test_vector_scene_graph_filled_rect_from_clojure_records) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Transform [tx ty sx sy rot]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Rect [id t style visible x y w h]) "
        "  (defrecord Group [id t style visible children]) "
        "  (defrecord Scene [root clip-rect erase-color collision-rules]) "
        "  (->Scene "
        "    (->Rect 1009 nil (->Style 65535 1 true true 2016 false 0) true 10 10 20 12) "
        "    nil nil nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record(scene, &fb));

    size_t fill_count = count_color(pixels, TEST_W * TEST_H, 0x07e0u);
    size_t stroke_count = count_color(pixels, TEST_W * TEST_H, 0xffffu);
    TEST_ASSERT_TRUE(fill_count > 0);
    TEST_ASSERT_TRUE(stroke_count > 0);
}

TEST(test_vector_scene_graph_decode_render_host_micro_benchmark) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID deco_scene = eval_string(
        "(do (require 'tiny-fx.game-demo) "
        "    (nth (tiny-fx.game-demo/create-demo-bundle) 0))",
        g_test_eval_state);
    ID score_scene = eval_string(
        "(do (require 'tiny-fx.game-demo) "
        "    (nth (tiny-fx.game-demo/create-demo-bundle) 1))",
        g_test_eval_state);
    ID game_scene = eval_string(
        "(do (require 'tiny-fx.game-demo) "
        "    (nth (tiny-fx.game-demo/create-demo-bundle) 2))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(deco_scene);
    TEST_ASSERT_NOT_NULL(score_scene);
    TEST_ASSERT_NOT_NULL(game_scene);

    enum { BENCH_W = 320, BENCH_H = 240 };
    uint16_t pixels[BENCH_W * BENCH_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, BENCH_W, BENCH_H, pixels, BENCH_W * BENCH_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    const unsigned warmup_iterations = 40u;
    const unsigned measured_iterations = 800u;

    uint64_t deco_ns = benchmark_scene_record_render_ns(deco_scene, &fb, warmup_iterations, measured_iterations);
    uint64_t score_ns = benchmark_scene_record_render_ns(score_scene, &fb, warmup_iterations, measured_iterations);
    uint64_t game_ns = benchmark_scene_record_render_ns(game_scene, &fb, warmup_iterations, measured_iterations);

    TEST_ASSERT_TRUE(deco_ns > 0u);
    TEST_ASSERT_TRUE(score_ns > 0u);
    TEST_ASSERT_TRUE(game_ns > 0u);

    double deco_total_ms = (double)deco_ns / 1e6;
    double score_total_ms = (double)score_ns / 1e6;
    double game_total_ms = (double)game_ns / 1e6;
    double deco_per_frame_ms = deco_total_ms / (double)measured_iterations;
    double score_per_frame_ms = score_total_ms / (double)measured_iterations;
    double game_per_frame_ms = game_total_ms / (double)measured_iterations;
    double deco_fps = ((double)measured_iterations * 1000.0) / deco_total_ms;
    double score_fps = ((double)measured_iterations * 1000.0) / score_total_ms;
    double game_fps = ((double)measured_iterations * 1000.0) / game_total_ms;

    printf("BENCH vector_scene_record/deco iterations=%u total_ms=%.3f per_frame_ms=%.6f fps=%.1f\n",
           measured_iterations, deco_total_ms, deco_per_frame_ms, deco_fps);
    printf("BENCH vector_scene_record/score iterations=%u total_ms=%.3f per_frame_ms=%.6f fps=%.1f\n",
           measured_iterations, score_total_ms, score_per_frame_ms, score_fps);
    printf("BENCH vector_scene_record/game iterations=%u total_ms=%.3f per_frame_ms=%.6f fps=%.1f\n",
           measured_iterations, game_total_ms, game_per_frame_ms, game_fps);
}

TEST(test_vector_scene_graph_runtime_vector_scene_bench_returns_metrics_map) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID result = eval_string(
        "(do (require 'tiny-clj.runtime) "
        "    (tiny-clj.runtime/vector-scene-bench 120 8))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_MAP_PERSISTENT, TAG(result));

    ID v_iterations = map_get(result, (ID)intern_symbol_global(":iterations"));
    ID v_warmup = map_get(result, (ID)intern_symbol_global(":warmup"));
    ID v_slot_count = map_get(result, (ID)intern_symbol_global(":slot-count"));
    ID v_deco_total_ms = map_get(result, (ID)intern_symbol_global(":deco-total-ms"));
    ID v_score_total_ms = map_get(result, (ID)intern_symbol_global(":score-total-ms"));
    ID v_game_total_ms = map_get(result, (ID)intern_symbol_global(":game-total-ms"));
    ID v_deco_us_pf = map_get(result, (ID)intern_symbol_global(":deco-us-per-frame"));
    ID v_score_us_pf = map_get(result, (ID)intern_symbol_global(":score-us-per-frame"));
    ID v_game_us_pf = map_get(result, (ID)intern_symbol_global(":game-us-per-frame"));
    ID v_total_ms = map_get(result, (ID)intern_symbol_global(":total-ms"));

    TEST_ASSERT_TRUE(is_fixnum(v_iterations));
    TEST_ASSERT_TRUE(is_fixnum(v_warmup));
    TEST_ASSERT_TRUE(is_fixnum(v_slot_count));
    TEST_ASSERT_TRUE(is_fixnum(v_deco_total_ms));
    TEST_ASSERT_TRUE(is_fixnum(v_score_total_ms));
    TEST_ASSERT_TRUE(is_fixnum(v_game_total_ms));
    TEST_ASSERT_TRUE(is_fixnum(v_deco_us_pf));
    TEST_ASSERT_TRUE(is_fixnum(v_score_us_pf));
    TEST_ASSERT_TRUE(is_fixnum(v_game_us_pf));
    TEST_ASSERT_TRUE(is_fixnum(v_total_ms));

    TEST_ASSERT_EQUAL_INT(120, as_fixnum(v_iterations));
    TEST_ASSERT_EQUAL_INT(8, as_fixnum(v_warmup));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(v_slot_count));
    TEST_ASSERT_TRUE(as_fixnum(v_deco_us_pf) >= 0);
    TEST_ASSERT_TRUE(as_fixnum(v_score_us_pf) >= 0);
    TEST_ASSERT_TRUE(as_fixnum(v_game_us_pf) >= 0);
    TEST_ASSERT_TRUE(as_fixnum(v_total_ms) >= 0);
}

TEST(test_vector_scene_graph_runtime_vector_scene_bench_arity_and_arg_validation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    bool arity_exception_caught = false;
    TRY {
        (void)eval_string(
            "(do (require 'tiny-clj.runtime) "
            "    (tiny-clj.runtime/vector-scene-bench 1 2 3))",
            g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected arity exception for vector-scene-bench");
    } CATCH(ex) {
        arity_exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArityException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(arity_exception_caught);

    bool arg_exception_caught = false;
    TRY {
        (void)eval_string(
            "(do (require 'tiny-clj.runtime) "
            "    (tiny-clj.runtime/vector-scene-bench \"x\"))",
            g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected argument exception for vector-scene-bench");
    } CATCH(ex) {
        arg_exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("IllegalArgumentException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(arg_exception_caught);
}

TEST(test_vector_scene_graph_tiny_fx_runtime_frontend_aliases_are_direct_vars) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID result = eval_string(
        "(do (require 'tiny-clj.runtime) "
        "    (require 'tiny-fx.gfx) "
        "    (require 'tiny-fx.gfx-scene) "
        "    (require 'tiny-fx.gfx-collision) "
        "    [(identical? tiny-fx.gfx/vector-scene-bench tiny-clj.runtime/vector-scene-bench) "
        "     (identical? tiny-fx.gfx/start-renderer! tiny-clj.runtime/start-renderer!) "
        "     (identical? tiny-fx.gfx/renderer-state tiny-clj.runtime/renderer-state) "
        "     (identical? tiny-fx.gfx/color tiny-fx.gfx-scene/color) "
        "     (identical? tiny-fx.gfx/normalize-spatial-rule tiny-fx.gfx-scene/normalize-spatial-rule) "
        "     (identical? tiny-fx.gfx/set-collision-callback! tiny-fx.gfx-collision/set-collision-callback!)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(6, vector_count(vec));
    for (unsigned int i = 0; i < vector_count(vec); i++) {
        TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(vec, i));
    }
}

TEST(test_vector_scene_graph_runtime_renderer_lifecycle_defaults_to_unsupported) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID result = eval_string(
        "(do (require 'tiny-clj.runtime) "
        "    [(tiny-clj.runtime/start-renderer!) "
        "     (tiny-clj.runtime/stop-renderer!) "
        "     (tiny-clj.runtime/start-renderer! [])])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    TEST_ASSERT_TRUE(vector_nth(vec, 0) == clj_false);
    TEST_ASSERT_TRUE(vector_nth(vec, 1) == clj_false);
    TEST_ASSERT_TRUE(vector_nth(vec, 2) == clj_false);
}

TEST(test_vector_scene_graph_native_runtime_renderer_builtins_link_and_defaults) {
    tiny_renderer_lifecycle_set_callbacks(NULL, NULL, NULL);
    vg_rendered_state_reset_all();
    builtins_tiny_fx_gfx_reset_cached_state();

    TEST_ASSERT_TRUE(native_tinyclj_runtime_start_renderer(NULL, 0) == clj_false);
    TEST_ASSERT_TRUE(native_tinyclj_runtime_stop_renderer(NULL, 0) == clj_false);

    ID slot = intern_symbol_global(":game");
    ID entity_id = fixnum(3001);
    ID field = intern_symbol_global(":t");

    ID state_args[] = {slot, entity_id};
    ID timeline_args[] = {slot, entity_id, field};

    TEST_ASSERT_NIL(native_tinyclj_runtime_renderer_state(state_args, 2));
    TEST_ASSERT_NIL(native_tinyclj_runtime_renderer_timeline_step(timeline_args, 3));
    TEST_ASSERT_NIL(native_tinyclj_runtime_renderer_timeline_progress(timeline_args, 3));
}

TEST(test_vector_scene_graph_custom_renderer_slot_binding_accepts_transient_descriptor_inputs) {
    tiny_renderer_lifecycle_set_callbacks(NULL, NULL, NULL);
    vg_rendered_state_reset_all();
    builtins_tiny_fx_gfx_reset_cached_state();

    CljPersistentMap *slot_desc = AUTORELEASE(make_map(2));
    CljPersistentVector *slot_descs = AUTORELEASE(make_vector(1, STRONG));
    CljAtom *scene_atom = AUTORELEASE(make_atom(NULL));
    ID kw_id = intern_symbol_global(":id");
    ID kw_atom = intern_symbol_global(":atom");
    ID slot_id = intern_symbol_global(":playfield");

    map_assoc_inplace(&slot_desc, kw_id, slot_id);
    map_assoc_inplace(&slot_desc, kw_atom, scene_atom);
    vector_conj_inplace(&slot_descs, slot_desc);

    ID start_args[] = {slot_descs};
    TEST_ASSERT_TRUE(native_tinyclj_runtime_start_renderer(start_args, 1) == clj_false);

    ID state_args[] = {slot_id, fixnum(9101)};
    TEST_ASSERT_NIL(native_tinyclj_runtime_renderer_state(state_args, 2));
}

TEST(test_vector_scene_graph_tiny_fx_runtime_renderer_lifecycle_defaults_to_unsupported) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID result = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "    [(tiny-fx.gfx/start-renderer!) "
        "     (tiny-fx.gfx/stop-renderer!) "
        "     (tiny-fx.gfx/start-renderer! [])])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    TEST_ASSERT_TRUE(vector_nth(vec, 0) == clj_false);
    TEST_ASSERT_TRUE(vector_nth(vec, 1) == clj_false);
    TEST_ASSERT_TRUE(vector_nth(vec, 2) == clj_false);
}

TEST(test_vector_scene_graph_runtime_renderer_lifecycle_arity_and_arg_validation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    bool start_arity_caught = false;
    TRY {
        (void)eval_string(
            "(do (require 'tiny-clj.runtime) "
            "    (tiny-clj.runtime/start-renderer! [] []))",
            g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected arity exception for start-renderer!");
    } CATCH(ex) {
        start_arity_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArityException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(start_arity_caught);

    bool start_arg_caught = false;
    TRY {
        (void)eval_string(
            "(do (require 'tiny-clj.runtime) "
            "    (tiny-clj.runtime/start-renderer! :bad))",
            g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected argument exception for start-renderer!");
    } CATCH(ex) {
        start_arg_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("IllegalArgumentException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(start_arg_caught);

    bool stop_arity_caught = false;
    TRY {
        (void)eval_string(
            "(do (require 'tiny-clj.runtime) "
            "    (tiny-clj.runtime/stop-renderer! 1))",
            g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected arity exception for stop-renderer!");
    } CATCH(ex) {
        stop_arity_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArityException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(stop_arity_caught);
}

TEST(test_vector_scene_graph_runtime_rendered_state_queries_return_nil_without_captured_frames) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID result = eval_string(
        "(do (require 'tiny-clj.runtime) "
        "    (require 'tiny-fx.gfx) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.gfx/slot-descriptors)) "
        "    [(tiny-clj.runtime/renderer-state :game 3001) "
        "     (tiny-clj.runtime/renderer-timeline-step :game 3001 :t) "
        "     (tiny-clj.runtime/renderer-timeline-progress :game 3001 :t)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    TEST_ASSERT_TRUE(vector_nth(vec, 0) == NULL);
    TEST_ASSERT_TRUE(vector_nth(vec, 1) == NULL);
    TEST_ASSERT_TRUE(vector_nth(vec, 2) == NULL);
}

TEST(test_vector_scene_graph_tiny_fx_runtime_rendered_state_queries_return_nil_without_captured_frames) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID result = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "    (tiny-fx.gfx/start-renderer! (tiny-fx.gfx/slot-descriptors)) "
        "    [(tiny-fx.gfx/renderer-state :game 3001) "
        "     (tiny-fx.gfx/renderer-timeline-step :game 3001 :t) "
        "     (tiny-fx.gfx/renderer-timeline-progress :game 3001 :t)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    TEST_ASSERT_TRUE(vector_nth(vec, 0) == NULL);
    TEST_ASSERT_TRUE(vector_nth(vec, 1) == NULL);
    TEST_ASSERT_TRUE(vector_nth(vec, 2) == NULL);
}

TEST(test_vector_scene_graph_runtime_rendered_state_queries_return_captured_values) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    vg_rendered_state_reset_all();

    VgTransformFixed world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = (12 << CLJ_FIXED_FRAC_BITS),
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = -(7 << CLJ_FIXED_FRAC_BITS),
    };
    VgRenderedTimelineSample sample = {
        .step_index = 3u,
        .keyframe_count = 9u,
        .phase_ms = 450u,
        .period_ms = 1800u,
        .loop = true,
    };

    vg_rendered_state_capture_begin(2u, 17u, 1234u);
    vg_rendered_state_capture_record_entity((uintptr_t)fixnum(3001), world_t);
    vg_rendered_state_capture_record_timeline((uintptr_t)fixnum(3001), VG_RENDERED_FIELD_T, sample);
    vg_rendered_state_capture_commit();

    ID result = eval_string(
        "(do (require 'tiny-clj.runtime) "
        "    (require 'tiny-fx.gfx) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.gfx/slot-descriptors)) "
        "    [(tiny-clj.runtime/renderer-state :game 3001) "
        "     (tiny-clj.runtime/renderer-timeline-step :game 3001 :t) "
        "     (tiny-clj.runtime/renderer-timeline-progress :game 3001 :t)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));

    ID state_map = vector_nth(vec, 0);
    TEST_ASSERT_NOT_NULL(state_map);
    TEST_ASSERT_TRUE(is_map(state_map));
    TEST_ASSERT_EQUAL_INT(12, as_fixnum(map_get(state_map, (ID)intern_symbol_global(":tx"))));
    TEST_ASSERT_EQUAL_INT(-7, as_fixnum(map_get(state_map, (ID)intern_symbol_global(":ty"))));
    TEST_ASSERT_EQUAL_INT(17, as_fixnum(map_get(state_map, (ID)intern_symbol_global(":snapshot-gen"))));
    TEST_ASSERT_EQUAL_INT(1234, as_fixnum(map_get(state_map, (ID)intern_symbol_global(":ts-ms"))));

    ID step = vector_nth(vec, 1);
    TEST_ASSERT_NOT_NULL(step);
    TEST_ASSERT_TRUE(is_fixnum(step));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(step));

    ID progress_map = vector_nth(vec, 2);
    TEST_ASSERT_NOT_NULL(progress_map);
    TEST_ASSERT_TRUE(is_map(progress_map));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":step"))));
    TEST_ASSERT_EQUAL_INT(9, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":count"))));
    TEST_ASSERT_EQUAL_INT(450, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":phase-ms"))));
    TEST_ASSERT_EQUAL_INT(1800, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":period-ms"))));
    TEST_ASSERT_EQUAL_INT(250, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":permille"))));
    TEST_ASSERT_TRUE(map_get(progress_map, (ID)intern_symbol_global(":loop")) == clj_true);

    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_runtime_rendered_state_queries_accept_registered_dynamic_slot_ids) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    vg_rendered_state_reset_all();

    VgTransformFixed world_t = {
        .m00 = VG_SCALE_ONE,
        .m01 = 0,
        .m02 = (5 << CLJ_FIXED_FRAC_BITS),
        .m10 = 0,
        .m11 = VG_SCALE_ONE,
        .m12 = (9 << CLJ_FIXED_FRAC_BITS),
    };
    VgRenderedTimelineSample sample = {
        .step_index = 1u,
        .keyframe_count = 4u,
        .phase_ms = 125u,
        .period_ms = 500u,
        .loop = false,
    };

    vg_rendered_state_capture_begin(0u, 23u, 777u);
    vg_rendered_state_capture_record_entity((uintptr_t)fixnum(9101), world_t);
    vg_rendered_state_capture_record_timeline((uintptr_t)fixnum(9101), VG_RENDERED_FIELD_T, sample);
    vg_rendered_state_capture_commit();

    ID result = eval_string(
        "(do (require 'tiny-clj.runtime) "
        "    (def dynamic-slot-scene* (atom nil)) "
        "    (tiny-clj.runtime/start-renderer! [{:id :playfield :atom dynamic-slot-scene*}]) "
        "    [(tiny-clj.runtime/renderer-state :playfield 9101) "
        "     (tiny-clj.runtime/renderer-timeline-step :playfield 9101 :t) "
        "     (tiny-clj.runtime/renderer-timeline-progress :playfield 9101 :t)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));

    ID state_map = vector_nth(vec, 0);
    TEST_ASSERT_NOT_NULL(state_map);
    TEST_ASSERT_TRUE(is_map(state_map));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(map_get(state_map, (ID)intern_symbol_global(":tx"))));
    TEST_ASSERT_EQUAL_INT(9, as_fixnum(map_get(state_map, (ID)intern_symbol_global(":ty"))));
    TEST_ASSERT_EQUAL_INT(23, as_fixnum(map_get(state_map, (ID)intern_symbol_global(":snapshot-gen"))));

    ID step = vector_nth(vec, 1);
    TEST_ASSERT_NOT_NULL(step);
    TEST_ASSERT_TRUE(is_fixnum(step));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(step));

    ID progress_map = vector_nth(vec, 2);
    TEST_ASSERT_NOT_NULL(progress_map);
    TEST_ASSERT_TRUE(is_map(progress_map));
    TEST_ASSERT_EQUAL_INT(125, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":phase-ms"))));
    TEST_ASSERT_EQUAL_INT(500, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":period-ms"))));
    TEST_ASSERT_EQUAL_INT(250, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":permille"))));

    ID restore_result = eval_string(
        "(do (require 'tiny-clj.runtime) "
        "    (require 'tiny-fx.gfx) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.gfx/slot-descriptors)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(restore_result);

    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_runtime_rendered_state_queries_non_timeline_fields_return_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    vg_rendered_state_reset_all();

    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Timeline [keyframes loop]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 7101 nil (->Style 65535 1 true false 0 false 0) true "
        "            (->Timeline [[0 4] [100 14]] false) 6 20 6) "
        "    [0 0 64 48] "
        "    0 true true 0 0 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgRenderSlotState state = {0};
    uint32_t dirty_pixels = 0u;
    vg_rendered_state_capture_begin(2u, 91u, 150u);
    bool rendered = vg_render_frame_slot_record_if_changed_at_ms(scene, &state, &fb, 1u, 150u, &dirty_pixels);
    if (rendered) {
        vg_rendered_state_capture_commit();
    } else {
        vg_rendered_state_capture_discard();
    }
    TEST_ASSERT_TRUE(rendered);

    ID result = eval_string(
        "(do (require 'tiny-clj.runtime) "
        "    (require 'tiny-fx.gfx) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.gfx/slot-descriptors)) "
        "    [(tiny-clj.runtime/renderer-timeline-step :game 7101 :x2) "
        "     (tiny-clj.runtime/renderer-timeline-progress :game 7101 :x2)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
    TEST_ASSERT_TRUE(vector_nth(vec, 0) == NULL);
    TEST_ASSERT_TRUE(vector_nth(vec, 1) == NULL);

    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_runtime_rendered_state_queries_non_loop_timeline_clamps_at_end) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    vg_rendered_state_reset_all();

    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "  (defrecord Timeline [keyframes loop]) "
        "  (defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]) "
        "  (defrecord Line [id t style visible x1 y1 x2 y2]) "
        "  (defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]) "
        "  (->FrameScene "
        "    (->Line 7201 nil (->Style 65535 1 true false 0 false 0) true "
        "            (->Timeline [[0 4] [100 14]] false) 8 20 8) "
        "    [0 0 64 48] "
        "    0 true true 0 0 nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgRenderSlotState state = {0};
    uint32_t dirty_pixels = 0u;
    vg_rendered_state_capture_begin(2u, 92u, 150u);
    bool rendered = vg_render_frame_slot_record_if_changed_at_ms(scene, &state, &fb, 1u, 150u, &dirty_pixels);
    if (rendered) {
        vg_rendered_state_capture_commit();
    } else {
        vg_rendered_state_capture_discard();
    }
    TEST_ASSERT_TRUE(rendered);

    ID result = eval_string(
        "(do (require 'tiny-clj.runtime) "
        "    (require 'tiny-fx.gfx) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.gfx/slot-descriptors)) "
        "    [(tiny-clj.runtime/renderer-timeline-step :game 7201 :x1) "
        "     (tiny-clj.runtime/renderer-timeline-progress :game 7201 :x1)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec));

    ID step = vector_nth(vec, 0);
    TEST_ASSERT_NOT_NULL(step);
    TEST_ASSERT_TRUE(is_fixnum(step));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(step));

    ID progress_map = vector_nth(vec, 1);
    TEST_ASSERT_NOT_NULL(progress_map);
    TEST_ASSERT_TRUE(is_map(progress_map));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":step"))));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":count"))));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":phase-ms"))));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":period-ms"))));
    TEST_ASSERT_EQUAL_INT(1000, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":permille"))));
    TEST_ASSERT_TRUE(map_get(progress_map, (ID)intern_symbol_global(":loop")) == clj_false);
    TEST_ASSERT_EQUAL_INT(92, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":snapshot-gen"))));
    TEST_ASSERT_EQUAL_INT(150, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":ts-ms"))));

    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_runtime_rendered_state_queries_validate_arity_and_args) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    bool state_arity_caught = false;
    TRY {
        (void)eval_string(
            "(do (require 'tiny-clj.runtime) "
            "    (tiny-clj.runtime/renderer-state :game))",
            g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected arity exception for renderer-state");
    } CATCH(ex) {
        state_arity_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArityException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(state_arity_caught);

    bool state_arg_caught = false;
    TRY {
        (void)eval_string(
            "(do (require 'tiny-clj.runtime) "
            "    (tiny-clj.runtime/renderer-state :bad 3001))",
            g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected argument exception for renderer-state slot");
    } CATCH(ex) {
        state_arg_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("IllegalArgumentException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(state_arg_caught);

    bool step_arg_caught = false;
    TRY {
        (void)eval_string(
            "(do (require 'tiny-clj.runtime) "
            "    (require 'tiny-fx.gfx) "
            "    (tiny-clj.runtime/start-renderer! (tiny-fx.gfx/slot-descriptors)) "
            "    (tiny-clj.runtime/renderer-timeline-step :game 3001 :bogus-field))",
            g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected argument exception for renderer-timeline-step field");
    } CATCH(ex) {
        step_arg_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("IllegalArgumentException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(step_arg_caught);

    bool progress_arity_caught = false;
    TRY {
        (void)eval_string(
            "(do (require 'tiny-clj.runtime) "
            "    (tiny-clj.runtime/renderer-timeline-progress :game 3001))",
            g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected arity exception for renderer-timeline-progress");
    } CATCH(ex) {
        progress_arity_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArityException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(progress_arity_caught);
}

TEST(test_vector_scene_graph_collision_detect_aabb_overlap_bounds) {
    VgAabb player = {.min_x = 58, .max_x = 86, .min_y = 124, .max_y = 146};
    VgAabb overlap_obstacle = {.min_x = 53, .max_x = 67, .min_y = 106, .max_y = 146};
    VgAabb far_obstacle = {.min_x = 213, .max_x = 227, .min_y = 106, .max_y = 146};
    VgAabb high_obstacle = {.min_x = 53, .max_x = 67, .min_y = 10, .max_y = 40};

    TEST_ASSERT_TRUE(vg_collision_detect_aabb_overlap(&player, &overlap_obstacle));
    TEST_ASSERT_FALSE(vg_collision_detect_aabb_overlap(&player, &far_obstacle));
    TEST_ASSERT_FALSE(vg_collision_detect_aabb_overlap(&player, &high_obstacle));
}

TEST(test_vector_scene_graph_collision_step_latch_and_cooldown) {
    VgCollisionState state = {0};

    bool toggled = vg_collision_step_latched_cooldown(&state, 1000u, 300u, true);
    TEST_ASSERT_TRUE(toggled);
    TEST_ASSERT_TRUE(state.collision_latched);
    TEST_ASSERT_EQUAL_UINT32(1300u, state.collision_cooldown_end_ms);

    toggled = vg_collision_step_latched_cooldown(&state, 1010u, 300u, true);
    TEST_ASSERT_FALSE(toggled);
    TEST_ASSERT_TRUE(state.collision_latched);

    toggled = vg_collision_step_latched_cooldown(&state, 1020u, 300u, false);
    TEST_ASSERT_FALSE(toggled);
    TEST_ASSERT_FALSE(state.collision_latched);

    toggled = vg_collision_step_latched_cooldown(&state, 1200u, 300u, true);
    TEST_ASSERT_FALSE(toggled);
    TEST_ASSERT_FALSE(state.collision_latched);

    toggled = vg_collision_step_latched_cooldown(&state, 1300u, 300u, true);
    TEST_ASSERT_TRUE(toggled);
    TEST_ASSERT_TRUE(state.collision_latched);
    TEST_ASSERT_EQUAL_UINT32(1600u, state.collision_cooldown_end_ms);
}
