#ifndef TINY_CLJ_VIEWER_HOST_SPATIAL_H
#define TINY_CLJ_VIEWER_HOST_SPATIAL_H

#include <stdbool.h>
#include <stdint.h>

#include "scene.h"
#include "tiny_fx_gfx.h"
#include "viewer_collision.h"

struct ViewerSceneBundle;

#define VIEWER_MAX_SPATIAL_RULES 128u

typedef struct ViewerCollisionPolicy {
    ID self_entity_id;
    ID other_entity_id;
    ID self_prototype;
    ID other_prototype;
    int radius_px;
    ID slot_id;
    ID rule;
    ID rule_id;
    ID kind;
    ID channel;
} ViewerCollisionPolicy;

typedef struct ViewerSpatialRuleSet {
    ViewerCollisionPolicy items[VIEWER_MAX_SPATIAL_RULES];
    VgCollisionState states[VIEWER_MAX_SPATIAL_RULES];
    uint32_t count;
} ViewerSpatialRuleSet;

ID viewer_scene_entity_map(FrameScene *scene);
void destroy_collision_policy(ViewerCollisionPolicy *policy);
void destroy_spatial_rule_set(ViewerSpatialRuleSet *rule_set);
ID viewer_make_spatial_event(const struct ViewerSceneBundle *bundle,
                             const ViewerCollisionPolicy *policy,
                             ID phase,
                             uint32_t snapshot_gen,
                             const VgAabb *self_box,
                             const VgAabb *other_box);
bool viewer_load_spatial_rules_from_scene(FrameScene *game_scene,
                                          ViewerSpatialRuleSet *io_rule_set);
bool viewer_apply_collision_step(struct ViewerSceneBundle *bundle,
                                 ViewerSpatialRuleSet *rule_set,
                                 VgSlotChangeTracker *slot_change_tracker,
                                 uint32_t now_ms);

#endif /* TINY_CLJ_VIEWER_HOST_SPATIAL_H */
