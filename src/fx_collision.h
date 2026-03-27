#ifndef TINY_CLJ_VIEWER_COLLISION_H
#define TINY_CLJ_VIEWER_COLLISION_H

#include <stdbool.h>
#include <stdint.h>
#include "object.h"

/* Mutable state for edge/latch tracking in higher-level collision flows. */
typedef struct {
    bool collision_latched;
} VgCollisionState;

/* Axis-aligned bounds in pixel space. */
typedef struct {
    int min_x;
    int max_x;
    int min_y;
    int max_y;
} VgAabb;

bool vg_collision_detect_aabb_overlap(const VgAabb *a,
                                      const VgAabb *b);

/* Matches prototype selectors by identity first, then structural equality. */
bool vg_collision_selector_matches_entity_prototype(ID entity_prototype,
                                                    ID selector);

/**
 * @brief Result of a predictive CCD sweep query.
 *
 * obstacle_id == -1 means no hit.
 * gap_num / gap_denom: entry time as a rational (gap_num / gap_denom ms).
 * normal_axis: 0 = x-axis hit (:left/:right), 1 = y-axis hit (:top/:bottom).
 * normal_sign: -1 mover hits left/top face, +1 mover hits right/bottom face.
 */
typedef struct {
    int32_t obstacle_id;
    int32_t gap_num;
    int32_t gap_denom;
    int     normal_axis;  /* 0=x, 1=y */
    int     normal_sign;  /* -1 or +1 */
} VgSweepResult;

/**
 * @brief Predictive swept-AABB first-hit query (C primitive).
 *
 * Iterates the Clojure seqable @p obstacles (each a map with :id :x :y :w :h),
 * computes the earliest entry time for @p mover moving at (vx, vy) px/ms, and
 * returns the first obstacle hit within @p max_t_ms.  Returns a result with
 * obstacle_id == -1 when nothing is hit.
 *
 * @param mover      Axis-aligned mover bounding box.
 * @param vx         Horizontal velocity (px/ms, segment_step_ms denominator).
 * @param vy         Vertical velocity   (px/ms, segment_step_ms denominator).
 * @param obstacles  Clojure seqable of obstacle maps, or NULL/nil for empty.
 * @param max_t_ms   Maximum time window in ms.  0 or negative → no hit.
 * @param debug_buf  Optional debug buffer (may be NULL).
 */
VgSweepResult vg_sweep_aabb(VgAabb mover,
                             int32_t vx, int32_t vy,
                             ID obstacles, int32_t max_t_ms,
                             void *debug_buf);

/**
 * @brief Native binding for fx/sweep-aabb (Clojure → C).
 *
 * args[0] mover map  {:x :y :w :h}
 * args[1] vel map    {:vx :vy}
 * args[2] obstacles  seq of maps {:id :x :y :w :h}
 * args[3] max-ms     fixnum
 *
 * Returns a map {:hit-id N :normal :kw} (AUTORELEASE),
 * or NULL (nil) on no hit.
 */
ID native_fx_sweep_aabb(ID *args, unsigned int argc);

/**
 * @brief Native binding for fx/interpolate-segment (Clojure → C).
 *
 * args[0] seg map   {:start-ms :end-ms :from-x :from-y :to-x :to-y}
 * args[1] now-ms    fixnum
 *
 * Returns {:x N :y N} (AUTORELEASE), clamped to [from,to].
 */
ID native_fx_interpolate_segment(ID *args, unsigned int argc);

#endif
