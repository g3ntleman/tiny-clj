#ifndef TINY_CLJ_VIEWER_SPATIAL_BRIDGE_H
#define TINY_CLJ_VIEWER_SPATIAL_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "vector_scene_graph.h"
#include "tiny_fx_gfx.h"
#include "viewer_collision.h"

struct ViewerSceneBundle;

/* Must fit the concrete collision policies of the active scene. */
#define VIEWER_MAX_SPATIAL_RULES 64u

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
    uint32_t version;
} ViewerSpatialRuleSet;

ID viewer_collision_scene_entity_map(FrameScene *scene);
void destroy_collision_policy(ViewerCollisionPolicy *policy);
void destroy_spatial_rule_set(ViewerSpatialRuleSet *rule_set);
void viewer_collision_dispatch_state_lock(void);
void viewer_collision_dispatch_state_unlock(void);
void viewer_collision_set_dispatch_context(struct ViewerSceneBundle *bundle,
                                          ViewerSpatialRuleSet *rule_set);
void viewer_collision_reset_dispatch_state(void);
bool viewer_collision_poll_drain(void);
ID viewer_collision_make_spatial_event(const struct ViewerSceneBundle *bundle,
                                       const ViewerCollisionPolicy *policy,
                                       ID phase,
                                       uint32_t snapshot_gen,
                                       const VgAabb *self_box,
                                       const VgAabb *other_box);
bool viewer_collision_load_rules_from_scene(FrameScene *scene,
                                            ViewerSpatialRuleSet *io_rule_set);
bool viewer_collision_detect_step(struct ViewerSceneBundle *bundle,
                                  ViewerSpatialRuleSet *rule_set,
                                  uint32_t now_ms,
                                  const VgClipRect *dirty_rects,
                                  size_t dirty_rect_count);

#endif /* TINY_CLJ_VIEWER_SPATIAL_BRIDGE_H */
