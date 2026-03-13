#include "builtins_tiny_fx_gfx.h"

#include <stdint.h>
#include <string.h>

#include "eval.h"
#include "exception.h"
#include "map.h"
#include "memory.h"
#include "platform.h"
#include "renderer_lifecycle.h"
#include "rendered_state_snapshot.h"
#include "scene.h"
#include "symbol.h"
#include "tiny_fx_gfx.h"
#include "vector.h"

#define TINYCLJ_SCENE_BENCH_WIDTH 320u
#define TINYCLJ_SCENE_BENCH_HEIGHT 240u
#define TINYCLJ_SCENE_BENCH_PIXEL_COUNT ((size_t)TINYCLJ_SCENE_BENCH_WIDTH * (size_t)TINYCLJ_SCENE_BENCH_HEIGHT)

typedef struct {
    ID id;
    uint8_t index;
} TinycljRendererSlotBinding;

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
static ID g_kw_permille = NULL;

static bool tinyclj_runtime_intern_common_keywords(void) {
    if (g_kw_id) {
        return true;
    }

    g_kw_id = intern_symbol_global(":id");
    g_kw_atom = intern_symbol_global(":atom");
    g_kw_deco = intern_symbol_global(":deco");
    g_kw_score = intern_symbol_global(":score");
    g_kw_game = intern_symbol_global(":game");

    g_kw_t = intern_symbol_global(":t");
    g_kw_style = intern_symbol_global(":style");
    g_kw_visible = intern_symbol_global(":visible");
    g_kw_children = intern_symbol_global(":children");
    g_kw_pts = intern_symbol_global(":pts");
    g_kw_closed = intern_symbol_global(":closed");
    g_kw_x = intern_symbol_global(":x");
    g_kw_y = intern_symbol_global(":y");
    g_kw_w = intern_symbol_global(":w");
    g_kw_h = intern_symbol_global(":h");
    g_kw_x1 = intern_symbol_global(":x1");
    g_kw_y1 = intern_symbol_global(":y1");
    g_kw_x2 = intern_symbol_global(":x2");
    g_kw_y2 = intern_symbol_global(":y2");
    g_kw_x3 = intern_symbol_global(":x3");
    g_kw_y3 = intern_symbol_global(":y3");
    g_kw_scale = intern_symbol_global(":scale");
    g_kw_rot = intern_symbol_global(":rot");
    g_kw_text = intern_symbol_global(":text");

    g_kw_platform = intern_symbol_global(":platform");
    g_kw_iterations = intern_symbol_global(":iterations");
    g_kw_warmup = intern_symbol_global(":warmup");
    g_kw_slot_count = intern_symbol_global(":slot-count");
    g_kw_deco_total_ms = intern_symbol_global(":deco-total-ms");
    g_kw_score_total_ms = intern_symbol_global(":score-total-ms");
    g_kw_game_total_ms = intern_symbol_global(":game-total-ms");
    g_kw_deco_us_per_frame = intern_symbol_global(":deco-us-per-frame");
    g_kw_score_us_per_frame = intern_symbol_global(":score-us-per-frame");
    g_kw_game_us_per_frame = intern_symbol_global(":game-us-per-frame");
    g_kw_total_ms = intern_symbol_global(":total-ms");

    g_kw_tx = intern_symbol_global(":tx");
    g_kw_ty = intern_symbol_global(":ty");
    g_kw_m00 = intern_symbol_global(":m00");
    g_kw_m01 = intern_symbol_global(":m01");
    g_kw_m02 = intern_symbol_global(":m02");
    g_kw_m10 = intern_symbol_global(":m10");
    g_kw_m11 = intern_symbol_global(":m11");
    g_kw_m12 = intern_symbol_global(":m12");
    g_kw_snapshot_gen = intern_symbol_global(":snapshot-gen");
    g_kw_ts_ms = intern_symbol_global(":ts-ms");

    g_kw_step = intern_symbol_global(":step");
    g_kw_count = intern_symbol_global(":count");
    g_kw_phase_ms = intern_symbol_global(":phase-ms");
    g_kw_period_ms = intern_symbol_global(":period-ms");
    g_kw_loop = intern_symbol_global(":loop");
    g_kw_permille = intern_symbol_global(":permille");

    return g_kw_id && g_kw_atom && g_kw_deco && g_kw_score && g_kw_game && g_kw_t && g_kw_style &&
           g_kw_visible && g_kw_children && g_kw_pts && g_kw_closed && g_kw_x && g_kw_y && g_kw_w &&
           g_kw_h && g_kw_x1 && g_kw_y1 && g_kw_x2 && g_kw_y2 && g_kw_x3 && g_kw_y3 && g_kw_scale &&
           g_kw_rot && g_kw_text && g_kw_platform && g_kw_iterations && g_kw_warmup && g_kw_slot_count &&
           g_kw_deco_total_ms && g_kw_score_total_ms && g_kw_game_total_ms && g_kw_deco_us_per_frame &&
           g_kw_score_us_per_frame && g_kw_game_us_per_frame && g_kw_total_ms && g_kw_tx && g_kw_ty &&
           g_kw_m00 && g_kw_m01 && g_kw_m02 && g_kw_m10 && g_kw_m11 && g_kw_m12 && g_kw_snapshot_gen &&
           g_kw_ts_ms && g_kw_step && g_kw_count && g_kw_phase_ms && g_kw_period_ms && g_kw_loop &&
           g_kw_permille;
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

static bool tinyclj_runtime_register_slot_bindings(ID slot_atoms) {
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

    if (slot_obj == g_kw_deco) {
        *out_slot = 0u;
        return true;
    }
    if (slot_obj == g_kw_score) {
        *out_slot = 1u;
        return true;
    }
    if (slot_obj == g_kw_game) {
        *out_slot = 2u;
        return true;
    }
    return false;
}

static bool tinyclj_runtime_parse_rendered_field(ID field_obj, VgRenderedField *out_field) {
    if (!out_field || !field_obj || !is_symbol(field_obj) || !tinyclj_runtime_intern_common_keywords()) {
        return false;
    }

    if (field_obj == g_kw_t) {
        *out_field = VG_RENDERED_FIELD_T;
        return true;
    }
    if (field_obj == g_kw_style) {
        *out_field = VG_RENDERED_FIELD_STYLE;
        return true;
    }
    if (field_obj == g_kw_visible) {
        *out_field = VG_RENDERED_FIELD_VISIBLE;
        return true;
    }
    if (field_obj == g_kw_children) {
        *out_field = VG_RENDERED_FIELD_CHILDREN;
        return true;
    }
    if (field_obj == g_kw_pts) {
        *out_field = VG_RENDERED_FIELD_PTS;
        return true;
    }
    if (field_obj == g_kw_closed) {
        *out_field = VG_RENDERED_FIELD_CLOSED;
        return true;
    }
    if (field_obj == g_kw_x) {
        *out_field = VG_RENDERED_FIELD_X;
        return true;
    }
    if (field_obj == g_kw_y) {
        *out_field = VG_RENDERED_FIELD_Y;
        return true;
    }
    if (field_obj == g_kw_w) {
        *out_field = VG_RENDERED_FIELD_W;
        return true;
    }
    if (field_obj == g_kw_h) {
        *out_field = VG_RENDERED_FIELD_H;
        return true;
    }
    if (field_obj == g_kw_x1) {
        *out_field = VG_RENDERED_FIELD_X1;
        return true;
    }
    if (field_obj == g_kw_y1) {
        *out_field = VG_RENDERED_FIELD_Y1;
        return true;
    }
    if (field_obj == g_kw_x2) {
        *out_field = VG_RENDERED_FIELD_X2;
        return true;
    }
    if (field_obj == g_kw_y2) {
        *out_field = VG_RENDERED_FIELD_Y2;
        return true;
    }
    if (field_obj == g_kw_x3) {
        *out_field = VG_RENDERED_FIELD_X3;
        return true;
    }
    if (field_obj == g_kw_y3) {
        *out_field = VG_RENDERED_FIELD_Y3;
        return true;
    }
    if (field_obj == g_kw_scale) {
        *out_field = VG_RENDERED_FIELD_SCALE;
        return true;
    }
    if (field_obj == g_kw_rot) {
        *out_field = VG_RENDERED_FIELD_ROT;
        return true;
    }
    if (field_obj == g_kw_text) {
        *out_field = VG_RENDERED_FIELD_TEXT;
        return true;
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

    g_kw_id = NULL;
    g_kw_atom = NULL;
    g_kw_deco = NULL;
    g_kw_score = NULL;
    g_kw_game = NULL;

    g_kw_t = NULL;
    g_kw_style = NULL;
    g_kw_visible = NULL;
    g_kw_children = NULL;
    g_kw_pts = NULL;
    g_kw_closed = NULL;
    g_kw_x = NULL;
    g_kw_y = NULL;
    g_kw_w = NULL;
    g_kw_h = NULL;
    g_kw_x1 = NULL;
    g_kw_y1 = NULL;
    g_kw_x2 = NULL;
    g_kw_y2 = NULL;
    g_kw_x3 = NULL;
    g_kw_y3 = NULL;
    g_kw_scale = NULL;
    g_kw_rot = NULL;
    g_kw_text = NULL;

    g_kw_platform = NULL;
    g_kw_iterations = NULL;
    g_kw_warmup = NULL;
    g_kw_slot_count = NULL;
    g_kw_deco_total_ms = NULL;
    g_kw_score_total_ms = NULL;
    g_kw_game_total_ms = NULL;
    g_kw_deco_us_per_frame = NULL;
    g_kw_score_us_per_frame = NULL;
    g_kw_game_us_per_frame = NULL;
    g_kw_total_ms = NULL;

    g_kw_tx = NULL;
    g_kw_ty = NULL;
    g_kw_m00 = NULL;
    g_kw_m01 = NULL;
    g_kw_m02 = NULL;
    g_kw_m10 = NULL;
    g_kw_m11 = NULL;
    g_kw_m12 = NULL;
    g_kw_snapshot_gen = NULL;
    g_kw_ts_ms = NULL;

    g_kw_step = NULL;
    g_kw_count = NULL;
    g_kw_phase_ms = NULL;
    g_kw_period_ms = NULL;
    g_kw_loop = NULL;
    g_kw_permille = NULL;
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

    if (!tiny_fx_gfx_ensure_schema(st) || !require_namespace_by_name(st, "tiny-fx.game-demo")) {
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
        if (!tinyclj_runtime_register_slot_bindings(slot_atoms)) {
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
        8,
        g_kw_step, fixnum((int32_t)state.sample.step_index),
        g_kw_count, fixnum((int32_t)state.sample.keyframe_count),
        g_kw_phase_ms, fixnum((int32_t)phase),
        g_kw_period_ms, fixnum((int32_t)period),
        g_kw_loop, state.sample.loop ? clj_true : clj_false,
        g_kw_permille, fixnum((int32_t)permille),
        g_kw_snapshot_gen, fixnum((int32_t)state.snapshot_generation),
        g_kw_ts_ms, fixnum((int32_t)state.frame_time_ms)));
}
