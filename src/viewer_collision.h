#ifndef TINY_CLJ_VIEWER_COLLISION_H
#define TINY_CLJ_VIEWER_COLLISION_H

#include <stdbool.h>
#include <stdint.h>

/* Mutable state for player-vs-obstacle collision stepping. */
typedef struct {
    bool collision_latched;
    uint32_t collision_cooldown_end_ms;
} VgCollisionState;

bool vg_collision_detect_player_vs_obstacle(int player_jump_y,
                                            int obstacle_x);

bool vg_collision_step_player_vs_obstacle(VgCollisionState *state,
                                          uint32_t now_ms,
                                          uint32_t cooldown_ms,
                                          int player_jump_y,
                                          int obstacle_x);

#endif
