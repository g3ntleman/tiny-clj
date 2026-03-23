#include "viewer_host_slots.h"

#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "builtins_tiny_fx_gfx.h"
#include "callbacks.h"
#include "eval.h"
#include "exception.h"
#include "memory.h"
#include "record.h"
#include "rendered_state_snapshot.h"
#include "tiny_clj.h"
#include "tiny_fx_gfx.h"
#include "vector.h"
#include "viewer_collision_bridge.h"

#define TINYCLJ_TINY_FX_HOST_HEAP_LIMIT_BYTES 614400u

static ID g_viewer_kw_id = NULL;
static ID g_viewer_kw_atom = NULL;
static ID g_viewer_kw_game = NULL;
static ID g_viewer_kw_tiny_breakout = NULL;
static ID g_viewer_kw_slots = NULL;
static ID g_viewer_kw_entry = NULL;
static ID g_viewer_kw_prepare_callback = NULL;
static ID g_viewer_kw_startup_callback = NULL;
static ID g_viewer_kw_spatial_callback = NULL;
static ID g_viewer_kw_game_scene_atom = NULL;

static const IdSymbolCacheEntry g_viewer_slot_extract_symbol_cache[] = {
    {&g_viewer_kw_id, ":id"},
    {&g_viewer_kw_atom, ":atom"},
};

static const IdSymbolCacheEntry g_viewer_breakout_symbol_cache[] = {
    {&g_viewer_kw_game, ":game"},
    {&g_viewer_kw_tiny_breakout, ":tiny-breakout"},
};

static const IdSymbolCacheEntry g_viewer_host_config_symbol_cache[] = {
    {&g_viewer_kw_slots, ":slots"},
    {&g_viewer_kw_entry, ":entry"},
    {&g_viewer_kw_prepare_callback, ":prepare-callback"},
    {&g_viewer_kw_startup_callback, ":startup-callback"},
    {&g_viewer_kw_spatial_callback, ":spatial-callback"},
    {&g_viewer_kw_game_scene_atom, ":game-scene-atom"},
};

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
    RELEASE(bundle->startup_callback);
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
    return SIZE_MAX;
#endif
}

void viewer_tiny_fx_host_apply_heap_limit(void) {
    size_t host_heap_limit = viewer_tiny_fx_host_heap_limit_bytes();
    memory_set_heap_limit_bytes(host_heap_limit);
}

static bool viewer_extract_scene_slots(ID slots, ViewerSceneBundle *out_bundle) {
    if (!slots || !out_bundle || !is_vector(slots)) {
        return false;
    }
    if (!id_symbol_cache_init_global(
            g_viewer_slot_extract_symbol_cache,
            sizeof(g_viewer_slot_extract_symbol_cache) / sizeof(g_viewer_slot_extract_symbol_cache[0]))) {
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
        ID slot_id = viewer_slot_desc_field(slot_desc, g_viewer_kw_id);
        ID slot_atom = viewer_slot_desc_field(slot_desc, g_viewer_kw_atom);
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

static bool viewer_load_breakout_host_config_fast(EvalState *st,
                                                  ViewerSceneBundle *out_bundle,
                                                  ViewerSpatialRuleSet *out_rule_set) {
    if (!st || !out_bundle || !out_rule_set) {
        return false;
    }

    bool require_ok = false;
    ID scene_atom = NULL;
    ID prepare_callback = NULL;
    ID startup_callback = NULL;
    ID spatial_callback = NULL;
    FrameScene *scene = NULL;
    ViewerConfiguredSlot *slot_items = NULL;
    bool slot_items_assigned = false;
    bool ok = false;

    WITH_AUTORELEASE_POOL({
        require_ok = require_namespace_by_name(st, "tiny-breakout.runtime");
    });
    if (!require_ok) {
        return false;
    }

    WITH_AUTORELEASE_POOL({
        scene_atom = RETAIN(eval_string("tiny-breakout.runtime/scene*", st));
        prepare_callback = RETAIN(eval_string("tiny-breakout.runtime/bootstrap-runtime!", st));
        startup_callback = RETAIN(eval_string("tiny-breakout.runtime/start-runtime!", st));
        spatial_callback = RETAIN(eval_string("tiny-breakout.runtime/on-spatial-event!", st));
    });

    if (!scene_atom || TAG(scene_atom) != CLJ_ATOM) {
        viewer_fail_game_demo_config(NULL, "breakout host config scene atom is invalid");
        goto cleanup;
    }
    if (!prepare_callback ||
        (TAG(prepare_callback) != CLJ_FUNC && TAG(prepare_callback) != CLJ_CLOSURE)) {
        viewer_fail_game_demo_config(NULL, "breakout host prepare callback must be callable");
        goto cleanup;
    }
    if (!startup_callback ||
        (TAG(startup_callback) != CLJ_FUNC && TAG(startup_callback) != CLJ_CLOSURE)) {
        viewer_fail_game_demo_config(NULL, "breakout host startup callback must be callable");
        goto cleanup;
    }
    if (!spatial_callback ||
        (TAG(spatial_callback) != CLJ_FUNC && TAG(spatial_callback) != CLJ_CLOSURE)) {
        viewer_fail_game_demo_config(NULL, "breakout host spatial callback must be callable");
        goto cleanup;
    }

    WITH_AUTORELEASE_POOL({
        (void)eval_function_call(prepare_callback, NULL, 0, NULL, st);
    });

    scene = viewer_frame_scene_from_atom((CljAtom *)scene_atom);
    if (!scene) {
        viewer_fail_game_demo_config(NULL, "breakout host scene atom must deref to a frame-scene");
        goto cleanup;
    }

    slot_items = (ViewerConfiguredSlot *)CLJ_HOST_CALLOC(1u, sizeof(ViewerConfiguredSlot));
    if (!slot_items) {
        goto cleanup;
    }

    if (!id_symbol_cache_init_global(
            g_viewer_breakout_symbol_cache,
            sizeof(g_viewer_breakout_symbol_cache) / sizeof(g_viewer_breakout_symbol_cache[0]))) {
        goto cleanup;
    }
    slot_items[0].id = g_viewer_kw_game;
    slot_items[0].scene_atom = (CljAtom *)scene_atom;
    slot_items[0].scene = scene;
    RETAIN(scene);

    out_bundle->slots = slot_items;
    slot_items_assigned = true;
    out_bundle->slot_count = 1u;
    out_bundle->game_slot_index = 0u;
    out_bundle->has_game_slot = true;
    out_bundle->entry = g_viewer_kw_tiny_breakout;
    out_bundle->startup_callback = startup_callback;
    startup_callback = NULL;
    out_bundle->spatial_callback = spatial_callback;
    spatial_callback = NULL;
    out_bundle->game_scene_atom = (CljAtom *)scene_atom;
    out_bundle->game_scene = scene;
    scene_atom = NULL;

    if (!viewer_collision_load_rules_from_scene(out_bundle->game_scene, out_rule_set)) {
        viewer_fail_game_demo_config(out_bundle, "breakout host scene contains invalid spatial rules");
        goto cleanup;
    }

    ok = true;

cleanup:
    if (!ok && out_bundle) {
        destroy_scene_bundle(out_bundle);
    }
    if (!slot_items_assigned) {
        CLJ_HOST_FREE(slot_items);
    }
    RELEASE(scene_atom);
    RELEASE(prepare_callback);
    RELEASE(startup_callback);
    RELEASE(spatial_callback);
    return ok;
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
    if (strcmp(config_source.namespace_name, "tiny-clj.deployment") == 0) {
        return viewer_load_breakout_host_config_fast(st, out_bundle, out_rule_set);
    }
    bool require_ok = false;
    WITH_AUTORELEASE_POOL({
        require_ok = require_namespace_by_name(st, config_source.namespace_name);
    });
    if (!require_ok) {
        return false;
    }
    ID cfg = NULL;
    bool ok = false;
    WITH_AUTORELEASE_POOL({
        cfg = RETAIN(eval_string(config_source.config_expr, st));
    });
    if (!is_map(cfg)) {
        goto cleanup_invalid_map;
    }
    if (!id_symbol_cache_init_global(
            g_viewer_host_config_symbol_cache,
            sizeof(g_viewer_host_config_symbol_cache) / sizeof(g_viewer_host_config_symbol_cache[0]))) {
        goto cleanup_missing_keys;
    }
    ID slots = map_get_sentinel(cfg, g_viewer_kw_slots, NULL);
    ID entry = map_get_sentinel(cfg, g_viewer_kw_entry, NULL);
    ID prepare_callback = map_get_sentinel(cfg, g_viewer_kw_prepare_callback, NULL);
    ID startup_callback = map_get_sentinel(cfg, g_viewer_kw_startup_callback, NULL);
    ID spatial_callback = map_get_sentinel(cfg, g_viewer_kw_spatial_callback, NULL);
    ID game_scene_atom = map_get_sentinel(cfg, g_viewer_kw_game_scene_atom, NULL);
    if (prepare_callback) {
        unsigned char prepare_tag = TAG(prepare_callback);
        if ((prepare_tag != CLJ_FUNC && prepare_tag != CLJ_CLOSURE)) {
            goto cleanup_invalid_prepare_callback;
        }
        WITH_AUTORELEASE_POOL({
            (void)eval_function_call(prepare_callback, NULL, 0, NULL, st);
        });
    }
    if (!viewer_extract_scene_slots(slots, out_bundle)) {
        goto cleanup_invalid_slots;
    }
    builtins_tiny_fx_gfx_register_slot_bindings(slots);
    if (!spatial_callback || !game_scene_atom || TAG(game_scene_atom) != CLJ_ATOM) {
        goto cleanup_missing_callbacks;
    }
    unsigned char fn_tag = TAG(spatial_callback);
    if ((fn_tag != CLJ_FUNC && fn_tag != CLJ_CLOSURE)) {
        goto cleanup_invalid_spatial_callback;
    }
    if (startup_callback) {
        unsigned char startup_tag = TAG(startup_callback);
        if ((startup_tag != CLJ_FUNC && startup_tag != CLJ_CLOSURE)) {
            goto cleanup_invalid_startup_callback;
        }
    }
    out_bundle->entry = RETAIN(entry);
    out_bundle->startup_callback = RETAIN(startup_callback);
    out_bundle->spatial_callback = RETAIN(spatial_callback);
    out_bundle->game_scene_atom = (CljAtom *)RETAIN(game_scene_atom);
    out_bundle->game_scene = viewer_frame_scene_from_atom(out_bundle->game_scene_atom);
    if (!out_bundle->game_scene) {
        goto cleanup_invalid_game_scene;
    }
    for (uint8_t i = 0; i < out_bundle->slot_count; i++) {
        if (out_bundle->slots[i].scene_atom == out_bundle->game_scene_atom) {
            out_bundle->game_slot_index = i;
            out_bundle->has_game_slot = true;
            break;
        }
    }
    if (!out_bundle->has_game_slot) {
        goto cleanup_missing_game_slot;
    }
    out_bundle->game_scene = out_bundle->slots[out_bundle->game_slot_index].scene;
    if (!viewer_collision_load_rules_from_scene(out_bundle->game_scene, out_rule_set)) {
        goto cleanup_invalid_rules;
    }
    ok = true;

cleanup:
    RELEASE(cfg);
    return ok;

cleanup_invalid_map:
    viewer_fail_game_demo_config(NULL, "viewer config function must return a map");
    goto cleanup;

cleanup_missing_keys:
    viewer_fail_game_demo_config(NULL, "viewer failed to intern required config keys");
    goto cleanup;

cleanup_invalid_slots:
    viewer_fail_game_demo_config(NULL, "viewer config contains invalid :slots data");
    goto cleanup;

cleanup_missing_callbacks:
    viewer_fail_game_demo_config(
        out_bundle,
        "viewer config must provide function :spatial-callback and atom :game-scene-atom");
    goto cleanup;

cleanup_invalid_spatial_callback:
    viewer_fail_game_demo_config(
        out_bundle,
        "viewer config :spatial-callback must be callable");
    goto cleanup;

cleanup_invalid_prepare_callback:
    viewer_fail_game_demo_config(
        out_bundle,
        "viewer config :prepare-callback must be callable");
    goto cleanup;

cleanup_invalid_startup_callback:
    viewer_fail_game_demo_config(
        out_bundle,
        "viewer config :startup-callback must be callable");
    goto cleanup;

cleanup_invalid_game_scene:
    viewer_fail_game_demo_config(
        out_bundle,
        "viewer config :game-scene-atom must deref to a frame-scene");
    goto cleanup;

cleanup_missing_game_slot:
    viewer_fail_game_demo_config(
        out_bundle,
        "viewer config must include :game-scene-atom in :slots");
    goto cleanup;

cleanup_invalid_rules:
    viewer_fail_game_demo_config(
        out_bundle,
        "viewer config game scene contains invalid spatial rules");
    goto cleanup;
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
