#include "viewer_collision.h"

/**
 * @brief Checks whether a wrap-safe uint32 deadline has been reached.
 *
 * @param now_ms Current monotonic time in milliseconds.
 * @param deadline_ms Absolute deadline in the same clock domain.
 * @return true when @p now_ms is at or after @p deadline_ms.
 */
static inline bool collision_deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

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
    if (a->max_x < b->min_x || a->min_x > b->max_x) {
        return false;
    }
    if (a->max_y < b->min_y || a->min_y > b->max_y) {
        return false;
    }
    return true;
}

/**
 * @brief Steps collision state with latch and cooldown gating.
 *
 * Returns true only on accepted collision-enter edges. While overlap remains
 * active, the latch suppresses retriggering until a non-overlap frame clears
 * the latch. Cooldown checks are wrap-safe for uint32 monotonic clocks.
 *
 * @param state Caller-owned mutable collision state (must not be NULL).
 * @param now_ms Current monotonic time in milliseconds.
 * @param cooldown_ms Minimum delay before the next accepted enter edge.
 * @param colliding Current overlap state from caller-provided geometry test.
 * @return true when a new collision-enter edge is accepted; otherwise false.
 * @note Not internally synchronized. Caller must serialize access to @p state.
 */
bool vg_collision_step_latched_cooldown(VgCollisionState *state,
                                        uint32_t now_ms,
                                        uint32_t cooldown_ms,
                                        bool colliding) {
    if (!state) {
        return false;
    }
    if (!colliding) {
        state->collision_latched = false;
        return false;
    }
    if (state->collision_latched) {
        return false;
    }
    if (!collision_deadline_reached(now_ms, state->collision_cooldown_end_ms)) {
        return false;
    }

    state->collision_latched = true;
    state->collision_cooldown_end_ms = now_ms + cooldown_ms;
    return true;
}
