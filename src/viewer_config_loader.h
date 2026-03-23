#ifndef TINY_CLJ_VIEWER_CONFIG_LOADER_H
#define TINY_CLJ_VIEWER_CONFIG_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"
#include "namespace.h"
#include "scene.h"
#include "tiny_fx_gfx.h"

struct ViewerSpatialRuleSet;

typedef struct {
    const char *namespace_name;
    const char *config_expr;
    const char *display_name;
} ViewerConfigSource;

typedef struct {
    ID id;
    CljAtom *scene_atom;
    FrameScene *scene;
} ViewerConfiguredSlot;

typedef struct ViewerSceneBundle {
    ID slots_root;
    ViewerConfiguredSlot *slots;
    uint8_t slot_count;
    uint8_t primary_slot_index;
    bool has_primary_slot;
    ID entry;
    ID startup_callback;
    ID spatial_callback;
    CljAtom *primary_scene_atom;
    FrameScene *primary_scene;
} ViewerSceneBundle;

FrameScene *viewer_frame_scene_from_atom(CljAtom *scene_atom);
void destroy_scene_bundle(ViewerSceneBundle *bundle);
ViewerConfigSource viewer_default_config_source(void);
size_t tiny_fx_host_heap_limit_bytes(void);
void tiny_fx_host_apply_heap_limit(void);
bool viewer_load_deployment_config(EvalState *st,
                                   ViewerConfigSource config_source,
                                   ViewerSceneBundle *out_bundle,
                                   struct ViewerSpatialRuleSet *out_rule_set);
void viewer_sync_configured_slots(ViewerSceneBundle *bundle,
                                  struct ViewerSpatialRuleSet *rule_set,
                                  VgSlotChangeTracker *slot_change_tracker,
                                  bool publish_changes);

#endif /* TINY_CLJ_VIEWER_CONFIG_LOADER_H */
