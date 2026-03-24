#ifndef TINY_CLJ_FX_SPATIAL_BRIDGE_H
#define TINY_CLJ_FX_SPATIAL_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "vector_scene_graph.h"
#include "tiny_fx_gfx.h"
#include "fx_collision.h"

struct ViewerSceneBundle;

/* Must fit the concrete collision policies of the active scene. */
#define FX_MAX_SPATIAL_RULES 64u

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
    ViewerCollisionPolicy items[FX_MAX_SPATIAL_RULES];
    VgCollisionState states[FX_MAX_SPATIAL_RULES];
    uint32_t count;
    uint32_t version;
} ViewerSpatialRuleSet;

ID fx_collision_scene_entity_map(FrameScene *scene);
void destroy_collision_policy(ViewerCollisionPolicy *policy);
void destroy_spatial_rule_set(ViewerSpatialRuleSet *rule_set);
ID fx_collision_make_spatial_event(const struct ViewerSceneBundle *bundle,
                                       const ViewerCollisionPolicy *policy,
                                       ID phase,
                                       uint32_t snapshot_gen,
                                       const VgAabb *self_box,
                                       const VgAabb *other_box);
ID fx_collision_make_spatial_event_snapshot(const ViewerCollisionPolicy *policy,
                                                ID slot_id,
                                                ID phase,
                                                uint32_t snapshot_gen,
                                                ID self_entity,
                                                ID other_entity,
                                                const VgAabb *self_box,
                                                const VgAabb *other_box);
bool fx_collision_load_rules_from_scene(FrameScene *scene,
                                            ViewerSpatialRuleSet *io_rule_set);
bool fx_collision_detect_step(struct ViewerSceneBundle *bundle,
                                  ViewerSpatialRuleSet *rule_set,
                                  uint32_t now_ms,
                                  const VgClipRect *dirty_rects,
                                  size_t dirty_rect_count);

#endif /* TINY_CLJ_FX_SPATIAL_BRIDGE_H */
