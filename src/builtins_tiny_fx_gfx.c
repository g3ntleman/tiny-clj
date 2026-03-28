#include "builtins_tiny_fx_gfx.h"

#include "exception.h"

#ifndef TINYCLJ_WITH_TINY_FX
#define TINYCLJ_WITH_TINY_FX 1
#endif

#if TINYCLJ_WITH_TINY_FX
#include <stdint.h>
#include <string.h>

#include "eval.h"
#include "map.h"
#include "memory.h"
#include "platform.h"
#include "renderer_lifecycle.h"
#include "rendered_state_snapshot.h"
#include "scene.h"
#include "symbol.h"
#include "tiny_fx_gfx.h"
#include "vector.h"
#endif

#if !TINYCLJ_WITH_TINY_FX

static ID tinyclj_runtime_fx_disabled(const char *fn_name) {
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "tiny-fx is disabled; %s is unavailable",
                              fn_name ? fn_name : "renderer API");
    return NULL;
}

void builtins_tiny_fx_gfx_reset_cached_state(void) {
}

#ifdef DEBUG
ID native_tinyfx_gfx_bench_vector_scene_bench(ID *args, unsigned int argc) {
    (void)args;
    (void)argc;
    return tinyclj_runtime_fx_disabled("tiny-clj.runtime/vector-scene-bench");
}
#endif

ID native_tinyclj_runtime_start_renderer(ID *args, unsigned int argc) {
    (void)args;
    (void)argc;
    return tinyclj_runtime_fx_disabled("tiny-clj.runtime/start-renderer!");
}

ID native_tinyclj_runtime_stop_renderer(ID *args, unsigned int argc) {
    (void)args;
    (void)argc;
    return tinyclj_runtime_fx_disabled("tiny-clj.runtime/stop-renderer!");
}

ID native_tinyclj_runtime_renderer_state(ID *args, unsigned int argc) {
    (void)args;
    (void)argc;
    return tinyclj_runtime_fx_disabled("tiny-clj.runtime/renderer-state");
}

ID native_tinyclj_runtime_renderer_timeline_step(ID *args, unsigned int argc) {
    (void)args;
    (void)argc;
    return tinyclj_runtime_fx_disabled("tiny-clj.runtime/renderer-timeline-step");
}

ID native_tinyclj_runtime_renderer_timeline_progress(ID *args, unsigned int argc) {
    (void)args;
    (void)argc;
    return tinyclj_runtime_fx_disabled("tiny-clj.runtime/renderer-timeline-progress");
}

ID native_tinyfx_color_color(ID *args, unsigned int argc) {
    (void)args;
    (void)argc;
    return tinyclj_runtime_fx_disabled("tiny-fx.gfx/color");
}

#else

#define TINYCLJ_SCENE_BENCH_WIDTH 320u
#define TINYCLJ_SCENE_BENCH_HEIGHT 240u
#define TINYCLJ_SCENE_BENCH_PIXEL_COUNT ((size_t)TINYCLJ_SCENE_BENCH_WIDTH * (size_t)TINYCLJ_SCENE_BENCH_HEIGHT)

typedef struct {
    ID id;
    uint8_t index;
} TinycljRendererSlotBinding;

typedef struct {
    ID *slot;
    uint8_t index;
} TinycljNamedRendererSlot;

typedef struct {
    ID *slot;
    VgRenderedField field;
} TinycljRenderedFieldKeyword;

static TinycljRendererSlotBinding g_renderer_slot_bindings[VG_RENDERED_STATE_MAX_SLOTS];
static uint8_t g_renderer_slot_binding_count = 0u;

static ID g_kw_id = NULL;
static ID g_kw_atom = NULL;
static ID g_kw_deco = NULL;
static ID g_kw_score = NULL;
static ID g_kw_game = NULL;

static ID g_kw_t = NULL;
static ID g_kw_style = NULL;
static ID g_kw_visible = NULL;
static ID g_kw_children = NULL;
static ID g_kw_pts = NULL;
static ID g_kw_closed = NULL;
static ID g_kw_x = NULL;
static ID g_kw_y = NULL;
static ID g_kw_w = NULL;
static ID g_kw_h = NULL;
static ID g_kw_x1 = NULL;
static ID g_kw_y1 = NULL;
static ID g_kw_x2 = NULL;
static ID g_kw_y2 = NULL;
static ID g_kw_x3 = NULL;
static ID g_kw_y3 = NULL;
static ID g_kw_scale = NULL;
static ID g_kw_rot = NULL;
static ID g_kw_text = NULL;

static ID g_kw_platform = NULL;
static ID g_kw_iterations = NULL;
static ID g_kw_warmup = NULL;
static ID g_kw_slot_count = NULL;
static ID g_kw_deco_total_ms = NULL;
static ID g_kw_score_total_ms = NULL;
static ID g_kw_game_total_ms = NULL;
static ID g_kw_deco_us_per_frame = NULL;
static ID g_kw_score_us_per_frame = NULL;
static ID g_kw_game_us_per_frame = NULL;
static ID g_kw_total_ms = NULL;

static ID g_kw_tx = NULL;
static ID g_kw_ty = NULL;
static ID g_kw_m00 = NULL;
static ID g_kw_m01 = NULL;
static ID g_kw_m02 = NULL;
static ID g_kw_m10 = NULL;
static ID g_kw_m11 = NULL;
static ID g_kw_m12 = NULL;
static ID g_kw_snapshot_gen = NULL;
static ID g_kw_ts_ms = NULL;

static ID g_kw_step = NULL;
static ID g_kw_count = NULL;
static ID g_kw_phase_ms = NULL;
static ID g_kw_period_ms = NULL;
static ID g_kw_loop = NULL;
static ID g_kw_end_event = NULL;
static ID g_kw_event_id = NULL;
static ID g_kw_slot_id = NULL;
static ID g_kw_entity_id = NULL;
static ID g_kw_field = NULL;
static ID g_kw_at_end = NULL;
static ID g_kw_permille = NULL;

static bool tinyfx_color_parse_u8(ID value, uint8_t *out) {
    if (!is_fixnum(value)) {
        return false;
    }
    int32_t parsed = as_fixnum(value);
    if (parsed < 0 || parsed > 255) {
        return false;
    }
    *out = (uint8_t)parsed;
    return true;
}

static ID tinyfx_color_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    const uint16_t r5 = (uint16_t)(((uint32_t)r * 31u) / 255u);
    const uint16_t g6 = (uint16_t)(((uint32_t)g * 63u) / 255u);
    const uint16_t b5 = (uint16_t)(((uint32_t)b * 31u) / 255u);
    return fixnum((int32_t)((r5 << 11) | (g6 << 5) | b5));
}

static IdSymbolCacheEntry g_runtime_keyword_cache[] = {
    {&g_kw_id, ":id"},
    {&g_kw_atom, ":atom"},
    {&g_kw_deco, ":deco"},
    {&g_kw_score, ":score"},
    {&g_kw_game, ":game"},
    {&g_kw_t, ":t"},
    {&g_kw_style, ":style"},
    {&g_kw_visible, ":visible"},
    {&g_kw_children, ":children"},
    {&g_kw_pts, ":pts"},
    {&g_kw_closed, ":closed"},
    {&g_kw_x, ":x"},
    {&g_kw_y, ":y"},
    {&g_kw_w, ":w"},
    {&g_kw_h, ":h"},
    {&g_kw_x1, ":x1"},
    {&g_kw_y1, ":y1"},
    {&g_kw_x2, ":x2"},
    {&g_kw_y2, ":y2"},
    {&g_kw_x3, ":x3"},
    {&g_kw_y3, ":y3"},
    {&g_kw_scale, ":scale"},
    {&g_kw_rot, ":rot"},
    {&g_kw_text, ":text"},
    {&g_kw_platform, ":platform"},
    {&g_kw_iterations, ":iterations"},
    {&g_kw_warmup, ":warmup"},
    {&g_kw_slot_count, ":slot-count"},
    {&g_kw_deco_total_ms, ":deco-total-ms"},
    {&g_kw_score_total_ms, ":score-total-ms"},
    {&g_kw_game_total_ms, ":game-total-ms"},
    {&g_kw_deco_us_per_frame, ":deco-us-per-frame"},
    {&g_kw_score_us_per_frame, ":score-us-per-frame"},
    {&g_kw_game_us_per_frame, ":game-us-per-frame"},
    {&g_kw_total_ms, ":total-ms"},
    {&g_kw_tx, ":tx"},
    {&g_kw_ty, ":ty"},
    {&g_kw_m00, ":m00"},
    {&g_kw_m01, ":m01"},
    {&g_kw_m02, ":m02"},
    {&g_kw_m10, ":m10"},
    {&g_kw_m11, ":m11"},
    {&g_kw_m12, ":m12"},
    {&g_kw_snapshot_gen, ":snapshot-gen"},
    {&g_kw_ts_ms, ":ts-ms"},
    {&g_kw_step, ":step"},
    {&g_kw_count, ":count"},
    {&g_kw_phase_ms, ":phase-ms"},
    {&g_kw_period_ms, ":period-ms"},
    {&g_kw_loop, ":loop"},
    {&g_kw_end_event, ":end-event"},
    {&g_kw_event_id, ":event-id"},
    {&g_kw_slot_id, ":slot-id"},
    {&g_kw_entity_id, ":entity-id"},
    {&g_kw_field, ":field"},
    {&g_kw_at_end, ":at-end"},
    {&g_kw_permille, ":permille"},
};

static const TinycljNamedRendererSlot g_named_renderer_slots[] = {
    {&g_kw_deco, 0u},
    {&g_kw_score, 1u},
    {&g_kw_game, 2u},
};

static const TinycljRenderedFieldKeyword g_rendered_field_keywords[] = {
    {&g_kw_t, VG_RENDERED_FIELD_T},
    {&g_kw_style, VG_RENDERED_FIELD_STYLE},
    {&g_kw_visible, VG_RENDERED_FIELD_VISIBLE},
    {&g_kw_children, VG_RENDERED_FIELD_CHILDREN},
    {&g_kw_pts, VG_RENDERED_FIELD_PTS},
    {&g_kw_closed, VG_RENDERED_FIELD_CLOSED},
    {&g_kw_x, VG_RENDERED_FIELD_X},
    {&g_kw_y, VG_RENDERED_FIELD_Y},
    {&g_kw_w, VG_RENDERED_FIELD_W},
    {&g_kw_h, VG_RENDERED_FIELD_H},
    {&g_kw_x1, VG_RENDERED_FIELD_X1},
    {&g_kw_y1, VG_RENDERED_FIELD_Y1},
    {&g_kw_x2, VG_RENDERED_FIELD_X2},
    {&g_kw_y2, VG_RENDERED_FIELD_Y2},
    {&g_kw_x3, VG_RENDERED_FIELD_X3},
    {&g_kw_y3, VG_RENDERED_FIELD_Y3},
    {&g_kw_scale, VG_RENDERED_FIELD_SCALE},
    {&g_kw_rot, VG_RENDERED_FIELD_ROT},
    {&g_kw_text, VG_RENDERED_FIELD_TEXT},
};

static void tinyclj_runtime_reset_keyword_cache(void) {
    size_t keyword_count = sizeof(g_runtime_keyword_cache) / sizeof(g_runtime_keyword_cache[0]);
    for (size_t i = 0; i < keyword_count; i++) {
        *g_runtime_keyword_cache[i].slot = NULL;
    }
}

static bool tinyclj_runtime_intern_common_keywords(void) {
    if (!id_symbol_cache_init_global(
            g_runtime_keyword_cache,
            sizeof(g_runtime_keyword_cache) / sizeof(g_runtime_keyword_cache[0]))) {
        tinyclj_runtime_reset_keyword_cache();
        return false;
    }
    return true;
}

static ID tinyclj_runtime_slot_desc_field(ID slot_desc, ID key) {
    if (!slot_desc || !key) {
        return NULL;
    }
    if (is_map(slot_desc)) {
        return map_get_sentinel(slot_desc, key, NULL);
    }
    if (TAG(slot_desc) == CLJ_RECORD) {
        return tiny_fx_gfx_get_field(slot_desc, key, NULL);
    }
    return NULL;
}

static void tinyclj_runtime_clear_slot_bindings(void) {
    memset(g_renderer_slot_bindings, 0, sizeof(g_renderer_slot_bindings));
    g_renderer_slot_binding_count = 0u;
}

/**
 * @brief Populate native renderer slot index map from host :slots vector.
 *
 * @param slot_atoms Vector of {:id kw :atom atom} slot descriptors (same shape as start-renderer!).
 * @return true when bindings were applied or slot_atoms was nil; false on invalid shape.
 */
bool builtins_tiny_fx_gfx_register_slot_bindings(ID slot_atoms) {
    tinyclj_runtime_clear_slot_bindings();
    if (!slot_atoms) {
        return true;
    }
    if (!is_vector(slot_atoms)) {
        return false;
    }
    if (!tinyclj_runtime_intern_common_keywords()) {
        return false;
    }

    CljPersistentVector *vec = as_vector(slot_atoms);
    if (!vec) {
        return false;
    }

    uint32_t count = vector_count(vec);
    if (count > VG_RENDERED_STATE_MAX_SLOTS) {
        return false;
    }

    for (uint32_t i = 0; i < count; i++) {
        ID slot_desc = vector_nth(vec, i);
        ID slot_id = tinyclj_runtime_slot_desc_field(slot_desc, g_kw_id);
        ID slot_atom = tinyclj_runtime_slot_desc_field(slot_desc, g_kw_atom);
        if (!slot_id || !is_symbol(slot_id) || !slot_atom || TAG(slot_atom) != CLJ_ATOM) {
            tinyclj_runtime_clear_slot_bindings();
            return false;
        }
        CljSymbol *slot_sym = intern_symbol(as_symbol(slot_id)->ns_name, as_symbol(slot_id)->cname);
        if (!slot_sym) {
            tinyclj_runtime_clear_slot_bindings();
            return false;
        }
        for (uint32_t j = 0; j < i; j++) {
            if (g_renderer_slot_bindings[j].id == slot_sym) {
                tinyclj_runtime_clear_slot_bindings();
                return false;
            }
        }
        // Store canonical interned symbols only, so the global binding table does
        // not depend on the ownership or pool lifetime of the original descriptor.
        g_renderer_slot_bindings[i].id = slot_sym;
        g_renderer_slot_bindings[i].index = (uint8_t)i;
    }

    g_renderer_slot_binding_count = (uint8_t)count;
    return true;
}

static bool tinyclj_scene_bench_parse_u32_arg(ID value, uint32_t default_value, uint32_t *out_value) {
    if (!out_value) {
        return false;
    }
    *out_value = default_value;
    if (!value) {
        return true;
    }
    if (!is_fixnum(value)) {
        return false;
    }
    int32_t parsed = as_fixnum(value);
    if (parsed <= 0) {
        return false;
    }
    *out_value = (uint32_t)parsed;
    return true;
}

static uint32_t tinyclj_scene_bench_elapsed_ms(uint32_t start_ms, uint32_t end_ms) {
    if (end_ms >= start_ms) {
        return end_ms - start_ms;
    }
    return (86400000u - start_ms) + end_ms;
}

static bool tinyclj_scene_bench_run_scene(ID scene,
                                          VgFrameBuffer *fb,
                                          uint32_t warmup_iterations,
                                          uint32_t measured_iterations,
                                          uint32_t *out_total_ms) {
    if (!scene || !fb || !out_total_ms || measured_iterations == 0u) {
        return false;
    }

    for (uint32_t i = 0; i < warmup_iterations; i++) {
        if (!vg_render_scene_record(scene, fb)) {
            return false;
        }
    }

    uint32_t start_ms = platform_current_time_ms();
    for (uint32_t i = 0; i < measured_iterations; i++) {
        if (!vg_render_scene_record(scene, fb)) {
            return false;
        }
    }
    uint32_t end_ms = platform_current_time_ms();
    *out_total_ms = tinyclj_scene_bench_elapsed_ms(start_ms, end_ms);
    return true;
}

static int32_t tinyclj_clamp_u64_to_fixnum(uint64_t value) {
    if (value > (uint64_t)FIXNUM_MAX) {
        return (int32_t)FIXNUM_MAX;
    }
    return (int32_t)value;
}

static int32_t tinyclj_clamp_i64_to_fixnum(int64_t value) {
    if (value > (int64_t)FIXNUM_MAX) {
        return (int32_t)FIXNUM_MAX;
    }
    if (value < (int64_t)FIXNUM_MIN) {
        return (int32_t)FIXNUM_MIN;
    }
    return (int32_t)value;
}

static int32_t tinyclj_fixed_raw_to_int_trunc_zero(int32_t raw) {
    if (raw >= 0) {
        return raw >> CLJ_FIXED_FRAC_BITS;
    }
    return -(((-raw) >> CLJ_FIXED_FRAC_BITS));
}

static bool tinyclj_runtime_parse_renderer_slot(ID slot_obj, uint8_t *out_slot) {
    if (!out_slot) {
        return false;
    }

    if (is_fixnum(slot_obj)) {
        int32_t slot = as_fixnum(slot_obj);
        if (slot < 0 || slot >= (int32_t)VG_RENDERED_STATE_MAX_SLOTS) {
            return false;
        }
        *out_slot = (uint8_t)slot;
        return true;
    }

    if (!slot_obj || !is_symbol(slot_obj) || !tinyclj_runtime_intern_common_keywords()) {
        return false;
    }

    for (uint8_t i = 0; i < g_renderer_slot_binding_count; i++) {
        if (g_renderer_slot_bindings[i].id == slot_obj) {
            *out_slot = g_renderer_slot_bindings[i].index;
            return true;
        }
    }

    size_t named_slot_count = sizeof(g_named_renderer_slots) / sizeof(g_named_renderer_slots[0]);
    for (size_t i = 0; i < named_slot_count; i++) {
        if (slot_obj == *g_named_renderer_slots[i].slot) {
            *out_slot = g_named_renderer_slots[i].index;
            return true;
        }
    }
    return false;
}

static bool tinyclj_runtime_parse_rendered_field(ID field_obj, VgRenderedField *out_field) {
    if (!out_field || !field_obj || !is_symbol(field_obj) || !tinyclj_runtime_intern_common_keywords()) {
        return false;
    }

    size_t field_count = sizeof(g_rendered_field_keywords) / sizeof(g_rendered_field_keywords[0]);
    for (size_t i = 0; i < field_count; i++) {
        if (field_obj == *g_rendered_field_keywords[i].slot) {
            *out_field = g_rendered_field_keywords[i].field;
            return true;
        }
    }
    return false;
}

/**
 * @brief Reset cached tiny-fx runtime state used by builtin dispatch.
 *
 * Clears slot-id bindings and lazily interned keyword caches. This keeps test
 * resets deterministic when builtins are re-registered between runs.
 */
void builtins_tiny_fx_gfx_reset_cached_state(void) {
    tinyclj_runtime_clear_slot_bindings();
    tinyclj_runtime_reset_keyword_cache();
}

ID native_tinyfx_color_color(ID *args, unsigned int argc) {
    if (argc == 1u) {
        if (!is_fixnum(args[0])) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            "tiny-fx.gfx/color RGB888 input must be an integer in range 0x000000..0xFFFFFF",
                            __FILE__,
                            __LINE__,
                            0);
            return NULL;
        }
        int32_t rgb = as_fixnum(args[0]);
        if (rgb < 0 || rgb > 0xFFFFFF) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            "tiny-fx.gfx/color RGB888 input must be an integer in range 0x000000..0xFFFFFF",
                            __FILE__,
                            __LINE__,
                            0);
            return NULL;
        }
        return tinyfx_color_rgb565((uint8_t)((uint32_t)rgb >> 16),
                                   (uint8_t)(((uint32_t)rgb >> 8) & 0xFFu),
                                   (uint8_t)((uint32_t)rgb & 0xFFu));
    }
    if (argc == 3u) {
        uint8_t r = 0u;
        uint8_t g = 0u;
        uint8_t b = 0u;
        if (!tinyfx_color_parse_u8(args[0], &r) ||
            !tinyfx_color_parse_u8(args[1], &g) ||
            !tinyfx_color_parse_u8(args[2], &b)) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            "tiny-fx.gfx/color RGB channel inputs must be integers in range 0..255",
                            __FILE__,
                            __LINE__,
                            0);
            return NULL;
        }
        return tinyfx_color_rgb565(r, g, b);
    }
    throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                              "Wrong number of args (%u) passed to: tiny-fx.gfx/color",
                              argc);
    return NULL;
}

#ifdef DEBUG
/**
 * @brief Benchmark decode plus render of the current Clojure demo scenes.
 *
 * Uses the current `tiny-fx.game-demo/create-demo-bundle` output and a static
 * framebuffer to avoid heap allocation in the hot path.
 *
 * @param args Optional `[iterations warmup]`
 * @param argc Number of arguments
 * @return Metrics map on success, or `NULL` after throwing an exception
 */
ID native_tinyfx_gfx_bench_vector_scene_bench(ID *args, unsigned int argc) {
    CHECK_ARITY_MAX(argc, 2, "tiny-fx.gfx-bench/vector-scene-bench");

    EvalState *st = builtin_get_eval_state();
    if (!st) {
        throw_exception(EXCEPTION_RUNTIME,
                        "tiny-fx.gfx-bench/vector-scene-bench: EvalState not available",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    uint32_t iterations = 800u;
    uint32_t warmup = 40u;
    if (argc >= 1 && !tinyclj_scene_bench_parse_u32_arg(args[0], iterations, &iterations)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "tiny-fx.gfx-bench/vector-scene-bench iterations must be a positive integer",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }
    if (argc >= 2 && !tinyclj_scene_bench_parse_u32_arg(args[1], warmup, &warmup)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "tiny-fx.gfx-bench/vector-scene-bench warmup must be a positive integer",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    if (!tiny_fx_gfx_require_records_namespace(st) ||
        !tiny_fx_gfx_ensure_schema(st) ||
        !require_namespace_by_name(st, "tiny-fx.game-demo")) {
        throw_exception(EXCEPTION_RUNTIME,
                        "tiny-fx.gfx-bench/vector-scene-bench failed to initialize tiny-fx scene schema",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    ID bundle = eval_string("(tiny-fx.game-demo/create-demo-bundle)", st);
    if (!bundle || !is_vector(bundle)) {
        throw_exception(EXCEPTION_RUNTIME,
                        "tiny-fx.gfx-bench/vector-scene-bench failed to build demo scene bundle",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    CljPersistentVector *vec = as_vector(bundle);
    if (!vec || vector_count(vec) < 3) {
        throw_exception(EXCEPTION_RUNTIME,
                        "tiny-fx.gfx-bench/vector-scene-bench demo bundle shape mismatch",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    ID deco_scene = vector_nth(vec, 0);
    ID score_scene = vector_nth(vec, 1);
    ID game_scene = vector_nth(vec, 2);
    if (!deco_scene || !score_scene || !game_scene) {
        throw_exception(EXCEPTION_RUNTIME,
                        "tiny-fx.gfx-bench/vector-scene-bench demo scenes are missing",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    uint16_t *pixels = (uint16_t *)CLJ_MALLOC(TINYCLJ_SCENE_BENCH_PIXEL_COUNT * sizeof(uint16_t));
    if (!pixels) {
        throw_exception(EXCEPTION_RUNTIME,
                        "tiny-fx.gfx-bench/vector-scene-bench failed to allocate framebuffer",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    VgFrameBuffer fb;
    if (!vg_framebuffer_init(&fb,
                             TINYCLJ_SCENE_BENCH_WIDTH,
                             TINYCLJ_SCENE_BENCH_HEIGHT,
                             pixels,
                             TINYCLJ_SCENE_BENCH_PIXEL_COUNT)) {
        CLJ_FREE(pixels);
        throw_exception(EXCEPTION_RUNTIME,
                        "tiny-fx.gfx-bench/vector-scene-bench framebuffer init failed",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }
    vg_framebuffer_clear(&fb, 0x0000u);

    uint32_t deco_total_ms = 0u;
    uint32_t score_total_ms = 0u;
    uint32_t game_total_ms = 0u;
    bool ok = tinyclj_scene_bench_run_scene(deco_scene, &fb, warmup, iterations, &deco_total_ms) &&
              tinyclj_scene_bench_run_scene(score_scene, &fb, warmup, iterations, &score_total_ms) &&
              tinyclj_scene_bench_run_scene(game_scene, &fb, warmup, iterations, &game_total_ms);
    CLJ_FREE(pixels);
    if (!ok) {
        throw_exception(EXCEPTION_RUNTIME,
                        "tiny-fx.gfx-bench/vector-scene-bench render failed",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    uint64_t deco_us_per_frame = ((uint64_t)deco_total_ms * 1000ull) / (uint64_t)iterations;
    uint64_t score_us_per_frame = ((uint64_t)score_total_ms * 1000ull) / (uint64_t)iterations;
    uint64_t game_us_per_frame = ((uint64_t)game_total_ms * 1000ull) / (uint64_t)iterations;
    uint64_t total_ms = (uint64_t)deco_total_ms + (uint64_t)score_total_ms + (uint64_t)game_total_ms;
    uint32_t slot_count = vector_count(vec);

    if (!tinyclj_runtime_intern_common_keywords()) {
        throw_exception(EXCEPTION_RUNTIME,
                        "tiny-fx.gfx-bench/vector-scene-bench failed to intern keyword cache",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    ID platform_str = make_string(platform_name());
    if (!platform_str) {
        return NULL;
    }

    CljPersistentMap *result = make_map_from_kv(
        11,
        g_kw_platform, platform_str,
        g_kw_iterations, fixnum((int32_t)iterations),
        g_kw_warmup, fixnum((int32_t)warmup),
        g_kw_slot_count, fixnum((int32_t)slot_count),
        g_kw_deco_total_ms, fixnum((int32_t)deco_total_ms),
        g_kw_score_total_ms, fixnum((int32_t)score_total_ms),
        g_kw_game_total_ms, fixnum((int32_t)game_total_ms),
        g_kw_deco_us_per_frame, fixnum(tinyclj_clamp_u64_to_fixnum(deco_us_per_frame)),
        g_kw_score_us_per_frame, fixnum(tinyclj_clamp_u64_to_fixnum(score_us_per_frame)),
        g_kw_game_us_per_frame, fixnum(tinyclj_clamp_u64_to_fixnum(game_us_per_frame)),
        g_kw_total_ms, fixnum(tinyclj_clamp_u64_to_fixnum(total_ms)));
    RELEASE(platform_str);
    return AUTORELEASE(result);
}
#endif

/**
 * @brief Register current renderer slot descriptors and start the host renderer.
 *
 * Slot registration is maintained even when the active runtime has no renderer
 * backend, so state-query builtins can still resolve symbolic slot ids in tests.
 *
 * @param args Optional `[slot-descriptors]`
 * @param argc Number of arguments
 * @return `clj_true` on successful backend start, `clj_false` when unsupported
 */
ID native_tinyclj_runtime_start_renderer(ID *args, unsigned int argc) {
    CHECK_ARITY_MAX(argc, 1, "tiny-clj.runtime/start-renderer!");

    ID slot_atoms = NULL;
    if (argc == 1) {
        slot_atoms = args[0];
        if (slot_atoms && !is_vector(slot_atoms)) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            "tiny-clj.runtime/start-renderer! expects nil or a vector of slot atoms",
                            __FILE__,
                            __LINE__,
                            0);
            return NULL;
        }
        if (!builtins_tiny_fx_gfx_register_slot_bindings(slot_atoms)) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            "tiny-clj.runtime/start-renderer! slot descriptors must contain unique {:id kw :atom atom} entries",
                            __FILE__,
                            __LINE__,
                            0);
            return NULL;
        }
    }

    return tiny_renderer_lifecycle_start(slot_atoms) ? clj_true : clj_false;
}

/**
 * @brief Stop the host renderer if a backend is installed.
 *
 * @param args Unused
 * @param argc Number of arguments, must be zero
 * @return `clj_true` on success, `clj_false` when unsupported
 */
ID native_tinyclj_runtime_stop_renderer(ID *args, unsigned int argc) {
    (void)args;
    CHECK_ARITY(argc, 0, "tiny-clj.runtime/stop-renderer!");
    return tiny_renderer_lifecycle_stop() ? clj_true : clj_false;
}

/**
 * @brief Read the latest captured render transform state for one entity.
 *
 * @param args `[slot entity-id]`
 * @param argc Number of arguments, must be two
 * @return State map or `NULL` when no snapshot entry exists
 */
ID native_tinyclj_runtime_renderer_state(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "tiny-clj.runtime/renderer-state");

    uint8_t slot = 0u;
    if (!tinyclj_runtime_parse_renderer_slot(args[0], &slot)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "tiny-clj.runtime/renderer-state slot must be :deco, :score, :game or a registered slot index/id",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }
    if (!args[1]) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "tiny-clj.runtime/renderer-state entity-id must not be nil",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    VgRenderedEntityState state;
    if (!vg_rendered_state_query_entity(slot, (uintptr_t)args[1], &state)) {
        return NULL;
    }
    if (!tinyclj_runtime_intern_common_keywords()) {
        return NULL;
    }

    int32_t tx = tinyclj_fixed_raw_to_int_trunc_zero(state.world_t.m02);
    int32_t ty = tinyclj_fixed_raw_to_int_trunc_zero(state.world_t.m12);
    return AUTORELEASE(make_map_from_kv(
        10,
        g_kw_tx, fixnum(tinyclj_clamp_i64_to_fixnum(tx)),
        g_kw_ty, fixnum(tinyclj_clamp_i64_to_fixnum(ty)),
        g_kw_m00, fixnum(tinyclj_clamp_i64_to_fixnum(state.world_t.m00)),
        g_kw_m01, fixnum(tinyclj_clamp_i64_to_fixnum(state.world_t.m01)),
        g_kw_m02, fixnum(tinyclj_clamp_i64_to_fixnum(state.world_t.m02)),
        g_kw_m10, fixnum(tinyclj_clamp_i64_to_fixnum(state.world_t.m10)),
        g_kw_m11, fixnum(tinyclj_clamp_i64_to_fixnum(state.world_t.m11)),
        g_kw_m12, fixnum(tinyclj_clamp_i64_to_fixnum(state.world_t.m12)),
        g_kw_snapshot_gen, fixnum((int32_t)state.snapshot_generation),
        g_kw_ts_ms, fixnum((int32_t)state.frame_time_ms)));
}

/**
 * @brief Read the active keyframe index for one captured timeline field.
 *
 * @param args `[slot entity-id field]`
 * @param argc Number of arguments, must be three
 * @return Fixnum step index or `NULL` when no timeline sample exists
 */
ID native_tinyclj_runtime_renderer_timeline_step(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 3, "tiny-clj.runtime/renderer-timeline-step");

    uint8_t slot = 0u;
    if (!tinyclj_runtime_parse_renderer_slot(args[0], &slot)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "tiny-clj.runtime/renderer-timeline-step slot must be :deco, :score, :game or a registered slot index/id",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }
    if (!args[1]) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "tiny-clj.runtime/renderer-timeline-step entity-id must not be nil",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    VgRenderedField field = VG_RENDERED_FIELD_NONE;
    if (!tinyclj_runtime_parse_rendered_field(args[2], &field)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "tiny-clj.runtime/renderer-timeline-step field must be a supported keyword",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    VgRenderedTimelineState state;
    if (!vg_rendered_state_query_timeline(slot, (uintptr_t)args[1], field, &state)) {
        return NULL;
    }
    return fixnum((int32_t)state.sample.step_index);
}

/**
 * @brief Read phase metadata for one captured timeline field.
 *
 * @param args `[slot entity-id field]`
 * @param argc Number of arguments, must be three
 * @return Timeline metadata map or `NULL` when no timeline sample exists
 */
ID native_tinyclj_runtime_renderer_timeline_progress(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 3, "tiny-clj.runtime/renderer-timeline-progress");

    uint8_t slot = 0u;
    if (!tinyclj_runtime_parse_renderer_slot(args[0], &slot)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "tiny-clj.runtime/renderer-timeline-progress slot must be :deco, :score, :game or a registered slot index/id",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }
    if (!args[1]) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "tiny-clj.runtime/renderer-timeline-progress entity-id must not be nil",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    VgRenderedField field = VG_RENDERED_FIELD_NONE;
    if (!tinyclj_runtime_parse_rendered_field(args[2], &field)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "tiny-clj.runtime/renderer-timeline-progress field must be a supported keyword",
                        __FILE__,
                        __LINE__,
                        0);
        return NULL;
    }

    VgRenderedTimelineState state;
    if (!vg_rendered_state_query_timeline(slot, (uintptr_t)args[1], field, &state)) {
        return NULL;
    }
    if (!tinyclj_runtime_intern_common_keywords()) {
        return NULL;
    }

    uint32_t period = state.sample.period_ms;
    uint32_t phase = state.sample.phase_ms;
    uint32_t permille = 0u;
    if (period > 0u) {
        uint64_t scaled = ((uint64_t)phase * 1000ull) / (uint64_t)period;
        permille = (scaled > 1000ull) ? 1000u : (uint32_t)scaled;
    }

    return AUTORELEASE(make_map_from_kv(
        14,
        g_kw_step, fixnum((int32_t)state.sample.step_index),
        g_kw_count, fixnum((int32_t)state.sample.keyframe_count),
        g_kw_phase_ms, fixnum((int32_t)phase),
        g_kw_period_ms, fixnum((int32_t)period),
        g_kw_loop, state.sample.loop ? clj_true : clj_false,
        g_kw_end_event, state.sample.end_event ? clj_true : clj_false,
        g_kw_event_id, state.sample.event_id_bits ? (ID)state.sample.event_id_bits : NULL,
        g_kw_slot_id, args[0],
        g_kw_entity_id, args[1],
        g_kw_field, args[2],
        g_kw_at_end, state.sample.at_end ? clj_true : clj_false,
        g_kw_permille, fixnum((int32_t)permille),
        g_kw_snapshot_gen, fixnum((int32_t)state.snapshot_generation),
        g_kw_ts_ms, fixnum((int32_t)state.frame_time_ms)));
}

#endif
