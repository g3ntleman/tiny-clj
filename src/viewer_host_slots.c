#include "viewer_host_slots.h"

#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "callbacks.h"
#include "exception.h"
#include "memory.h"
#include "record.h"
#include "rendered_state_snapshot.h"
#include "tiny_clj.h"
#include "tiny_fx_gfx.h"
#include "vector.h"
#include "viewer_collision_bridge.h"

#define TINYCLJ_TINY_FX_HOST_HEAP_LIMIT_BYTES 614400u

static inline uint32_t viewer_record_type_hash(ID obj) {
    if (!obj || TAG(obj) != CLJ_RECORD) {
        return 0u;
    }
    CljPersistentRecord *r = (CljPersistentRecord *)obj;
    return r->descriptor ? clj_hash(r->descriptor->type_symbol) : 0u;
}

static ID viewer_slot_desc_field(ID slot_desc, ID key) {
    if (!slot_desc || !key) {
        return NULL;
    }
    if (is_map(slot_desc)) {
        return map_get_sentinel(slot_desc, key, NULL);
    }
    if (TAG(slot_desc) == CLJ_RECORD) {
        return tiny_fx_gfx_get_field(slot_desc, key, NULL);
    }
    return NULL;
}

FrameScene *viewer_frame_scene_from_atom(CljAtom *scene_atom) {
    if (!scene_atom) {
        return NULL;
    }
    ID scene = atom_peek(scene_atom);
    if (!scene || TAG(scene) != CLJ_RECORD) {
        return NULL;
    }
    const VgRecordSchema *schema = tiny_fx_gfx_schema();
    if (!schema || viewer_record_type_hash(scene) != schema->h_frame_scene) {
        return NULL;
    }
    return (FrameScene *)scene;
}

void destroy_scene_bundle(ViewerSceneBundle *bundle) {
    if (!bundle) {
        return;
    }
    if (bundle->slots) {
        for (uint8_t i = 0; i < bundle->slot_count; i++) {
            RELEASE(bundle->slots[i].scene);
        }
    }
    CLJ_HOST_FREE(bundle->slots);
    RELEASE(bundle->slots_root);
    RELEASE(bundle->entry);
    RELEASE(bundle->spatial_callback);
    RELEASE(bundle->game_scene_atom);
    memset(bundle, 0, sizeof(*bundle));
}

static bool viewer_fail_game_demo_config(ViewerSceneBundle *bundle, const char *message) {
    if (bundle) {
        destroy_scene_bundle(bundle);
    }
    throw_exception(EXCEPTION_RUNTIME, message, __FILE__, __LINE__, 0);
    return false;
}

ViewerConfigSource viewer_selected_config_source(void) {
    const char *host_demo = getenv("TINYCLJ_HOST_DEMO");
#if defined(TINYCLJ_DEFAULT_HOST_DEMO)
    if (!host_demo || host_demo[0] == '\0') {
        host_demo = TINYCLJ_DEFAULT_HOST_DEMO;
    }
#endif
    if (!host_demo || host_demo[0] == '\0') {
        host_demo = "breakout";
    }
    if (strcmp(host_demo, "game-demo") == 0) {
        return (ViewerConfigSource){
            .namespace_name = "tiny-fx.game-demo",
            .config_expr = "(tiny-fx.game-demo/game-demo-config)",
            .display_name = "tiny-fx.game-demo/game-demo-config",
        };
    }
    return (ViewerConfigSource){
        .namespace_name = "tiny-clj.deployment",
        .config_expr = "(tiny-clj.deployment/breakout-host-config)",
        .display_name = "tiny-clj.deployment/breakout-host-config",
    };
}

size_t viewer_tiny_fx_host_heap_limit_bytes(void) {
#if defined(DEBUG) && !defined(ESP32_BUILD)
#if defined(TINYCLJ_HOST_HEAP_LIMIT_BYTES)
    return (size_t)TINYCLJ_HOST_HEAP_LIMIT_BYTES;
#else
    return (size_t)TINYCLJ_TINY_FX_HOST_HEAP_LIMIT_BYTES;
#endif
#else
    return 0u;
#endif
}

void viewer_tiny_fx_host_apply_heap_limit(void) {
    size_t host_heap_limit = viewer_tiny_fx_host_heap_limit_bytes();
    if (host_heap_limit == 0u) {
        return;
    }
    memory_set_heap_limit_bytes(host_heap_limit);
}

static bool viewer_extract_scene_slots(ID slots, ViewerSceneBundle *out_bundle) {
    if (!slots || !out_bundle || !is_vector(slots)) {
        return false;
    }
    static ID k_id = NULL;
    static ID k_atom = NULL;
    k_id = intern_symbol_global(":id");
    k_atom = intern_symbol_global(":atom");
    if (!k_id || !k_atom) {
        return false;
    }
    CljPersistentVector *vec = as_vector(slots);
    if (!vec) {
        return false;
    }
    uint32_t raw_count = vector_count(vec);
    if (raw_count == 0u || raw_count > VG_RENDERED_STATE_MAX_SLOTS) {
        return false;
    }
    ViewerConfiguredSlot *slot_items =
        (ViewerConfiguredSlot *)CLJ_HOST_CALLOC((size_t)raw_count, sizeof(ViewerConfiguredSlot));
    if (!slot_items) {
        return false;
    }
    for (uint32_t i = 0; i < raw_count; i++) {
        ID slot_desc = vector_nth(vec, i);
        ID slot_id = viewer_slot_desc_field(slot_desc, k_id);
        ID slot_atom = viewer_slot_desc_field(slot_desc, k_atom);
        if (!slot_id || !is_symbol(slot_id) || !slot_atom || TAG(slot_atom) != CLJ_ATOM) {
            CLJ_HOST_FREE(slot_items);
            return false;
        }
        for (uint32_t j = 0; j < i; j++) {
            if (slot_items[j].id == slot_id) {
                CLJ_HOST_FREE(slot_items);
                return false;
            }
        }
        FrameScene *scene = viewer_frame_scene_from_atom((CljAtom *)slot_atom);
        if (!scene) {
            CLJ_HOST_FREE(slot_items);
            return false;
        }
        slot_items[i].id = slot_id;
        slot_items[i].scene_atom = (CljAtom *)slot_atom;
        slot_items[i].scene = scene;
        RETAIN(scene);
    }
    RETAIN(slots);
    out_bundle->slots_root = slots;
    out_bundle->slots = slot_items;
    out_bundle->slot_count = (uint8_t)raw_count;
    return true;
}

bool viewer_load_game_demo_config(EvalState *st,
                                  ViewerConfigSource config_source,
                                  ViewerSceneBundle *out_bundle,
                                  ViewerSpatialRuleSet *out_rule_set) {
    if (!st || !out_bundle || !out_rule_set) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "viewer_load_game_demo_config requires eval state and output buffers",
                        __FILE__,
                        __LINE__,
                        0);
        return false;
    }
    memset(out_bundle, 0, sizeof(*out_bundle));
    memset(out_rule_set, 0, sizeof(*out_rule_set));
    if (!config_source.namespace_name || !config_source.config_expr) {
        return viewer_fail_game_demo_config(NULL, "viewer config source is incomplete");
    }
    if (!require_namespace_by_name(st, config_source.namespace_name)) {
        return false;
    }
    ID cfg = eval_string(config_source.config_expr, st);
    if (!is_map(cfg)) {
        return viewer_fail_game_demo_config(NULL, "viewer config function must return a map");
    }
    static CljSymbol *k_slots = NULL;
    static CljSymbol *k_entry = NULL;
    static CljSymbol *k_spatial_callback = NULL;
    static CljSymbol *k_game_scene_atom = NULL;
    k_slots = intern_symbol_global(":slots");
    k_entry = intern_symbol_global(":entry");
    k_spatial_callback = intern_symbol_global(":spatial-callback");
    k_game_scene_atom = intern_symbol_global(":game-scene-atom");
    if (!k_slots || !k_entry || !k_spatial_callback || !k_game_scene_atom) {
        return viewer_fail_game_demo_config(NULL, "viewer failed to intern required config keys");
    }
    ID slots = map_get_sentinel(cfg, k_slots, NULL);
    ID entry = map_get_sentinel(cfg, k_entry, NULL);
    ID spatial_callback = map_get_sentinel(cfg, k_spatial_callback, NULL);
    ID game_scene_atom = map_get_sentinel(cfg, k_game_scene_atom, NULL);
    if (!viewer_extract_scene_slots(slots, out_bundle)) {
        return viewer_fail_game_demo_config(NULL, "viewer config contains invalid :slots data");
    }
    if (!spatial_callback || !game_scene_atom || TAG(game_scene_atom) != CLJ_ATOM) {
        return viewer_fail_game_demo_config(
            out_bundle,
            "viewer config must provide function :spatial-callback and atom :game-scene-atom");
    }
    unsigned char fn_tag = TAG(spatial_callback);
    if ((fn_tag != CLJ_FUNC && fn_tag != CLJ_CLOSURE)) {
        return viewer_fail_game_demo_config(
            out_bundle,
            "viewer config :spatial-callback must be callable");
    }
    out_bundle->entry = RETAIN(entry);
    out_bundle->spatial_callback = RETAIN(spatial_callback);
    out_bundle->game_scene_atom = (CljAtom *)RETAIN(game_scene_atom);
    out_bundle->game_scene = viewer_frame_scene_from_atom(out_bundle->game_scene_atom);
    if (!out_bundle->game_scene) {
        return viewer_fail_game_demo_config(
            out_bundle,
            "viewer config :game-scene-atom must deref to a frame-scene");
    }
    for (uint8_t i = 0; i < out_bundle->slot_count; i++) {
        if (out_bundle->slots[i].scene_atom == out_bundle->game_scene_atom) {
            out_bundle->game_slot_index = i;
            out_bundle->has_game_slot = true;
            break;
        }
    }
    if (!out_bundle->has_game_slot) {
        return viewer_fail_game_demo_config(
            out_bundle,
            "viewer config must include :game-scene-atom in :slots");
    }
    out_bundle->game_scene = out_bundle->slots[out_bundle->game_slot_index].scene;
    if (!viewer_collision_load_rules_from_scene(out_bundle->game_scene, out_rule_set)) {
        return viewer_fail_game_demo_config(
            out_bundle,
            "viewer config game scene contains invalid spatial rules");
    }
    return true;
}

void viewer_sync_configured_slots(ViewerSceneBundle *bundle,
                                  ViewerSpatialRuleSet *rule_set,
                                  VgSlotChangeTracker *slot_change_tracker,
                                  bool publish_changes) {
    if (!bundle || !bundle->slots) {
        return;
    }
    viewer_collision_dispatch_state_lock();
    for (uint8_t i = 0; i < bundle->slot_count; i++) {
        FrameScene *scene = viewer_frame_scene_from_atom(bundle->slots[i].scene_atom);
        CLJ_ASSERT(scene && "configured slot atom must deref to FrameScene record");
        if (!scene || scene == bundle->slots[i].scene) {
            continue;
        }
        RETAIN(scene);
        RELEASE(bundle->slots[i].scene);
        bundle->slots[i].scene = scene;
        if (bundle->has_game_slot && i == bundle->game_slot_index) {
            bundle->game_scene = scene;
            if (rule_set) {
                (void)viewer_collision_load_rules_from_scene(scene, rule_set);
            }
        }
        if (publish_changes && slot_change_tracker) {
            (void)vg_slot_change_tracker_publish(slot_change_tracker, i, NULL);
        }
    }
    viewer_collision_dispatch_state_unlock();
}
