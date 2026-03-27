/**
 * Tests for the predictive CCD sweep-AABB subsystem.
 *
 * Phase 1 (test_sweep_p1_*): C-level vg_sweep_aabb primitive.
 * Phase 2 (test_sweep_p2_*): eval-level fx/sweep-aabb and fx/interpolate-segment.
 */
#include "tests_common.h"
#include "../fx_collision.h"
#include "../vector_scene_graph.h"
#include "map.h"

/* ============================================================
 * Helpers: build one obstacle map.
 *
 * Returns rc=1 (owned) per the make_* convention. Callers must
 * RELEASE() or AUTORELEASE() the result.
 *
 * intern_symbol_global is setup-only: allocates and interns a new
 * symbol on first call (malloc + symbol-table insert). Subsequent
 * calls return the cached singleton. Never call it on a hot path /
 * per-frame / per-entity.
 * ============================================================ */
static ID make_obs(int32_t id, int32_t x, int32_t y, int32_t w, int32_t h) {
    return (ID)make_map_from_kv(5,
        intern_symbol_global(":id"), fixnum(id),
        intern_symbol_global(":x"),  fixnum(x),
        intern_symbol_global(":y"),  fixnum(y),
        intern_symbol_global(":w"),  fixnum(w),
        intern_symbol_global(":h"),  fixnum(h));
}

/* Build a Clojure persistent vector containing one obstacle map.
 * Returns rc=1 (owned). Caller must RELEASE() or AUTORELEASE(). */
static ID make_obs_vec1(ID obs) {
    CljPersistentVector *v = make_vector(1, STRONG);
    if (!v) return NULL;
    vector_conj_inplace(&v, obs);
    return (ID)v;
}

/* Build a Clojure persistent vector containing two obstacle maps.
 * Returns rc=1 (owned). Caller must RELEASE() or AUTORELEASE(). */
static ID make_obs_vec2(ID obs_a, ID obs_b) {
    CljPersistentVector *v = make_vector(2, STRONG);
    if (!v) return NULL;
    vector_conj_inplace(&v, obs_a);
    vector_conj_inplace(&v, obs_b);
    return (ID)v;
}

/* ============================================================
 * Phase 1 – C-level vg_sweep_aabb
 * ============================================================ */

/*
 * test_sweep_p1_no_hit_moving_away
 *
 * Mover {0,8,46,54} moves LEFT (vx=-3, vy=0).
 * Single obstacle at x=30, y=46, w=16, h=16  →  right of mover.
 * The mover moves away from the obstacle → no hit expected.
 */
TEST(test_sweep_p1_no_hit_moving_away) {
    VgAabb mover = {.min_x=0, .max_x=8, .min_y=46, .max_y=54};
    ID obs  = AUTORELEASE(make_obs(1, 30, 46, 16, 16));
    ID seq  = AUTORELEASE(make_obs_vec1(obs));

    VgSweepResult r = vg_sweep_aabb(mover, -3, 0, seq, 1000, NULL);
    TEST_ASSERT_EQUAL_INT(-1, r.obstacle_id);
}

/*
 * test_sweep_p1_simple_hit
 *
 * Mover {0,8,46,54} moves RIGHT (vx=3, vy=0).
 * Obstacle at x=30, y=46, w=16, h=16.
 *
 *   gap_x = obs_min_x - mover_max_x = 30 - 8 = 22
 *   entry_num  = 22, entry_denom = 3, normal_axis = 0
 *
 * Expected: obstacle_id=1, gap_num=22, gap_denom=3, normal_axis=0.
 */
TEST(test_sweep_p1_simple_hit) {
    VgAabb mover = {.min_x=0, .max_x=8, .min_y=46, .max_y=54};
    ID obs  = AUTORELEASE(make_obs(1, 30, 46, 16, 16));
    ID seq  = AUTORELEASE(make_obs_vec1(obs));

    VgSweepResult r = vg_sweep_aabb(mover, 3, 0, seq, 1000, NULL);
    TEST_ASSERT_EQUAL_INT(1,  r.obstacle_id);
    TEST_ASSERT_EQUAL_INT(22, r.gap_num);
    TEST_ASSERT_EQUAL_INT(3,  r.gap_denom);
    TEST_ASSERT_EQUAL_INT(0,  r.normal_axis);
}

/*
 * test_sweep_p1_diagonal_hit_allows_future_cross_axis_overlap
 *
 * Mover {0,4,20,24} moves diagonally RIGHT+DOWN (vx=3, vy=2).
 * Obstacle at x=30, y=40, w=16, h=16.
 *
 * Entry times:
 *   tx = (30 - 4) / 3 = 26/3
 *   ty = (40 - 24) / 2 = 16/2 = 8
 *
 * The mover does not overlap the obstacle on Y at t=0, but it does by the
 * time the X entry occurs. A correct swept test must still report the hit.
 */
TEST(test_sweep_p1_diagonal_hit_allows_future_cross_axis_overlap) {
    VgAabb mover = {.min_x=0, .max_x=4, .min_y=20, .max_y=24};
    ID obs = AUTORELEASE(make_obs(1, 30, 40, 16, 16));
    ID seq = AUTORELEASE(make_obs_vec1(obs));

    VgSweepResult r = vg_sweep_aabb(mover, 3, 2, seq, 1000, NULL);
    TEST_ASSERT_EQUAL_INT(1,  r.obstacle_id);
    TEST_ASSERT_EQUAL_INT(26, r.gap_num);
    TEST_ASSERT_EQUAL_INT(3,  r.gap_denom);
    TEST_ASSERT_EQUAL_INT(0,  r.normal_axis);
}

/*
 * test_sweep_p1_earliest_obstacle_wins
 *
 * Two obstacles at different x positions; the closer one should be returned.
 */
TEST(test_sweep_p1_earliest_obstacle_wins) {
    VgAabb mover = {.min_x=0, .max_x=8, .min_y=46, .max_y=54};
    ID obs_near = AUTORELEASE(make_obs(1, 30, 46, 16, 16)); /* gap=22 */
    ID obs_far  = AUTORELEASE(make_obs(2, 60, 46, 16, 16)); /* gap=52 */
    ID seq = AUTORELEASE(make_obs_vec2(obs_far, obs_near)); /* far first to stress ordering */

    VgSweepResult r = vg_sweep_aabb(mover, 3, 0, seq, 1000, NULL);
    TEST_ASSERT_EQUAL_INT(1,  r.obstacle_id); /* near obstacle wins */
    TEST_ASSERT_EQUAL_INT(22, r.gap_num);
    TEST_ASSERT_EQUAL_INT(3,  r.gap_denom);
}

/*
 * test_sweep_p1_tie_lower_id_wins
 *
 * Two equidistant obstacles; lower :id should win.
 */
TEST(test_sweep_p1_tie_lower_id_wins) {
    VgAabb mover = {.min_x=0, .max_x=8, .min_y=46, .max_y=54};
    /* Both at same x=30, but different y ranges within mover's y span */
    ID obs_hi = AUTORELEASE(make_obs(5, 30, 46, 16, 16)); /* id=5 */
    ID obs_lo = AUTORELEASE(make_obs(2, 30, 46, 16, 16)); /* id=2 */
    ID seq = AUTORELEASE(make_obs_vec2(obs_hi, obs_lo)); /* higher id first */

    VgSweepResult r = vg_sweep_aabb(mover, 3, 0, seq, 1000, NULL);
    TEST_ASSERT_EQUAL_INT(2, r.obstacle_id); /* lower id wins */
    TEST_ASSERT_EQUAL_INT(22, r.gap_num);
}

/*
 * test_sweep_p1_beyond_max_t_no_hit
 *
 * Obstacle is reachable but outside the max_t_ms time window.
 * vx=3, gap=22:  time_ms = 22/3 ≈ 7.3 ms  >  max_t_ms=5.
 * max_gap_x = 5*3 = 15 < 22 → early reject.
 */
TEST(test_sweep_p1_beyond_max_t_no_hit) {
    VgAabb mover = {.min_x=0, .max_x=8, .min_y=46, .max_y=54};
    ID obs = AUTORELEASE(make_obs(1, 30, 46, 16, 16));
    ID seq = AUTORELEASE(make_obs_vec1(obs));

    VgSweepResult r = vg_sweep_aabb(mover, 3, 0, seq, 5 /* ms */, NULL);
    TEST_ASSERT_EQUAL_INT(-1, r.obstacle_id);
}

/*
 * test_sweep_p1_null_seq_no_hit
 *
 * NULL/nil obstacles seq returns no hit cleanly.
 */
TEST(test_sweep_p1_null_seq_no_hit) {
    VgAabb mover = {.min_x=0, .max_x=8, .min_y=0, .max_y=8};
    VgSweepResult r = vg_sweep_aabb(mover, 3, 0, NULL, 1000, NULL);
    TEST_ASSERT_EQUAL_INT(-1, r.obstacle_id);
}

/*
 * test_sweep_p1_no_y_overlap_no_hit
 *
 * Obstacle is to the right but completely above/below mover → no y-overlap.
 */
TEST(test_sweep_p1_no_y_overlap_no_hit) {
    VgAabb mover = {.min_x=0, .max_x=8, .min_y=0, .max_y=8};
    ID obs = AUTORELEASE(make_obs(1, 30, 100, 16, 16)); /* y=100 far from mover y=0..8 */
    ID seq = AUTORELEASE(make_obs_vec1(obs));

    VgSweepResult r = vg_sweep_aabb(mover, 3, 0, seq, 1000, NULL);
    TEST_ASSERT_EQUAL_INT(-1, r.obstacle_id);
}

/*
 * test_sweep_p1_vertical_gap_edge_touch_is_no_hit
 *
 * The mover travels straight up through a 4 px gap between two 26 px bricks.
 * Its x-range [66,70) only touches the neighboring brick edges at x=66 and x=70,
 * but does not overlap either brick interior. This must stay a no-hit so breakout
 * can thread a ball-sized gap without deleting an adjacent brick.
 */
TEST(test_sweep_p1_vertical_gap_edge_touch_is_no_hit) {
    VgAabb mover = {.min_x=66, .max_x=70, .min_y=80, .max_y=84};
    ID obs_left  = AUTORELEASE(make_obs(2001, 40, 40, 26, 10));
    ID obs_right = AUTORELEASE(make_obs(2002, 70, 40, 26, 10));
    ID seq = AUTORELEASE(make_obs_vec2(obs_left, obs_right));

    VgSweepResult r = vg_sweep_aabb(mover, 0, -2, seq, 1000, NULL);
    TEST_ASSERT_EQUAL_INT(-1, r.obstacle_id);
}

/* ============================================================
 * Phase 2 – eval-level fx/sweep-aabb and fx/interpolate-segment
 * ============================================================ */

/*
 * test_sweep_p2_no_hit_returns_nil
 *
 * Moving away from obstacle → (fx/sweep-aabb ...) → nil.
 */
TEST_SHARED(test_sweep_p2_no_hit_returns_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    eval_string("(require 'fx)", g_test_eval_state);
    ID result = eval_string(
        "(fx/sweep-aabb {:x 0 :y 46 :w 8 :h 8}"
        "               {:vx -3 :vy 0}"
        "               [{:id 1 :x 30 :y 46 :w 16 :h 16}]"
        "               1000)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(NULL, result);
}

/*
 * test_sweep_p2_hit_returns_record
 *
 * Moving right into an obstacle → SweepHit record with expected fields.
 *   hit-id  = 1
 *   time-ms = 22/3 = 7  (integer division)
 *   normal  = :left  (mover hits left face of obstacle when moving right)
 */
TEST_SHARED(test_sweep_p2_hit_returns_record) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    eval_string("(require 'fx)", g_test_eval_state);

    /* verify hit-id */
    ID hit_id = eval_string(
        "(:hit-id (fx/sweep-aabb {:x 0 :y 46 :w 8 :h 8}"
        "                        {:vx 3 :vy 0}"
        "                        [{:id 1 :x 30 :y 46 :w 16 :h 16}]"
        "                        1000))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum(hit_id));
    TEST_ASSERT_EQUAL_INT(1, (int)as_fixnum(hit_id));

    /* verify normal */
    ID normal = eval_string(
        "(:normal (fx/sweep-aabb {:x 0 :y 46 :w 8 :h 8}"
        "                        {:vx 3 :vy 0}"
        "                        [{:id 1 :x 30 :y 46 :w 16 :h 16}]"
        "                        1000))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(normal);
    TEST_ASSERT_TRUE(is_keyword(normal));
    CljSymbol *kw = (CljSymbol*)normal;
    TEST_ASSERT_EQUAL_STRING(":left", kw->cname);
}

TEST_SHARED(test_sweep_p2_vertical_gap_edge_touch_returns_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    eval_string("(require 'fx)", g_test_eval_state);
    ID result = eval_string(
        "(fx/sweep-aabb {:x 66 :y 80 :w 4 :h 4}"
        "               {:vx 0 :vy -2}"
        "               [{:id 2001 :x 40 :y 40 :w 26 :h 10}"
        "                {:id 2002 :x 70 :y 40 :w 26 :h 10}]"
        "               1000)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(NULL, result);
}

/*
 * test_sweep_p2_interpolate_segment_start
 *
 * At t=start-ms the result should equal from-{x,y}.
 */
TEST_SHARED(test_sweep_p2_interpolate_segment_start) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    eval_string("(require 'fx)", g_test_eval_state);
    ID x = eval_string(
        "(:x (fx/interpolate-segment"
        "     {:start-ms 100 :end-ms 200 :from-x 10 :from-y 20 :to-x 50 :to-y 80}"
        "     100))",
        g_test_eval_state);
    ID y = eval_string(
        "(:y (fx/interpolate-segment"
        "     {:start-ms 100 :end-ms 200 :from-x 10 :from-y 20 :to-x 50 :to-y 80}"
        "     100))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum(x));
    TEST_ASSERT_TRUE(is_fixnum(y));
    TEST_ASSERT_EQUAL_INT(10, (int)as_fixnum(x));
    TEST_ASSERT_EQUAL_INT(20, (int)as_fixnum(y));
}

/*
 * test_sweep_p2_interpolate_segment_end
 *
 * At t=end-ms the result should equal to-{x,y}.
 */
TEST_SHARED(test_sweep_p2_interpolate_segment_end) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    eval_string("(require 'fx)", g_test_eval_state);
    ID x = eval_string(
        "(:x (fx/interpolate-segment"
        "     {:start-ms 100 :end-ms 200 :from-x 10 :from-y 20 :to-x 50 :to-y 80}"
        "     200))",
        g_test_eval_state);
    ID y = eval_string(
        "(:y (fx/interpolate-segment"
        "     {:start-ms 100 :end-ms 200 :from-x 10 :from-y 20 :to-x 50 :to-y 80}"
        "     200))",
        g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum(x));
    TEST_ASSERT_TRUE(is_fixnum(y));
    TEST_ASSERT_EQUAL_INT(50, (int)as_fixnum(x));
    TEST_ASSERT_EQUAL_INT(80, (int)as_fixnum(y));
}
