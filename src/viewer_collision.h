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

#endif
