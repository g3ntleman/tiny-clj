#include "fx_collision.h"

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

bool vg_collision_selector_matches_entity_prototype(ID entity_prototype,
                                                    ID selector) {
    if (!entity_prototype || !selector) {
        return false;
    }
    return entity_prototype == selector || clj_equal(entity_prototype, selector);
}
