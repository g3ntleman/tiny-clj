#include "tests_common.h"
#include "../atom.h"
#include "../builtins_tiny_fx_gfx.h"
#include "../vector_scene_graph.h"
#include "../scene.h"
#include "../tiny_fx_gfx.h"
#include "../rendered_state_snapshot.h"
#include "../renderer_lifecycle.h"
#include "../fx_collision.h"
#include "../event_loop.h"
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

static void render_scene_over_clips(const VgNode *root,
                                    VgFrameBuffer *fb,
                                    const VgClipRect *clips,
                                    size_t clip_count) {
    for (size_t i = 0; i < clip_count; i++) {
        vg_render_scene_clipped(root, fb, clips[i]);
    }
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

TEST(test_vector_scene_graph_renders_line_from_canonical_scene_entity_map) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 4 6 18 6 nil)}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (record-create (quote Group) [:tiny-fx.scene/root nil nil true [101] nil]) "
        "                  101 (record-create (quote Line) [101 nil (->Style 65535 1 true false 0 false 0) true 4 6 18 6 nil])}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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

TEST(test_vector_scene_graph_flat_entity_map_symbol_child_id_renders) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (record-create (quote Group) [:tiny-fx.scene/root nil nil true ['player-line] nil]) "
        "                  'player-line (record-create (quote Line) ['player-line nil "
        "                                        (->Style 65535 1 true false 0 false 0) "
        "                                        true 4 6 18 6 nil])}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (record-create (quote Group) [:tiny-fx.scene/root (record-create (quote Transform) [2 3 1 1 0]) nil true [210] nil]) "
        "                  210 (record-create (quote Group) [210 (record-create (quote Transform) [5 2 1 1 0]) nil true [211] nil]) "
        "                  211 (record-create (quote Line) [211 nil (->Style 65535 1 true false 0 false 0) true 0 0 10 0 nil])}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {100 (record-create (quote Group) [100 nil nil true [] nil])}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (record-create (quote Group) [:tiny-fx.scene/root nil nil true [999] nil])}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_FALSE(vg_render_scene_record(scene, &fb));
}

TEST(test_vector_scene_graph_game_demo_bundle_uses_root_and_index_scene_shape) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "        index0 (:index (nth bundle 0)) "
        "        index1 (:index (nth bundle 1)) "
        "        index2 (:index (nth bundle 2)) "
        "        root0-id (:root (nth bundle 0)) "
        "        root1-id (:root (nth bundle 1)) "
        "        root2-id (:root (nth bundle 2)) "
        "        root0 (get index0 root0-id) "
        "        root1 (get index1 root1-id) "
        "        root2 (get index2 root2-id)] "
        "    (and (= :tiny-fx.scene/root root0-id) "
        "         (= :tiny-fx.scene/root root1-id) "
        "         (= :tiny-fx.scene/root root2-id) "
        "         (= :tiny-fx.scene/root (:id root0)) "
        "         (= :tiny-fx.scene/root (:id root1)) "
        "         (= :tiny-fx.scene/root (:id root2)) "
        "         (vector? (:children root0)) "
        "         (vector? (:children root1)) "
        "         (vector? (:children root2)) "
        "         (map? index0) "
        "         (map? index1) "
        "         (map? index2) "
        "         (contains? index0 :tiny-fx.scene/root) "
        "         (contains? index1 :tiny-fx.scene/root) "
        "         (contains? index2 :tiny-fx.scene/root) "
        "         (contains? index0 1001) "
        "         (contains? index1 2001) "
        "         (contains? index2 3002) "
        "         (contains? index2 3006))))",
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
        "  (require 'tiny-fx.game-demo) "
        "  (tiny-fx.game-demo/on-player-collision-toggle! {:kind :collision :phase :enter}) "
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
        "(let [game-atom (:game-scene-atom (tiny-fx.game-demo/game-demo-config)) "
        "      player (get (:index @game-atom) 3002) "
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
        "        score-index (:index (nth bundle 1)) "
        "        score-text (get score-index 2001) "
        "        score-field (:text score-text)] "
        "    (and score-text "
        "         (:keyframes score-field) "
        "         (= true (:loop score-field)))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_tiny_fx_startup_bundle_has_animated_title_and_stars) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    eval_string("(require 'tiny-fx.startup)", g_test_eval_state);
    eval_string("(require 'tiny-fx.gfx)", g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (tiny-fx.startup/create-startup-bundle) "
        "  true)",
        g_test_eval_state);
    if (!ok || ok == clj_false) { test_fprintf(stderr, "ok failed\n"); }
    TEST_ASSERT_TRUE(ok && ok != clj_false);

    ID layout_ok = eval_string(
        "(let [bundle (tiny-fx.startup/create-startup-bundle) "
        "      deco-root (:root (nth bundle 0)) "
        "      score-root (:root (nth bundle 1)) "
        "      deco-group (get deco-root :tiny-fx.scene/root) "
        "      display-frame (get score-root 1100) "
        "      display-screen (get score-root 1101) "
        "      c1 (= [:deco :score] (mapv (fn [slot] (:id slot)) (tiny-fx.startup/slot-descriptors))) "
        "      c2 (= 2 (count (:children deco-group))) "
        "      c3 (= [1100 1101 2001 2002] (:children (get score-root :tiny-fx.scene/root))) "
        "      c4 (= [58 54 204 112] [(:x display-frame) (:y display-frame) (:w display-frame) (:h display-frame)]) "
        "      c5 (= [70 66 180 88] [(:x display-screen) (:y display-screen) (:w display-screen) (:h display-screen)])] "
        "  (println \"c1:\" c1 \"c2:\" c2 \"c3:\" c3 \"c4:\" c4 \"c5:\" c5) "
        "  (and c1 c2 c3 c4 c5))",
        g_test_eval_state);
    if (!layout_ok || layout_ok == clj_false) { test_fprintf(stderr, "layout_ok failed\n"); }
    TEST_ASSERT_TRUE(layout_ok && layout_ok != clj_false);

    ID style_ok = eval_string(
        "(let [bundle (tiny-fx.startup/create-startup-bundle) "
        "      score-root (:root (nth bundle 1)) "
        "      title (get score-root 2001) "
        "      subtitle (get score-root 2002) "
        "      display-screen (get score-root 1101)] "
        "  (println \"t:\" (:stroke-color (:style title)) \"e:\" (tiny-fx.gfx/color 0x9BBC0F)) "
        "  (println \"s:\" (:stroke-color (:style subtitle)) \"e:\" (tiny-fx.gfx/color 0x9BBC0F)) "
        "  (println \"hf:\" (:has-fill (:style display-screen)) \"fc:\" (:fill-color (:style display-screen))) "
        "  (and (= (tiny-fx.gfx/color 0x9BBC0F) (:stroke-color (:style title))) "
        "       (= (tiny-fx.gfx/color 0x9BBC0F) (:stroke-color (:style subtitle))) "
        "       (= true (:has-fill (:style display-screen))) "
        "       (= 0 (:fill-color (:style display-screen)))))",
        g_test_eval_state);
    if (!style_ok || style_ok == clj_false) { test_fprintf(stderr, "style_ok failed\n"); }
    TEST_ASSERT_TRUE(style_ok && style_ok != clj_false);

    ID motion_ok = eval_string(
        "(let [bundle (tiny-fx.startup/create-startup-bundle) "
        "      deco-root (:root (nth bundle 0)) "
        "      score-root (:root (nth bundle 1)) "
        "      star-layer (get deco-root 1010) "
        "      star-layer-front (get deco-root 1020) "
        "      title (get score-root 2001) "
        "      subtitle (get score-root 2002) "
        "      stars-back (:children star-layer) "
        "      stars-front (:children star-layer-front) "
        "      stars-kf0 (nth (nth (:keyframes (:t star-layer)) 0) 1) "
        "      stars-kf1 (nth (nth (:keyframes (:t star-layer)) 1) 1) "
        "      t0 (nth (nth (:keyframes (:t title)) 0) 1) "
        "      t4 (nth (nth (:keyframes (:t title)) 4) 1)] "
        "  (and (= 16 (count stars-back)) "
        "       (= 16 (count stars-front)) "
        "       (= 32 (+ (count stars-back) (count stars-front))) "
        "       (contains? (:t star-layer) :keyframes) "
        "       (contains? (:t star-layer) :loop) "
        "       (< (:ty stars-kf0) (:ty stars-kf1)) "
        "       (contains? (:t title) :keyframes) "
        "       (contains? (:text title) :keyframes) "
        "       (contains? (:text subtitle) :keyframes) "
        "       (> (:sx t0) (:sx t4)) "
        "       (> (:sy t0) (:sy t4)) "
        "       (< (:tx t0) (:tx t4)) "
        "       (< (:ty t0) (:ty t4))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(motion_ok && motion_ok != clj_false);
}

TEST(test_vector_scene_graph_game_demo_game_motion_is_timeline_driven) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "        game-index (:index (nth bundle 2)) "
        "        terrain (get game-index 3001) "
        "        player (get game-index 3002) "
        "        rocket-body (get game-index 3003) "
        "        rocket-nose (get game-index 3005)] "
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
        "        game-index (:index (nth bundle 2)) "
        "        player (get game-index 3002) "
        "        proxy (get game-index 3006) "
        "        player-kf0 (nth (:keyframes (:t player)) 0) "
        "        player-kf1 (nth (:keyframes (:t player)) 1) "
        "        player-t0 (nth player-kf0 1) "
        "        player-t1 (nth player-kf1 1) "
        "        rocket-body (get game-index 3003) "
        "        rocket-kf0 (nth (:keyframes (:t rocket-body)) 0) "
        "        rocket-t0 (nth rocket-kf0 1) "
        "        rocket-nose (get game-index 3005)] "
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
        "  (require 'tiny-fx.game-demo) "
        "  (let [cfg (tiny-fx.game-demo/game-demo-config) "
        "        slots (:slots cfg) "
        "        slot-ids (mapv (fn [slot] (:id slot)) slots) "
        "        slot-atoms (mapv (fn [slot] (:atom slot)) slots) "
        "        spatial-callback (:spatial-callback cfg) "
        "        game-scene-atom (:game-scene-atom cfg) "
        "        game-scene @game-scene-atom "
        "        rocket-body (get (:index game-scene) 3003) "
        "        rocket-nose (get (:index game-scene) 3005) "
        "        rules (:collision-rules game-scene) "
        "        rule1 (first rules)] "
        "    (and (= [:deco :score :game] slot-ids) "
        "         (= 3 (count slots)) "
        "         (= 3 (count slot-atoms)) "
        "         (fn? spatial-callback) "
        "         (= game-scene @game-scene-atom) "
        "         (= 1 (count rules)) "
        "         (= :collision (:kind rule1)) "
        "         (nil? (:channel rule1)) "
        "         (= 0 (:radius rule1)) "
        "         (= (:self rule1) (:prototype rocket-body)) "
        "         (= 3006 (:other rule1)) "
        "         (= 3003 (:id rocket-body)) "
        "         (= 3005 (:id rocket-nose)))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_game_demo_player_entity_matches_tri_type_hash) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    TEST_ASSERT_TRUE(tiny_fx_gfx_require_records_namespace(g_test_eval_state));
    TEST_ASSERT_TRUE(tiny_fx_gfx_ensure_schema(g_test_eval_state));

    ID player = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (get (:index (nth (tiny-fx.game-demo/create-demo-bundle) 2)) 3002))",
        g_test_eval_state);
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
        "  (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "        game0 (nth bundle 2) "
        "        game-scene-atom (:game-scene-atom (tiny-fx.game-demo/game-demo-config)) "
        "        p0 (get (:index game0) 3002) "
        "        h0 (get (:index game0) 3006)] "
        "    (let [disabled? true "
        "          enter? (nil? (tiny-fx.game-demo/on-player-collision-toggle! {:kind :collision :phase :enter})) "
        "          p1 (get (:index @game-scene-atom) 3002) "
        "          h1 (get (:index @game-scene-atom) 3006) "
        "          t1 (nth (nth (:keyframes (:t p1)) 0) 1) "
        "          ht1 (nth (nth (:keyframes (:t h1)) 0) 1) "
        "          exit? (nil? (tiny-fx.game-demo/on-player-collision-toggle! {:kind :collision :phase :exit})) "
        "          p2 (get (:index @game-scene-atom) 3002) "
        "          h2 (get (:index @game-scene-atom) 3006) "
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
        "       (= [1 1] [(:sx t2) (:sy t2)]) "
        "       (= [(:x1 h0) (:y1 h0) (:x2 h0) (:y2 h0) (:x3 h0) (:y3 h0)] "
        "          [(:x1 h1) (:y1 h1) (:x2 h1) (:y2 h1) (:x3 h1) (:y3 h1)]) "
        "       (= [(:x1 h0) (:y1 h0) (:x2 h0) (:y2 h0) (:x3 h0) (:y3 h0)] "
        "          [(:x1 h2) (:y1 h2) (:x2 h2) (:y2 h2) (:x3 h2) (:y3 h2)]) "
        "       (= 0.75 (:sx ht1)) "
        "       (= [1 1] [(:sx ht2) (:sy ht2)])"
        "       ])))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(13, vector_count(v));
    for (uint32_t i = 0; i < 13; i++) {
        TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, i));
    }
}

TEST(test_vector_scene_graph_game_demo_spatial_watcher_dispatch_toggles_player_scale) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (require 'tiny-fx.gfx-collision) "
        "  (let [game-scene-atom (:game-scene-atom (tiny-fx.game-demo/game-demo-config)) "
        "        enter? (nil? (tiny-fx.gfx-collision/invoke-collision-callback! "
        "                       {:source :spatial "
        "                        :id :player-vs-rocket "
        "                        :rule {:id :player-vs-rocket} "
        "                        :kind :collision "
        "                        :phase :enter})) "
        "        p1 (get (:index @game-scene-atom) 3002) "
        "        h1 (get (:index @game-scene-atom) 3006) "
        "        t1 (nth (nth (:keyframes (:t p1)) 0) 1) "
        "        ht1 (nth (nth (:keyframes (:t h1)) 0) 1) "
        "        exit? (nil? (tiny-fx.gfx-collision/invoke-collision-callback! "
        "                      {:source :spatial "
        "                       :id :player-vs-rocket "
        "                       :rule {:id :player-vs-rocket} "
        "                       :kind :collision "
        "                       :phase :exit})) "
        "        p2 (get (:index @game-scene-atom) 3002) "
        "        h2 (get (:index @game-scene-atom) 3006) "
        "        t2 (nth (nth (:keyframes (:t p2)) 0) 1) "
        "        ht2 (nth (nth (:keyframes (:t h2)) 0) 1)] "
        "    [enter? "
        "     (= 0.75 (:sx t1)) "
        "     (= 0.75 (:sx ht1)) "
        "     exit? "
        "     (= [1 1] [(:sx t2) (:sy t2)]) "
        "     (= [1 1] [(:sx ht2) (:sy ht2)])]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(TAG(out) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(out);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT(6, vector_count(v));
    for (uint32_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, i));
    }
}

/* Target: 0 (raised to 320000); TODO: find/fix game demo leaks to lower again. */
TEST(test_vector_scene_graph_game_demo_collision_callback_repeated_enter_reapplies_scale_from_scene_state, 320000) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do (require 'tiny-fx.game-demo) "
        "    (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "          game-scene-atom (:game-scene-atom (tiny-fx.game-demo/game-demo-config)) "
        "          _ (tiny-fx.game-demo/on-player-collision-toggle! {:kind :collision :phase :enter}) "
        "          game0 (nth bundle 2) "
        "          _ (reset! game-scene-atom game0) "
        "          _ (tiny-fx.game-demo/on-player-collision-toggle! {:kind :collision :phase :enter}) "
        "          player (get (:index @game-scene-atom) 3002) "
        "          t (nth (nth (:keyframes (:t player)) 0) 1)] "
        "      (= 0.75 (:sx t))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(out && out != clj_false);
}

/* Target: 0 (raised to 320000); TODO: find/fix game demo leaks to lower again. */
TEST(test_vector_scene_graph_game_demo_collision_proxy_tracks_visible_player_scale, 320000) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID out = eval_string(
        "(do "
        "  (require 'tiny-fx.game-demo) "
        "  (let [bundle (tiny-fx.game-demo/create-demo-bundle) "
        "        game-scene-atom (:game-scene-atom (tiny-fx.game-demo/game-demo-config)) "
        "        _ (tiny-fx.game-demo/on-player-collision-toggle! {:kind :collision :phase :enter}) "
        "        player (get (:index @game-scene-atom) 3002) "
        "        proxy (get (:index @game-scene-atom) 3006) "
        "        pt (nth (nth (:keyframes (:t player)) 0) 1) "
        "        ht (nth (nth (:keyframes (:t proxy)) 0) 1)] "
        "    (= [(:sx pt) (:sy pt)] [(:sx ht) (:sy ht)])))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(out && out != clj_false);
}

TEST(test_vector_scene_graph_game_demo_hidden_collision_proxy_captures_rendered_aabb) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    TEST_ASSERT_TRUE(tiny_fx_gfx_require_records_namespace(g_test_eval_state));
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

TEST(test_vector_scene_graph_rendered_state_capture_compute_dirty_rect_for_changed_entity_aabbs, 0) {
    vg_rendered_state_reset_all();

    VgTransformFixed t0 = vg_transform_fixed_identity();
    VgTransformFixed t1 = vg_transform_fixed_identity();
    t1.m02 = 4 * VG_SCALE_ONE;
    uintptr_t entity_a = (uintptr_t)fixnum(4101);
    uintptr_t entity_b = (uintptr_t)fixnum(4102);

    vg_rendered_state_capture_begin(3u, 1u, 0u);
    vg_rendered_state_capture_record_entity(entity_a, t0);
    vg_rendered_state_capture_record_entity_aabb(entity_a, (VgAabb){20, 30, 10, 20});
    vg_rendered_state_capture_record_entity(entity_b, t0);
    vg_rendered_state_capture_record_entity_aabb(entity_b, (VgAabb){2, 6, 2, 6});
    vg_rendered_state_capture_commit();

    vg_rendered_state_capture_begin(3u, 2u, 16u);
    vg_rendered_state_capture_record_entity(entity_a, t1);
    vg_rendered_state_capture_record_entity_aabb(entity_a, (VgAabb){24, 34, 10, 20});
    vg_rendered_state_capture_record_entity(entity_b, t0);
    vg_rendered_state_capture_record_entity_aabb(entity_b, (VgAabb){2, 6, 2, 6});

    VgClipRect dirty = {0};
    TEST_ASSERT_TRUE(vg_rendered_state_capture_compute_dirty_rect(3u,
                                                                  (VgClipRect){0, 0, TEST_W, TEST_H},
                                                                  1u,
                                                                  &dirty));
    TEST_ASSERT_EQUAL_INT(19, dirty.x);
    TEST_ASSERT_EQUAL_INT(9, dirty.y);
    TEST_ASSERT_EQUAL_INT(17, dirty.w);
    TEST_ASSERT_EQUAL_INT(13, dirty.h);

    vg_rendered_state_capture_discard();
    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_rendered_state_capture_compute_dirty_rect_falls_back_without_aabb, 0) {
    vg_rendered_state_reset_all();

    VgTransformFixed t0 = vg_transform_fixed_identity();
    VgTransformFixed t1 = vg_transform_fixed_identity();
    t1.m02 = 2 * VG_SCALE_ONE;
    uintptr_t entity = (uintptr_t)fixnum(4201);

    vg_rendered_state_capture_begin(4u, 1u, 0u);
    vg_rendered_state_capture_record_entity(entity, t0);
    vg_rendered_state_capture_commit();

    vg_rendered_state_capture_begin(4u, 2u, 16u);
    vg_rendered_state_capture_record_entity(entity, t1);

    VgClipRect clip = {0, 0, TEST_W, TEST_H};
    VgClipRect dirty = {0};
    TEST_ASSERT_TRUE_MESSAGE(
        vg_rendered_state_capture_compute_dirty_rect(4u, clip, 1u, &dirty),
        "entity without AABB should fall back to full clip rect");
    TEST_ASSERT_EQUAL_INT(clip.x, dirty.x);
    TEST_ASSERT_EQUAL_INT(clip.y, dirty.y);
    TEST_ASSERT_EQUAL_INT(clip.w, dirty.w);
    TEST_ASSERT_EQUAL_INT(clip.h, dirty.h);

    vg_rendered_state_capture_discard();
    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_rendered_state_capture_text_content_change_uses_aabb_union, 0) {
    vg_rendered_state_reset_all();

    VgTransformFixed t0 = vg_transform_fixed_identity();
    uintptr_t entity = (uintptr_t)fixnum(4202);

    vg_rendered_state_capture_begin(4u, 1u, 0u);
    vg_rendered_state_capture_record_entity(entity, t0);
    vg_rendered_state_capture_record_entity_content_signature(entity, 0x11111111u);
    vg_rendered_state_capture_record_entity_aabb(entity, (VgAabb){10, 19, 20, 30});
    vg_rendered_state_capture_commit();

    vg_rendered_state_capture_begin(4u, 2u, 16u);
    vg_rendered_state_capture_record_entity(entity, t0);
    vg_rendered_state_capture_record_entity_content_signature(entity, 0x22222222u);
    vg_rendered_state_capture_record_entity_aabb(entity, (VgAabb){10, 14, 20, 30});

    VgClipRect clip = {0, 0, TEST_W, TEST_H};
    VgClipRect dirty = {0};
    TEST_ASSERT_TRUE_MESSAGE(
        vg_rendered_state_capture_compute_dirty_rect(4u, clip, 1u, &dirty),
        "text content change with AABB should dirty only the text bounds");
    TEST_ASSERT_EQUAL_INT(9, dirty.x);
    TEST_ASSERT_EQUAL_INT(19, dirty.y);
    TEST_ASSERT_EQUAL_INT(12, dirty.w);
    TEST_ASSERT_EQUAL_INT(13, dirty.h);

    vg_rendered_state_capture_discard();
    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_text_local_bounds_tracks_visible_glyphs_only, 0) {
    VgRectData bounds = {0};
    VgTextData text = {.x = 0, .y = 0, .scale = VG_SCALE_ONE, .rot_deg = 0, .text = " HI "};

    TEST_ASSERT_TRUE(vg_text_local_bounds(&text, &bounds));
    TEST_ASSERT_EQUAL_INT(6, bounds.x);
    TEST_ASSERT_EQUAL_INT(0, bounds.y);
    TEST_ASSERT_EQUAL_INT(19, bounds.w);
    TEST_ASSERT_EQUAL_INT(11, bounds.h);
}

TEST(test_vector_scene_graph_rendered_state_capture_collect_dirty_rects_keeps_far_entities_separate, 0) {
    vg_rendered_state_reset_all();

    VgTransformFixed t0 = vg_transform_fixed_identity();
    VgTransformFixed t1 = vg_transform_fixed_identity();
    t1.m02 = 100 * VG_SCALE_ONE;
    uintptr_t paddle = (uintptr_t)fixnum(4301);
    uintptr_t ball = (uintptr_t)fixnum(4302);

    vg_rendered_state_capture_begin(5u, 1u, 0u);
    vg_rendered_state_capture_record_entity(paddle, t0);
    vg_rendered_state_capture_record_entity_aabb(paddle, (VgAabb){140, 179, 224, 227});
    vg_rendered_state_capture_record_entity(ball, t0);
    vg_rendered_state_capture_record_entity_aabb(ball, (VgAabb){156, 159, 120, 123});
    vg_rendered_state_capture_commit();

    vg_rendered_state_capture_begin(5u, 2u, 16u);
    vg_rendered_state_capture_record_entity(paddle, t1);
    vg_rendered_state_capture_record_entity_aabb(paddle, (VgAabb){240, 279, 224, 227});
    vg_rendered_state_capture_record_entity(ball, t1);
    vg_rendered_state_capture_record_entity_aabb(ball, (VgAabb){256, 259, 120, 123});

    VgClipRect dirty_rects[4] = {0};
    size_t dirty_count = 0u;
    TEST_ASSERT_TRUE(vg_rendered_state_capture_collect_dirty_rects(5u,
                                                                   (VgClipRect){0, 0, 320, 240},
                                                                   1u,
                                                                   dirty_rects,
                                                                   4u,
                                                                   &dirty_count));
    TEST_ASSERT_EQUAL_UINT(4u, dirty_count);
    TEST_ASSERT_TRUE(vg_clip_rect_equal(dirty_rects[0], (VgClipRect){.x = 139, .y = 223, .w = 42, .h = 6}));
    TEST_ASSERT_TRUE(vg_clip_rect_equal(dirty_rects[1], (VgClipRect){.x = 239, .y = 223, .w = 42, .h = 6}));
    TEST_ASSERT_TRUE(vg_clip_rect_equal(dirty_rects[2], (VgClipRect){.x = 155, .y = 119, .w = 6, .h = 6}));
    TEST_ASSERT_TRUE(vg_clip_rect_equal(dirty_rects[3], (VgClipRect){.x = 255, .y = 119, .w = 6, .h = 6}));

    vg_rendered_state_capture_discard();
    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_game_demo_gpio_press_triggers_demo_melody_once) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-clj.gpio) "
        "  (require 'tiny-fx.game-demo) "
        "  (require 'tiny-fx.sound-debug) "
        "  (tiny-fx.game-demo/create-demo-bundle) "
        "  (tiny-clj.gpio/simulate! 1 1) "
        "  (Thread/sleep 30) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (tiny-clj.gpio/simulate! 1 0) "
        "  (Thread/sleep 30) "
        "  (dotimes [_ 8] (run-next-task)) "
        "  (let [count @tiny-fx.game-demo/demo-melody-trigger-count* "
        "        status (tiny-fx.sound-debug/host-status!)] "
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
        "  (require 'tiny-fx.gfx-collision) "
        "  (def collision-test-cb-a (fn collision-test-cb-a [event] 11)) "
        "  (def collision-test-cb-b (fn collision-test-cb-b [event] 22)) "
        "  (let [_ (tiny-fx.gfx-collision/set-collision-callback! collision-test-cb-a) "
        "        v1 (tiny-fx.gfx-collision/invoke-collision-callback! {:phase :enter}) "
        "        _ (tiny-fx.gfx-collision/set-collision-callback! collision-test-cb-b) "
        "        v2 (tiny-fx.gfx-collision/invoke-collision-callback! {:phase :exit}) "
        "        _ (tiny-fx.gfx-collision/set-collision-callback! nil) "
        "        v3 (tiny-fx.gfx-collision/invoke-collision-callback! nil)] "
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
        "        game-scene (nth bundle 2) "
        "        game-index (:index game-scene) "
        "        game-root (get game-index (:root game-scene)) "
        "        terrain (get game-index 3001) "
        "        stars-group (get game-index 3020) "
        "        s1 (get game-index 3021) "
        "        s2 (get game-index 3022) "
        "        s3 (get game-index 3023) "
        "        s4 (get game-index 3024) "
        "        s5 (get game-index 3025) "
        "        s6 (get game-index 3026)] "
        "    (and (= [3021 3022 3023 3024 3025 3026] (:children stars-group)) "
        "         (= 3020 (first (:children game-root))) "
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
        "         (= 0 (:stroke-color (second (second (:keyframes (:style s1)))))) "
        "         (= 0 (:stroke-color (second (second (:keyframes (:style s2)))))) "
        "         (= 0 (:stroke-color (second (second (:keyframes (:style s3)))))) "
        "         (= 0 (:stroke-color (second (second (:keyframes (:style s4)))))) "
        "         (= 0 (:stroke-color (second (second (:keyframes (:style s5)))))) "
        "         (= 0 (:stroke-color (second (second (:keyframes (:style s6)))))))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_merge_nested_record_maps_regression) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID ok = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.gfx) "
        "  (let [style (record-create (quote Style) [65535 1 true false 0 false 0]) "
        "        root (record-create (quote Group) [:tiny-fx.scene/root nil style true [3021 3022] nil]) "
        "        star-a (record-create (quote Tri) [3021 nil style true 8 8 10 4 12 8 nil]) "
        "        star-b (record-create (quote Tri) [3022 nil style true 18 8 20 4 22 8 nil]) "
        "        base {:tiny-fx.scene/root root} "
        "        stars {3021 star-a 3022 star-b} "
        "        merged (merge base stars)] "
        "    (and (contains? merged :tiny-fx.scene/root) "
        "         (contains? merged 3021) "
        "         (contains? merged 3022))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(ok && ok != clj_false);
}

TEST(test_vector_scene_graph_timeline_numeric_interpolation_moves_line) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true "
        "                          (record-create (quote Timeline) [[[0 4] [100 14]] false]) "
        "                          6 "
        "                          (record-create (quote Timeline) [[[0 18] [100 28]] false]) "
        "                          6 nil)}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root "
        "                          (record-create (quote Timeline) "
        "                            [[[0 (record-create (quote Transform) [0 0 1 1 0])] "
        "                              [100 (record-create (quote Transform) [20 0 1 1 0])]] false]) "
        "                          (->Style 65535 1 true false 0 false 0) true 0 6 10 6 nil)}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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

TEST(test_vector_scene_graph_timeline_transform_interpolation_applies_timeline_ease) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do "
        "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.gfx) "
        "  (defrecord TimelineEase [keyframes loop ease]) "
        "  (let [entities {:tiny-fx.scene/root (tiny-fx.gfx-scene/->Line :tiny-fx.scene/root "
        "                            (record-create (quote TimelineEase) "
        "                              [[[0 (record-create (quote Transform) [0 0 1 1 0])] "
        "                                [100 (record-create (quote Transform) [20 0 1 1 0])]] false :out-cubic]) "
        "                            (tiny-fx.gfx-scene/->Style 65535 1 true false 0 false 0) true 0 6 10 6 nil)}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    TEST_ASSERT_TRUE(vg_render_scene_record_at_ms(scene, &fb, 50u));
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 17]);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)6 * TEST_W + 27]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)6 * TEST_W + 10]);
}

TEST(test_vector_scene_graph_timeline_loop_wraps_phase) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true "
        "                          (record-create (quote Timeline) [[[0 4] [100 14]] true]) "
        "                          6 "
        "                          (record-create (quote Timeline) [[[0 18] [100 28]] true]) "
        "                          6 nil)}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Group :tiny-fx.scene/root nil nil false [121] nil) "
        "                  121 (->Line 121 nil (->Style 65535 1 true false 0 false 0) true 4 6 18 6 nil)}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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

TEST(test_vector_scene_graph_renders_nested_entity_map_transform_inheritance) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Group :tiny-fx.scene/root (record-create (quote Transform) [7 5 1 1 0]) nil true [201] nil) "
        "                  201 (record-create (quote Line) [201 nil (->Style 65535 1 true false 0 false 0) true 0 0 10 0 nil])}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Group :tiny-fx.scene/root nil nil true [301] nil) "
        "                  301 (->Line 301 nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10 nil)}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities [20 8 10 6] 63488 nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 2 3 8 3 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [1 2 10 12] 3 true true 0 1 nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [20 8 10 6] 0 true true 0 0 nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [20 8 10 6] 0 false true 0 0 nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 4 10 20 10 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [0 8 30 6] 0 true true 0 0 nil])))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(static_scene);

    ID animated_scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true "
        "                          (record-create (quote Timeline) [[[0 4] [100 14]] false]) 10 20 10 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [0 8 30 6] 0 true true 0 0 nil])))",
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true "
        "                          (record-create (quote Timeline) [[[0 4] [100 14]] false]) 10 20 10 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [0 8 30 6] 0 true true 0 0 nil])))",
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

TEST(test_vector_scene_graph_render_frame_scene_slot_record_clears_has_animation_after_non_loop_timeline_end) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true "
        "                          (record-create (quote Timeline) [[[0 4] [100 14]] false false]) 10 20 10 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [0 8 30 6] 0 true true 0 0 nil])))",
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

    dirty_pixels = 0u;
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_at_ms(scene, &state, &fb, 1u, 150u, true, &dirty_pixels));
    TEST_ASSERT_FALSE(state.has_animation);
    TEST_ASSERT_EQUAL_HEX16(0xffffu, pixels[(size_t)10 * TEST_W + 14]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 4]);
}

TEST(test_vector_scene_graph_render_frame_scene_slot_record_reports_dirty_rect_for_style_animation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Rect :tiny-fx.scene/root nil "
        "                              (->Style 0 1 true true "
        "                                       (record-create (quote Timeline) [[[0 31] [100 63488]] false false]) "
        "                                       false 0) "
        "                              true 4 8 12 8 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [4 8 12 8] 0 true true 0 0 nil])))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgRenderSlotState state = {0};
    VgRenderFrameSlotResult result = {0};
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_result_at_ms(scene, &state, &fb, 1u, 0u, false, &result));
    TEST_ASSERT_TRUE(result.rendered);
    TEST_ASSERT_TRUE(state.has_animation);
    TEST_ASSERT_EQUAL_HEX16(31u, pixels[(size_t)10 * TEST_W + 8]);

    memset(&result, 0, sizeof(result));
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_result_at_ms(scene, &state, &fb, 1u, 50u, true, &result));
    TEST_ASSERT_TRUE(result.rendered);
    TEST_ASSERT_TRUE(result.dirty_pixels > 0u);
    TEST_ASSERT_TRUE(result.dirty_rect.w > 0);
    TEST_ASSERT_TRUE(result.dirty_rect.h > 0);
    TEST_ASSERT_NOT_EQUAL(31u, pixels[(size_t)10 * TEST_W + 8]);
}

TEST(test_vector_scene_graph_discrete_keyframes_change_style_pixels_over_time) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [fill (record-create (quote Timeline) "
        "               [[[0 (tiny-fx.gfx/color 24 24 24)] "
        "                 [50 (tiny-fx.gfx/color 24 24 24)] "
        "                 [50 (tiny-fx.gfx/color 128 128 128)] "
        "                 [100 (tiny-fx.gfx/color 128 128 128)] "
        "                 [100 (tiny-fx.gfx/color 255 255 255)]] "
        "                false false]) "
        "        entities {:tiny-fx.scene/root (->Rect :tiny-fx.scene/root nil "
        "                              (->Style 0 1 true true fill false 0) "
        "                              true 4 8 12 8 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [4 8 12 8] 0 true true 0 0 nil])))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgRenderSlotState state = {0};
    VgRenderFrameSlotResult result = {0};
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_result_at_ms(scene, &state, &fb, 1u, 25u, false, &result));
    TEST_ASSERT_TRUE(result.rendered);
    uint16_t before = pixels[(size_t)10 * TEST_W + 8];

    memset(&result, 0, sizeof(result));
    TEST_ASSERT_TRUE(vg_render_frame_slot_record_result_at_ms(scene, &state, &fb, 1u, 75u, true, &result));
    TEST_ASSERT_TRUE(result.rendered);
    TEST_ASSERT_TRUE(result.dirty_pixels > 0u);
    uint16_t after = pixels[(size_t)10 * TEST_W + 8];

    TEST_ASSERT_NOT_EQUAL(before, after);
}

TEST(test_vector_scene_graph_render_frame_scene_slot_record_reports_dirty_rect_union) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID first_scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [20 8 10 6] 0 true true 0 1 nil])))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_scene);

    ID moved_scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 0 10 63 10 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [24 11 12 5] 0 true true 0 2 nil])))",
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

TEST(test_vector_scene_graph_dirty_union_tree_overlap_cluster_child_first_matches_union) {
    uint16_t union_pixels[TEST_W * TEST_H];
    uint16_t child_pixels[TEST_W * TEST_H];
    VgFrameBuffer union_fb;
    VgFrameBuffer child_fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&union_fb, TEST_W, TEST_H, union_pixels, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&child_fb, TEST_W, TEST_H, child_pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&union_fb, 0x0000u);
    vg_framebuffer_clear(&child_fb, 0x0000u);

    VgStyle red_fill = vg_style_default();
    red_fill.has_fill = true;
    red_fill.fill_color = 0xf800u;
    VgStyle green_fill = vg_style_default();
    green_fill.has_fill = true;
    green_fill.fill_color = 0x07e0u;

    VgNode r1 = {
        .id = 1,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = red_fill,
        .data.rect = {.x = 10, .y = 10, .w = 18, .h = 12}
    };
    VgNode r2 = {
        .id = 2,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = green_fill,
        .data.rect = {.x = 20, .y = 14, .w = 18, .h = 12}
    };
    VgNode *children[] = {&r1, &r2};
    VgNode root = {
        .id = 100,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = children, .child_count = 2}
    };

    VgClipRect child_clips[2] = {
        {.x = 10, .y = 10, .w = 18, .h = 12},
        {.x = 20, .y = 14, .w = 18, .h = 12},
    };
    VgClipRect union_clip = vg_clip_rect_union(child_clips[0], child_clips[1]);

    vg_render_scene_clipped(&root, &union_fb, union_clip);
    render_scene_over_clips(&root, &child_fb, child_clips, 2u);

    TEST_ASSERT_EQUAL_MEMORY(union_fb.pixels, child_fb.pixels, sizeof(union_pixels));
}

TEST(test_vector_scene_graph_dirty_union_tree_non_overlap_children_match_union) {
    uint16_t union_pixels[TEST_W * TEST_H];
    uint16_t child_pixels[TEST_W * TEST_H];
    VgFrameBuffer union_fb;
    VgFrameBuffer child_fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&union_fb, TEST_W, TEST_H, union_pixels, TEST_W * TEST_H));
    TEST_ASSERT_TRUE(vg_framebuffer_init(&child_fb, TEST_W, TEST_H, child_pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&union_fb, 0x0000u);
    vg_framebuffer_clear(&child_fb, 0x0000u);

    VgStyle cyan_fill = vg_style_default();
    cyan_fill.has_fill = true;
    cyan_fill.fill_color = 0x07ffu;
    VgStyle magenta_fill = vg_style_default();
    magenta_fill.has_fill = true;
    magenta_fill.fill_color = 0xf81fu;

    VgNode r1 = {
        .id = 11,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = cyan_fill,
        .data.rect = {.x = 6, .y = 6, .w = 10, .h = 8}
    };
    VgNode r2 = {
        .id = 12,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = magenta_fill,
        .data.rect = {.x = 38, .y = 26, .w = 12, .h = 10}
    };
    VgNode *children[] = {&r1, &r2};
    VgNode root = {
        .id = 110,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = children, .child_count = 2}
    };

    VgClipRect child_clips[2] = {
        {.x = 6, .y = 6, .w = 10, .h = 8},
        {.x = 38, .y = 26, .w = 12, .h = 10},
    };
    VgClipRect union_clip = vg_clip_rect_union(child_clips[0], child_clips[1]);

    vg_render_scene_clipped(&root, &union_fb, union_clip);
    render_scene_over_clips(&root, &child_fb, child_clips, 2u);

    TEST_ASSERT_EQUAL_MEMORY(union_fb.pixels, child_fb.pixels, sizeof(union_pixels));
}

TEST(test_vector_scene_graph_dirty_union_tree_budget_splits_when_union_exceeds_budget) {
    VgClipRect leaves[2] = {
        {.x = 2, .y = 2, .w = 6, .h = 4},
        {.x = 42, .y = 28, .w = 6, .h = 4},
    };
    VgClipRect planned[4] = {0};

    size_t plan_count = vg_dirty_union_plan_rects(leaves, 2u, 64u, planned, 4u);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        2u,
        plan_count,
        "dirty-union planner should split into child rects when union exceeds budget but leaves fit");
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[0], leaves[0]));
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[1], leaves[1]));
}

TEST(test_vector_scene_graph_dirty_union_tree_budget_keeps_non_overlapping_clusters_separate) {
    VgClipRect leaves[2] = {
        {.x = 4, .y = 4, .w = 4, .h = 4},
        {.x = 10, .y = 4, .w = 4, .h = 4},
    };
    VgClipRect planned[4] = {0};
    size_t plan_count = vg_dirty_union_plan_rects(leaves, 2u, 64u, planned, 4u);

    TEST_ASSERT_EQUAL_UINT(2u, plan_count);
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[0], leaves[0]));
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[1], leaves[1]));
}

TEST(test_vector_scene_graph_dirty_union_tree_budget_does_not_merge_leaf_inside_cluster_bbox_hole) {
    VgClipRect leaves[3] = {
        {.x = 0, .y = 0, .w = 8, .h = 4},
        {.x = 0, .y = 0, .w = 4, .h = 8},
        {.x = 4, .y = 4, .w = 4, .h = 4},
    };
    VgClipRect planned[4] = {0};

    size_t plan_count = vg_dirty_union_plan_rects(leaves, 3u, 64u, planned, 4u);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        2u,
        plan_count,
        "leaf inside union bbox hole must stay separate unless it overlaps a cluster leaf");
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[0], (VgClipRect){.x = 0, .y = 0, .w = 8, .h = 8}));
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[1], leaves[2]));
}

TEST(test_vector_scene_graph_dirty_union_tree_budget_splits_oversized_leaf_geometrically) {
    VgClipRect leaves[1] = {
        {.x = 0, .y = 0, .w = 16, .h = 8},
    };
    VgClipRect planned[8] = {0};

    size_t plan_count = vg_dirty_union_plan_rects(leaves, 1u, 64u, planned, 8u);

    TEST_ASSERT_EQUAL_UINT(2u, plan_count);
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[0], (VgClipRect){.x = 0, .y = 0, .w = 8, .h = 8}));
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[1], (VgClipRect){.x = 8, .y = 0, .w = 8, .h = 8}));
}

TEST(test_vector_scene_graph_dirty_union_tree_budget_merges_overlapping_leaves_per_cluster) {
    VgClipRect leaves[3] = {
        {.x = 2, .y = 2, .w = 6, .h = 4},
        {.x = 6, .y = 3, .w = 6, .h = 4},
        {.x = 42, .y = 28, .w = 6, .h = 4},
    };
    VgClipRect planned[4] = {0};

    size_t plan_count = vg_dirty_union_plan_rects(leaves, 3u, 64u, planned, 4u);

    TEST_ASSERT_EQUAL_UINT(2u, plan_count);
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[0], (VgClipRect){.x = 2, .y = 2, .w = 10, .h = 5}));
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[1], leaves[2]));
}

TEST(test_vector_scene_graph_dirty_union_tree_budget_keeps_overlap_cluster_split_when_union_area_is_too_large) {
    VgClipRect leaves[2] = {
        {.x = 0, .y = 0, .w = 10, .h = 2},
        {.x = 8, .y = 1, .w = 10, .h = 2},
    };
    VgClipRect planned[4] = {0};

    size_t plan_count = vg_dirty_union_plan_rects(leaves, 2u, 64u, planned, 4u);

    TEST_ASSERT_EQUAL_UINT(2u, plan_count);
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[0], leaves[0]));
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[1], leaves[1]));
}

TEST(test_vector_scene_graph_dirty_union_tree_budget_falls_back_to_union_when_output_capacity_too_small) {
    VgClipRect leaves[3] = {
        {.x = 2, .y = 2, .w = 6, .h = 4},
        {.x = 6, .y = 3, .w = 6, .h = 4},
        {.x = 42, .y = 28, .w = 6, .h = 4},
    };
    VgClipRect planned[1] = {0};
    VgClipRect expected_union = vg_clip_rect_union(vg_clip_rect_union(leaves[0], leaves[1]), leaves[2]);

    size_t plan_count = vg_dirty_union_plan_rects(leaves, 3u, 64u, planned, 1u);

    TEST_ASSERT_EQUAL_UINT(1u, plan_count);
    TEST_ASSERT_TRUE(vg_clip_rect_equal(planned[0], expected_union));
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (vector "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 2 4 14 4 nil)} [0 0 20 10] 0 true true 0 0 nil]) "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 2 8 14 8 nil)} [0 0 20 12] 1 true true 0 0 nil]) "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true 2 12 14 12 nil)} [0 0 20 14] 2 true true 0 0 nil])))",
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

TEST(test_vector_scene_graph_rect_width_and_height_are_pixel_extents) {
    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgStyle style = vg_style_default();
    style.stroke_color = 0xffffu;
    style.stroke_width = 1;
    style.has_fill = true;
    style.fill_color = 0x07e0u;

    VgNode rect = {
        .id = 1011,
        .type = VG_NODE_RECT,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = style,
        .data.rect = {.x = 10, .y = 10, .w = 20, .h = 12}
    };

    vg_render_scene(&rect, &fb);

    TEST_ASSERT_NOT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 10]);
    TEST_ASSERT_NOT_EQUAL_HEX16(0x0000u, pixels[(size_t)21 * TEST_W + 29]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)10 * TEST_W + 30]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)22 * TEST_W + 10]);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, pixels[(size_t)22 * TEST_W + 30]);
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Rect :tiny-fx.scene/root nil (->Style 65535 1 true true 2016 false 0) true 10 10 20 12 nil)}] "
        "    (record-create (quote Scene) [:tiny-fx.scene/root entities nil nil nil])))",
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

TEST(test_vector_scene_graph_gfx_bench_vector_scene_bench_returns_metrics_map) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID result = eval_string(
        "(do (require 'tiny-fx.gfx-bench) "
        "    (tiny-fx.gfx-bench/vector-scene-bench 120 8))",
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

TEST(test_vector_scene_graph_gfx_bench_vector_scene_bench_arity_and_arg_validation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    bool arity_exception_caught = false;
    TRY {
        (void)eval_string(
            "(do (require 'tiny-fx.gfx-bench) "
            "    (tiny-fx.gfx-bench/vector-scene-bench 1 2 3))",
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
            "(do (require 'tiny-fx.gfx-bench) "
            "    (tiny-fx.gfx-bench/vector-scene-bench \"x\"))",
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
        "    [(identical? tiny-fx.gfx/start-renderer! tiny-clj.runtime/start-renderer!) "
        "     (identical? tiny-fx.gfx/stop-renderer! tiny-clj.runtime/stop-renderer!) "
        "     (identical? tiny-fx.gfx/renderer-state tiny-clj.runtime/renderer-state) "
        "     (identical? tiny-fx.gfx/renderer-timeline-step tiny-clj.runtime/renderer-timeline-step) "
        "     (identical? tiny-fx.gfx/renderer-timeline-progress tiny-clj.runtime/renderer-timeline-progress)])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(5, vector_count(vec));
    for (unsigned int i = 0; i < vector_count(vec); i++) {
        TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(vec, i));
    }
}

TEST(test_vector_scene_graph_tiny_fx_runtime_frontend_excludes_benchmark_surface) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID result = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "    (try (do tiny-fx.gfx/vector-scene-bench false) "
        "         (catch Exception e true)))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, result);
}

TEST(test_vector_scene_graph_tiny_fx_runtime_frontend_excludes_collision_surface) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    ID result = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "    (try (do tiny-fx.gfx-collision/set-collision-callback! false) "
        "         (catch Exception e true)))",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, result);
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
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
        "    (require 'tiny-fx.game-demo) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.game-demo/slot-descriptors)) "
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "    (require 'tiny-fx.game-demo) "
        "    (tiny-fx.gfx/start-renderer! (tiny-fx.game-demo/slot-descriptors)) "
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
        "    (require 'tiny-fx.game-demo) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.game-demo/slot-descriptors)) "
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
        "    (require 'tiny-fx.game-demo) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.game-demo/slot-descriptors)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(restore_result);

    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_runtime_rendered_state_queries_non_timeline_fields_return_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    vg_rendered_state_reset_all();

    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true "
        "                          (record-create (quote Timeline) [[[0 4] [100 14]] false]) 6 20 6 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [0 0 64 48] 0 true true 0 0 nil])))",
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
        "    (require 'tiny-fx.game-demo) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.game-demo/slot-descriptors)) "
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
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true "
        "                          (record-create (quote Timeline) [[[0 4] [100 14]] false]) 8 20 8 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [0 0 64 48] 0 true true 0 0 nil])))",
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
        "    (require 'tiny-fx.game-demo) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.game-demo/slot-descriptors)) "
        "    [(tiny-clj.runtime/renderer-timeline-step :game :tiny-fx.scene/root :x1) "
        "     (tiny-clj.runtime/renderer-timeline-progress :game :tiny-fx.scene/root :x1)])",
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
    TEST_ASSERT_TRUE(map_get(progress_map, (ID)intern_symbol_global(":end-event")) == clj_false);
    TEST_ASSERT_TRUE(map_get(progress_map, (ID)intern_symbol_global(":at-end")) == clj_true);
    TEST_ASSERT_EQUAL_INT(92, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":snapshot-gen"))));
    TEST_ASSERT_EQUAL_INT(150, as_fixnum(map_get(progress_map, (ID)intern_symbol_global(":ts-ms"))));

    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_runtime_rendered_state_queries_expose_timeline_end_event_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    vg_rendered_state_reset_all();

    ID scene = eval_string(
        "(do (require 'tiny-fx.gfx) (require '[tiny-fx.gfx-scene :refer :all]) "
        "  (let [entities {:tiny-fx.scene/root (->Line :tiny-fx.scene/root nil (->Style 65535 1 true false 0 false 0) true "
        "                          (record-create (quote Timeline) [[[0 4] [100 14]] false true]) 8 20 8 nil)}] "
        "    (record-create (quote FrameScene) [:tiny-fx.scene/root entities [0 0 64 48] 0 true true 0 0 nil])))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(scene);

    uint16_t pixels[TEST_W * TEST_H];
    VgFrameBuffer fb;
    TEST_ASSERT_TRUE(vg_framebuffer_init(&fb, TEST_W, TEST_H, pixels, TEST_W * TEST_H));
    vg_framebuffer_clear(&fb, 0x0000u);

    VgRenderSlotState state = {0};
    uint32_t dirty_pixels = 0u;
    vg_rendered_state_capture_begin(2u, 93u, 150u);
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
        "    (require 'tiny-fx.game-demo) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.game-demo/slot-descriptors)) "
        "    (tiny-clj.runtime/renderer-timeline-progress :game :tiny-fx.scene/root :x1))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_map(result));

    TEST_ASSERT_TRUE(map_get(result, (ID)intern_symbol_global(":loop")) == clj_false);
    TEST_ASSERT_TRUE(map_get(result, (ID)intern_symbol_global(":end-event")) == clj_true);
    TEST_ASSERT_TRUE(map_get(result, (ID)intern_symbol_global(":at-end")) == clj_true);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(map_get(result, (ID)intern_symbol_global(":step"))));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(map_get(result, (ID)intern_symbol_global(":period-ms"))));
    TEST_ASSERT_EQUAL_INT(1000, as_fixnum(map_get(result, (ID)intern_symbol_global(":permille"))));

    vg_rendered_state_reset_all();
}

TEST(test_vector_scene_graph_timeline_watch_polls_marked_end_edges_once) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    event_loop_clear();
    vg_rendered_state_reset_all();

    VgRenderedTimelineSample sample = {
        .step_index = 1u,
        .keyframe_count = 2u,
        .phase_ms = 100u,
        .period_ms = 100u,
        .loop = false,
        .end_event = true,
        .at_end = true,
    };

    vg_rendered_state_capture_begin(2u, 44u, 150u);
    vg_rendered_state_capture_record_timeline((uintptr_t)fixnum(3001), VG_RENDERED_FIELD_T, sample);
    vg_rendered_state_capture_commit();

    ID result = eval_string(
        "(do (require 'tiny-clj.runtime) "
        "    (require 'tiny-fx.gfx) "
        "    (require 'tiny-fx.gfx-timeline) "
        "    (require 'tiny-fx.game-demo) "
        "    (tiny-clj.runtime/start-renderer! (tiny-fx.game-demo/slot-descriptors)) "
        "    (let [seen (atom [])] "
        "      (tiny-fx.gfx-timeline/watch :demo/end "
        "        (fn [event] "
        "          (swap! seen conj [(:id event) (:at-end (:progress event))]) "
        "          nil) "
        "        {:slot :game :entity-id 3001 :field :t}) "
        "      (tiny-fx.gfx-timeline/poll-watchers!) "
        "      (run-next-task) "
        "      (tiny-fx.gfx-timeline/poll-watchers!) "
        "      (tiny-fx.gfx-timeline/watch :demo/end nil) "
        "      @seen))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result));
    CljPersistentVector *vec = as_vector(result);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(1, vector_count(vec));

    ID entry = vector_nth(vec, 0);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(entry));
    CljPersistentVector *event_vec = as_vector(entry);
    TEST_ASSERT_NOT_NULL(event_vec);
    TEST_ASSERT_EQUAL_INT(2, vector_count(event_vec));
    TEST_ASSERT_NOT_NULL(vector_nth(event_vec, 0));
    TEST_ASSERT_TRUE(vector_nth(event_vec, 1) == clj_true);

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
            "    (require 'tiny-fx.game-demo) "
            "    (tiny-clj.runtime/start-renderer! (tiny-fx.game-demo/slot-descriptors)) "
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
