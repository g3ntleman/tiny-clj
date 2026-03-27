#include "fx_collision.h"
#include "memory.h"
#include "symbol.h"
#include "seq.h"
#include "subjective-c/map.h"
#include "subjective-c/value.h"

/**
 * @brief Performs a fixed AABB overlap test.
 *
 * Uses integer SAT-style reject checks only. The implementation is O(1),
 * allocation-free, and uses no floating-point operations.
 *
 * @param a First box.
 * @param b Second box.
 * @return true when hitboxes overlap; otherwise false.
 */
bool vg_collision_detect_aabb_overlap(const VgAabb *a, const VgAabb *b) {
    if (!a || !b) {
        return false;
    }

    /* Fast SAT-style reject: avoid extra comparisons on non-overlap frames. */
    if (a->max_x <= b->min_x || a->min_x >= b->max_x) {
        return false;
    }
    if (a->max_y <= b->min_y || a->min_y >= b->max_y) {
        return false;
    }
    return true;
}

bool vg_collision_selector_matches_entity_prototype(ID entity_prototype,
                                                    ID selector) {
    if (!entity_prototype || !selector) {
        return false;
    }
    return entity_prototype == selector || clj_equal(entity_prototype, selector);
}

/* ============================================================
 * vg_sweep_aabb — predictive CCD first-hit query
 * ============================================================ */

/* Cached keywords used by the sweep + native bindings. */
static ID g_kw_id     = NULL;
static ID g_kw_x      = NULL;
static ID g_kw_y      = NULL;
static ID g_kw_w      = NULL;
static ID g_kw_h      = NULL;
static ID g_kw_vx     = NULL;
static ID g_kw_vy     = NULL;
static ID g_kw_hit_id = NULL;
static ID g_kw_normal = NULL;
static ID g_kw_left   = NULL;
static ID g_kw_right  = NULL;
static ID g_kw_top    = NULL;
static ID g_kw_bottom = NULL;
static ID g_kw_from_x = NULL;
static ID g_kw_from_y = NULL;
static ID g_kw_to_x   = NULL;
static ID g_kw_to_y   = NULL;
static ID g_kw_start_ms = NULL;
static ID g_kw_end_ms   = NULL;

static bool sweep_init_keywords(void) {
    static const IdSymbolCacheEntry kws[] = {
        {&g_kw_id,       ":id"},
        {&g_kw_x,        ":x"},
        {&g_kw_y,        ":y"},
        {&g_kw_w,        ":w"},
        {&g_kw_h,        ":h"},
        {&g_kw_vx,       ":vx"},
        {&g_kw_vy,       ":vy"},
        {&g_kw_hit_id,   ":hit-id"},
        {&g_kw_normal,   ":normal"},
        {&g_kw_left,     ":left"},
        {&g_kw_right,    ":right"},
        {&g_kw_top,      ":top"},
        {&g_kw_bottom,   ":bottom"},
        {&g_kw_from_x,   ":from-x"},
        {&g_kw_from_y,   ":from-y"},
        {&g_kw_to_x,     ":to-x"},
        {&g_kw_to_y,     ":to-y"},
        {&g_kw_start_ms, ":start-ms"},
        {&g_kw_end_ms,   ":end-ms"},
    };
    return id_symbol_cache_init_global(kws, sizeof(kws) / sizeof(kws[0]));
}

/* Read an integer field from a Clojure map; returns 0 on missing/non-fixnum. */
static int32_t map_get_int(ID map, ID key) {
    ID v = map_get_sentinel(map, key, NULL);
    return (v && is_fixnum((CljValue)(uintptr_t)v))
           ? as_fixnum((CljValue)(uintptr_t)v)
           : 0;
}

static bool rat_lt_i32(int32_t a_num, int32_t a_den,
                       int32_t b_num, int32_t b_den) {
    return (int64_t)a_num * (int64_t)b_den < (int64_t)b_num * (int64_t)a_den;
}

VgSweepResult vg_sweep_aabb(VgAabb mover,
                             int32_t vx, int32_t vy,
                             ID obstacles, int32_t max_t_ms,
                             void *debug_buf) {
    (void)debug_buf;

    VgSweepResult best = {.obstacle_id = -1, .gap_num = 0, .gap_denom = 1,
                          .normal_axis = 0, .normal_sign = 0};

    if (!obstacles || max_t_ms <= 0) {
        return best;
    }
    if (!sweep_init_keywords()) {
        return best;
    }

    /* best_gap_num/denom tracks the earliest entry time seen so far.
     * We compare fractions a/b < c/d as a*d < c*b (all denominators positive). */
    int32_t best_num   = max_t_ms; /* exclusive upper bound */
    int32_t best_denom = 1;
    bool    best_set   = false;

    /* Convert any seqable (vector, list, …) to a CLJ_SEQ iterator so that
     * seq_first / seq_next_inplace work correctly. make_seq returns NULL for
     * nil / empty input, which terminates the loop immediately. */
    ID seq = (ID)make_seq(obstacles);
    while (seq && !IS_IMMEDIATE(seq)) {
        ID elem = seq_first(seq);
        if (!elem) {
            seq_next_inplace(&seq);
            continue;
        }

        int32_t obs_id = map_get_int(elem, g_kw_id);
        int32_t ox     = map_get_int(elem, g_kw_x);
        int32_t oy     = map_get_int(elem, g_kw_y);
        int32_t ow     = map_get_int(elem, g_kw_w);
        int32_t oh     = map_get_int(elem, g_kw_h);

        /* Obstacle AABB: [ox, ox+ow) x [oy, oy+oh) */
        int32_t obs_min_x = ox;
        int32_t obs_max_x = ox + ow;
        int32_t obs_min_y = oy;
        int32_t obs_max_y = oy + oh;

        /* Swept AABB entry/exit times per axis as positive rationals.
         * Entry time = max(tx_entry, ty_entry), exit time = min(tx_exit, ty_exit). */
        int32_t entry_num_x = 0, entry_den_x = 1;
        int32_t exit_num_x  = INT32_MAX, exit_den_x = 1;
        int32_t entry_num_y = 0, entry_den_y = 1;
        int32_t exit_num_y  = INT32_MAX, exit_den_y = 1;
        int sign_x = 0;
        int sign_y = 0;

        if (vx > 0) {
            int32_t raw_entry_x = obs_min_x - mover.max_x;
            int32_t raw_exit_x  = obs_max_x - mover.min_x;
            if (raw_exit_x <= 0) {
                seq_next_inplace(&seq);
                continue;
            }
            entry_num_x = raw_entry_x > 0 ? raw_entry_x : 0;
            exit_num_x  = raw_exit_x;
            entry_den_x = vx;
            exit_den_x  = vx;
            sign_x      = -1; /* hits left face */
        } else if (vx < 0) {
            int32_t abs_vx      = -vx;
            int32_t raw_entry_x = mover.min_x - obs_max_x;
            int32_t raw_exit_x  = mover.max_x - obs_min_x;
            if (raw_exit_x <= 0) {
                seq_next_inplace(&seq);
                continue;
            }
            entry_num_x = raw_entry_x > 0 ? raw_entry_x : 0;
            exit_num_x  = raw_exit_x;
            entry_den_x = abs_vx;
            exit_den_x  = abs_vx;
            sign_x      = 1; /* hits right face */
        } else if (mover.max_x <= obs_min_x || mover.min_x >= obs_max_x) {
            seq_next_inplace(&seq);
            continue;
        }

        if (vy > 0) {
            int32_t raw_entry_y = obs_min_y - mover.max_y;
            int32_t raw_exit_y  = obs_max_y - mover.min_y;
            if (raw_exit_y <= 0) {
                seq_next_inplace(&seq);
                continue;
            }
            entry_num_y = raw_entry_y > 0 ? raw_entry_y : 0;
            exit_num_y  = raw_exit_y;
            entry_den_y = vy;
            exit_den_y  = vy;
            sign_y      = -1; /* hits top face */
        } else if (vy < 0) {
            int32_t abs_vy      = -vy;
            int32_t raw_entry_y = mover.min_y - obs_max_y;
            int32_t raw_exit_y  = mover.max_y - obs_min_y;
            if (raw_exit_y <= 0) {
                seq_next_inplace(&seq);
                continue;
            }
            entry_num_y = raw_entry_y > 0 ? raw_entry_y : 0;
            exit_num_y  = raw_exit_y;
            entry_den_y = abs_vy;
            exit_den_y  = abs_vy;
            sign_y      = 1; /* hits bottom face */
        } else if (mover.max_y <= obs_min_y || mover.min_y >= obs_max_y) {
            seq_next_inplace(&seq);
            continue;
        }

        bool x_entry_later = rat_lt_i32(entry_num_y, entry_den_y, entry_num_x, entry_den_x);
        bool y_entry_later = rat_lt_i32(entry_num_x, entry_den_x, entry_num_y, entry_den_y);

        int32_t gap_num;
        int32_t gap_denom;
        int     axis;
        int     sign;
        if (x_entry_later || (!y_entry_later && sign_x != 0)) {
            gap_num   = entry_num_x;
            gap_denom = entry_den_x;
            axis      = 0;
            sign      = sign_x;
        } else {
            gap_num   = entry_num_y;
            gap_denom = entry_den_y;
            axis      = 1;
            sign      = sign_y;
        }

        if (gap_denom <= 0) {
            seq_next_inplace(&seq);
            continue;
        }

        int32_t exit_num;
        int32_t exit_denom;
        if (rat_lt_i32(exit_num_x, exit_den_x, exit_num_y, exit_den_y)) {
            exit_num   = exit_num_x;
            exit_denom = exit_den_x;
        } else {
            exit_num   = exit_num_y;
            exit_denom = exit_den_y;
        }

        if (rat_lt_i32(exit_num, exit_denom, gap_num, gap_denom)) {
            seq_next_inplace(&seq);
            continue;
        }

        /* Early reject: gap_num / gap_denom >= max_t_ms → out of window */
        if ((int64_t)gap_num >= (int64_t)max_t_ms * (int64_t)gap_denom) {
            seq_next_inplace(&seq);
            continue;
        }

        /* Compare with current best: accept if earlier, or equal time + lower id. */
        bool better;
        if (!best_set) {
            better = true;
        } else {
            /* gap_num/gap_denom < best_num/best_denom ? */
            int64_t lhs = (int64_t)gap_num  * best_denom;
            int64_t rhs = (int64_t)best_num * gap_denom;
            if (lhs < rhs) {
                better = true;
            } else if (lhs == rhs) {
                better = (obs_id < best.obstacle_id);
            } else {
                better = false;
            }
        }

        if (better) {
            best.obstacle_id  = obs_id;
            best.gap_num      = gap_num;
            best.gap_denom    = gap_denom;
            best.normal_axis  = axis;
            best.normal_sign  = sign;
            best_num          = gap_num;
            best_denom        = gap_denom;
            best_set          = true;
        }

        seq_next_inplace(&seq);
    }
    RELEASE(seq);

    return best;
}

/* ============================================================
 * Native Clojure bindings
 * ============================================================ */

/**
 * @brief fx/sweep-aabb native implementation.
 * args: [mover-map vel-map obstacles-seq max-ms]
 * Returns {:hit-id N :normal :kw} (AUTORELEASE) or NULL on no hit.
 */
ID native_fx_sweep_aabb(ID *args, unsigned int argc) {
    if (argc < 4) return NULL;
    if (!sweep_init_keywords()) return NULL;

    ID mover_map = args[0];
    ID vel_map   = args[1];
    ID obstacles = args[2];
    ID max_ms_id = args[3];

    if (!mover_map || !vel_map) return NULL;
    if (!max_ms_id || !is_fixnum((CljValue)(uintptr_t)max_ms_id)) return NULL;

    int32_t mx = map_get_int(mover_map, g_kw_x);
    int32_t my = map_get_int(mover_map, g_kw_y);
    int32_t mw = map_get_int(mover_map, g_kw_w);
    int32_t mh = map_get_int(mover_map, g_kw_h);
    int32_t vx = map_get_int(vel_map,   g_kw_vx);
    int32_t vy = map_get_int(vel_map,   g_kw_vy);
    int32_t max_t = as_fixnum((CljValue)(uintptr_t)max_ms_id);

    VgAabb mover = {.min_x = mx, .max_x = mx + mw,
                    .min_y = my, .max_y = my + mh};

    VgSweepResult r = vg_sweep_aabb(mover, vx, vy, obstacles, max_t, NULL);
    if (r.obstacle_id == -1) return NULL;

    /* Map normal_axis/sign to keyword */
    ID normal_kw;
    if (r.normal_axis == 0) {
        normal_kw = r.normal_sign < 0 ? g_kw_left : g_kw_right;
    } else {
        normal_kw = r.normal_sign < 0 ? g_kw_top : g_kw_bottom;
    }

    CljPersistentMap *result = make_map_from_kv(
        2,
        g_kw_hit_id, fixnum(r.obstacle_id),
        g_kw_normal, normal_kw);
    if (!result) return NULL;
    return AUTORELEASE((ID)result);
}

/**
 * @brief fx/interpolate-segment native implementation.
 * args: [seg-map now-ms]
 * Returns {:x N :y N} (AUTORELEASE).
 */
ID native_fx_interpolate_segment(ID *args, unsigned int argc) {
    if (argc < 2) return NULL;
    if (!sweep_init_keywords()) return NULL;

    ID seg    = args[0];
    ID now_id = args[1];

    if (!seg) return NULL;
    if (!now_id || !is_fixnum((CljValue)(uintptr_t)now_id)) return NULL;

    int32_t start_ms = map_get_int(seg, g_kw_start_ms);
    int32_t end_ms   = map_get_int(seg, g_kw_end_ms);
    int32_t from_x   = map_get_int(seg, g_kw_from_x);
    int32_t from_y   = map_get_int(seg, g_kw_from_y);
    int32_t to_x     = map_get_int(seg, g_kw_to_x);
    int32_t to_y     = map_get_int(seg, g_kw_to_y);
    int32_t now      = as_fixnum((CljValue)(uintptr_t)now_id);

    int32_t rx, ry;
    int32_t dur = end_ms - start_ms;
    if (dur <= 0 || now <= start_ms) {
        rx = from_x; ry = from_y;
    } else if (now >= end_ms) {
        rx = to_x; ry = to_y;
    } else {
        int32_t elapsed = now - start_ms;
        rx = from_x + (int32_t)(((int64_t)(to_x - from_x) * elapsed) / dur);
        ry = from_y + (int32_t)(((int64_t)(to_y - from_y) * elapsed) / dur);
    }

    CljPersistentMap *result = make_map_from_kv(
        2,
        g_kw_x, fixnum(rx),
        g_kw_y, fixnum(ry));
    if (!result) return NULL;
    return AUTORELEASE((ID)result);
}
