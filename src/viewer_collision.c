#include "viewer_collision.h"

enum {
    PLAYER_MIN_X = 58,
    PLAYER_MAX_X = 86,
    PLAYER_MIN_Y_BASE = 124,
    PLAYER_MAX_Y_BASE = 146,
    OBSTACLE_MIN_X_BASE = 13,
    OBSTACLE_MAX_X_BASE = 27,
    OBSTACLE_MIN_Y = 106,
    OBSTACLE_MAX_Y = 146
};

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
 * @brief Performs a fixed AABB overlap test for player-vs-obstacle hitboxes.
 *
 * Uses integer SAT-style reject checks only. The implementation is O(1),
 * allocation-free, and uses no floating-point operations.
 *
 * @param player_jump_y Vertical player offset in pixels.
 * @param obstacle_x Horizontal obstacle offset in pixels.
 * @return true when hitboxes overlap; otherwise false.
 */
bool vg_collision_detect_player_vs_obstacle(int player_jump_y, int obstacle_x) {
    const int player_min_y = PLAYER_MIN_Y_BASE + player_jump_y;
    const int player_max_y = PLAYER_MAX_Y_BASE + player_jump_y;
    const int obstacle_min_x = OBSTACLE_MIN_X_BASE + obstacle_x;
    const int obstacle_max_x = OBSTACLE_MAX_X_BASE + obstacle_x;

    /* Fast SAT-style reject: avoid extra comparisons on non-overlap frames. */
    if (PLAYER_MAX_X < obstacle_min_x || PLAYER_MIN_X > obstacle_max_x) {
        return false;
    }
    if (player_max_y < OBSTACLE_MIN_Y || player_min_y > OBSTACLE_MAX_Y) {
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
 * @param player_jump_y Vertical player offset in pixels.
 * @param obstacle_x Horizontal obstacle offset in pixels.
 * @return true when a new collision-enter edge is accepted; otherwise false.
 * @note Not internally synchronized. Caller must serialize access to @p state.
 */
bool vg_collision_step_player_vs_obstacle(VgCollisionState *state,
                                          uint32_t now_ms,
                                          uint32_t cooldown_ms,
                                          int player_jump_y,
                                          int obstacle_x) {
    if (!state) {
        return false;
    }

    const bool colliding = vg_collision_detect_player_vs_obstacle(player_jump_y, obstacle_x);
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
