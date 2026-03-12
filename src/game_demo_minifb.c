#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <sched.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#include <mach/mach_init.h>
#endif

#if defined(TINYCLJ_WITH_MINIFB)
#include "vector_scene_graph.h"
#include "scene.h"
#include "tiny_fx_gfx.h"
#include "tiny_clj.h"
#include "builtins.h"
#include "eval.h"
#include "value.h"
#include "callbacks.h"
#include "runtime.h"
#include "record.h"
#include "event_loop.h"
#include "renderer_lifecycle.h"
#include "rendered_state_snapshot.h"
#include "render_backend.h"
#include "viewer_collision.h"
#include "platform.h"
#include "gpio.h"
#include "atom.h"
#include "vector.h"
#include "MiniFB.h"
#if defined(__APPLE__)
#include "game_demo_macos_menu.h"
#endif

#define VIEW_W 320
#define VIEW_H 240
#define VIEW_DEFAULT_WINDOW_SCALE 2u
#define VIEWER_MAX_SLOTS VG_RENDERED_STATE_MAX_SLOTS
#define TARGET_FPS              60u
#define SCENE_ERASE_COLOR       0x0000u
#define RGB565_BYTES_PER_PIXEL 2u
#define VIEWER_ANIMATED_WAIT_TIMEOUT_MS 8u
#define VIEWER_MAX_SPATIAL_RULES 16u

static uint64_t monotonic_now_ns(void);
typedef struct ViewerSceneBundle ViewerSceneBundle;
typedef struct ViewerConfiguredSlot ViewerConfiguredSlot;
typedef struct ViewerCollisionPolicy ViewerCollisionPolicy;

static inline uint32_t viewer_record_type_hash(ID obj) {
    CljPersistentRecord *r = (CljPersistentRecord *)obj;
    return r->descriptor ? clj_hash(r->descriptor->type_symbol) : 0u;
}

/* Handles immediate viewer exit shortcuts. */
static bool viewer_should_exit_for_keys(const uint8_t *keys) {
    if (!keys) {
        return false;
    }
    bool esc = keys[KB_KEY_ESCAPE] != 0;
    bool cmd_q = (keys[KB_KEY_Q] != 0) &&
                 ((keys[KB_KEY_LEFT_SUPER] != 0) || (keys[KB_KEY_RIGHT_SUPER] != 0));
    return esc || cmd_q;
}


typedef struct {
    bool use_mfb_waitsync;
    bool w_key_was_down;
    bool gpio_key_was_down[10];
} ViewerRuntimeFlags;

typedef struct {
    uint32_t dirty_pixels;
    uint32_t changed_slots;
    uint_fast32_t frame_serial;
} ViewerFrameRenderResult;

static bool viewer_key_pressed_once(const uint8_t *keys, int key, bool *was_down) {
    if (!was_down) {
        return false;
    }
    bool down = keys && keys[key] != 0;
    bool pressed = down && !(*was_down);
    *was_down = down;
    return pressed;
}

static void viewer_update_runtime_flags(const uint8_t *keys,
                                        ViewerRuntimeFlags *flags,
                                        uint64_t *next_frame_deadline_ns,
                                        uint64_t target_frame_ns) {
    if (!flags || !next_frame_deadline_ns) {
        return;
    }
    if (viewer_key_pressed_once(keys, KB_KEY_W, &flags->w_key_was_down)) {
        flags->use_mfb_waitsync = !flags->use_mfb_waitsync;
        *next_frame_deadline_ns = monotonic_now_ns() + target_frame_ns;
    }
}

static bool viewer_drain_one_runloop_task(EvalState *st) {
    if (!st) {
        return false;
    }
    bool ran = false;
    TRY {
        ran = event_loop_run_next(NULL, st);
    } CATCH(ex) {
        (void)ex;
        ran = false;
    } END_TRY
    return ran;
}

static void viewer_simulate_gpio_keys(const uint8_t *keys, ViewerRuntimeFlags *flags, EvalState *st) {
    if (!flags) {
        return;
    }

    static const struct {
        int key;
        int32_t pin;
    } gpio_key_map[] = {
        {KB_KEY_1, 1}, {KB_KEY_2, 2}, {KB_KEY_3, 3}, {KB_KEY_4, 4}, {KB_KEY_5, 5},
        {KB_KEY_6, 6}, {KB_KEY_7, 7}, {KB_KEY_8, 8}, {KB_KEY_9, 9}, {KB_KEY_0, 0},
    };

    for (size_t i = 0; i < (sizeof(gpio_key_map) / sizeof(gpio_key_map[0])); i++) {
        bool down = keys && keys[gpio_key_map[i].key] != 0;
        if (down == flags->gpio_key_was_down[i]) {
            continue;
        }
        flags->gpio_key_was_down[i] = down;
        if (gpio_simulate_digital(gpio_key_map[i].pin, down ? 1 : 0)) {
            (void)viewer_drain_one_runloop_task(st);
        }
    }
}

/* Expand RGB565 framebuffer pixels to MiniFB's XRGB8888 format. */
static uint32_t rgb565_to_xrgb8888(uint16_t c) {
    uint32_t r = (uint32_t)((((c >> 11) & 0x1f) * 255) / 31);
    uint32_t g = (uint32_t)((((c >> 5) & 0x3f) * 255) / 63);
    uint32_t b = (uint32_t)(((c & 0x1f) * 255) / 31);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

typedef struct {
    double window_start_s;
    uint64_t window_frames;
    uint64_t window_dirty_pixels;
    uint64_t window_changed_slots;
    uint32_t max_dirty_px_frame;
} ViewerPerfWindow;

typedef struct {
    double fps;
    double avg_dirty_px_per_frame;
    double dirty_ratio;
    double dirty_bytes_per_s;
    double full_bytes_per_s;
    double avg_changed_slots;
    double max_dirty_bytes_per_frame;
    double avg_render_lock_hold_us;
    double max_render_lock_hold_us;
    uint64_t skipped_generations;
    uint64_t skipped_max_frame;
    uint64_t skipped_max_slot;
} ViewerPerfSnapshot;

typedef struct {
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t sum_ns;
    uint32_t count;
} TimingAccumulator;

struct ViewerConfiguredSlot {
    ID id;
    CljAtom *scene_atom;
    FrameScene *scene;
};

struct ViewerSceneBundle {
    ID slots_root;
    ViewerConfiguredSlot *slots;
    uint8_t slot_count;
    uint8_t game_slot_index;
    bool has_game_slot;
    ID spatial_callback;
    CljAtom *game_scene_atom;
    FrameScene *game_scene;
};

struct ViewerCollisionPolicy {
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
};

typedef struct {
    ViewerCollisionPolicy items[VIEWER_MAX_SPATIAL_RULES];
    VgCollisionState states[VIEWER_MAX_SPATIAL_RULES];
    uint32_t count;
} ViewerSpatialRuleSet;

typedef struct {
    ID entity_id;
    bool resolved;
    bool present;
    VgRenderedEntityState state;
} ViewerEntityStateCacheEntry;

static void destroy_scene_bundle(ViewerSceneBundle *bundle) {
    if (!bundle) {
        return;
    }
    CLJ_FREE(bundle->slots);
    RELEASE(bundle->slots_root);
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

static void destroy_collision_policy(ViewerCollisionPolicy *policy) {
    if (!policy) {
        return;
    }
    RELEASE(policy->self_entity_id);
    RELEASE(policy->other_entity_id);
    RELEASE(policy->self_prototype);
    RELEASE(policy->other_prototype);
    RELEASE(policy->slot_id);
    RELEASE(policy->rule);
    RELEASE(policy->rule_id);
    RELEASE(policy->kind);
    RELEASE(policy->channel);
    memset(policy, 0, sizeof(*policy));
}

static void destroy_spatial_rule_set(ViewerSpatialRuleSet *rule_set) {
    if (!rule_set) {
        return;
    }
    for (uint32_t i = 0; i < rule_set->count && i < VIEWER_MAX_SPATIAL_RULES; i++) {
        destroy_collision_policy(&rule_set->items[i]);
    }
    memset(rule_set, 0, sizeof(*rule_set));
}

static bool viewer_collision_policy_same_identity(const ViewerCollisionPolicy *a,
                                                  const ViewerCollisionPolicy *b) {
    if (!a || !b) {
        return false;
    }
    if (a->rule && b->rule) {
        return a->rule == b->rule &&
               a->self_entity_id == b->self_entity_id &&
               a->other_entity_id == b->other_entity_id;
    }
    if (a->rule_id && b->rule_id) {
        return a->rule_id == b->rule_id &&
               a->self_entity_id == b->self_entity_id &&
               a->other_entity_id == b->other_entity_id;
    }
    return a->self_entity_id == b->self_entity_id &&
           a->other_entity_id == b->other_entity_id &&
           a->radius_px == b->radius_px &&
           a->kind == b->kind &&
           a->channel == b->channel;
}

static ID viewer_entity_prototype(ID entity_rec) {
    static CljSymbol *k_prototype = NULL;
    if (!k_prototype) {
        k_prototype = intern_symbol_global(":prototype");
    }
    if (!entity_rec || TAG(entity_rec) != CLJ_RECORD || !k_prototype) {
        return NULL;
    }
    return tiny_fx_gfx_get_field(entity_rec, k_prototype, NULL);
}

static bool viewer_selector_is_prototype(ID selector) {
    return selector && TAG(selector) == CLJ_RECORD;
}

static uint32_t viewer_collect_selector_entity_ids(ID root,
                                                   ID selector,
                                                   ID *out_ids,
                                                   uint32_t max_ids) {
    if (!root || !is_map(root) || !selector || !out_ids || max_ids == 0u) {
        return 0u;
    }
    if (!viewer_selector_is_prototype(selector)) {
        ID entity = map_get_sentinel(root, selector, NULL);
        if (!entity) {
            return 0u;
        }
        out_ids[0] = selector;
        return 1u;
    }

    CljPersistentMap *root_map = as_map(root);
    if (!root_map) {
        return 0u;
    }
    uint32_t count = 0u;
    MAP_FOR_EACH(root_map, entity_id, entity_rec) {
        if (count >= max_ids) {
            break;
        }
        if (viewer_entity_prototype(entity_rec) == selector) {
            out_ids[count++] = entity_id;
        }
    }
    return count;
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

static FrameScene *viewer_frame_scene_from_atom(CljAtom *scene_atom) {
    if (!scene_atom) {
        return NULL;
    }
    ID scene = scene_atom->value;
    if (!scene || TAG(scene) != CLJ_RECORD) {
        return NULL;
    }
    const VgRecordSchema *schema = tiny_fx_gfx_schema();
    if (!schema || viewer_record_type_hash(scene) != schema->h_frame_scene) {
        return NULL;
    }
    return (FrameScene *)scene;
}

static bool viewer_extract_scene_slots(ID slots, ViewerSceneBundle *out_bundle) {
    if (!slots || !out_bundle || !is_vector(slots)) {
        return false;
    }
    static ID k_id = NULL;
    static ID k_atom = NULL;
    if (!k_id) {
        k_id = intern_symbol_global(":id");
        k_atom = intern_symbol_global(":atom");
    }
    if (!k_id || !k_atom) {
        return false;
    }
    CljPersistentVector *vec = as_vector(slots);
    if (!vec) {
        return false;
    }
    uint32_t raw_count = vector_count(vec);
    if (raw_count == 0u || raw_count > VIEWER_MAX_SLOTS) {
        return false;
    }
    ViewerConfiguredSlot *slot_items =
        (ViewerConfiguredSlot *)CLJ_CALLOC((size_t)raw_count, sizeof(ViewerConfiguredSlot));
    if (!slot_items) {
        return false;
    }
    for (uint32_t i = 0; i < raw_count; i++) {
        ID slot_desc = vector_nth(vec, i);
        ID slot_id = viewer_slot_desc_field(slot_desc, k_id);
        ID slot_atom = viewer_slot_desc_field(slot_desc, k_atom);
        if (!slot_id || !is_symbol(slot_id) || !slot_atom || TAG(slot_atom) != CLJ_ATOM) {
            CLJ_FREE(slot_items);
            return false;
        }
        for (uint32_t j = 0; j < i; j++) {
            if (slot_items[j].id == slot_id) {
                CLJ_FREE(slot_items);
                return false;
            }
        }
        FrameScene *scene = viewer_frame_scene_from_atom((CljAtom *)slot_atom);
        if (!scene) {
            CLJ_FREE(slot_items);
            return false;
        }
        slot_items[i].id = slot_id;
        slot_items[i].scene_atom = (CljAtom *)slot_atom;
        slot_items[i].scene = scene;
    }
    RETAIN(slots);
    out_bundle->slots_root = slots;
    out_bundle->slots = slot_items;
    out_bundle->slot_count = (uint8_t)raw_count;
    return true;
}

static ID viewer_make_aabb_record(const VgAabb *box) {
    if (!box) {
        return NULL;
    }
    static CljRecordDescriptor *desc = NULL;
    if (!desc) {
        desc = record_descriptor_lookup(intern_symbol_global("Aabb"));
    }
    if (!desc) {
        return NULL;
    }
    ID values[4] = {
        fixnum(box->min_x),
        fixnum(box->min_y),
        fixnum(box->max_x),
        fixnum(box->max_y),
    };
    return (ID)make_record_with_descriptor_values(desc, values, 4u);
}

static ID viewer_make_spatial_event(const ViewerSceneBundle *bundle,
                                    const ViewerCollisionPolicy *policy,
                                    ID phase,
                                    uint32_t snapshot_gen,
                                    const VgAabb *self_box,
                                    const VgAabb *other_box) {
    if (!bundle || !policy || !bundle->game_scene || !bundle->has_game_slot ||
        bundle->game_slot_index >= bundle->slot_count || !phase || !self_box || !other_box) {
        return NULL;
    }
    static CljRecordDescriptor *desc = NULL;
    static ID source_spatial = NULL;
    if (!desc) {
        desc = record_descriptor_lookup(intern_symbol_global("SpatialEvent"));
    }
    if (!source_spatial) {
        source_spatial = intern_symbol_global(":spatial");
    }
    if (!desc || !source_spatial) {
        return NULL;
    }
    ID self_aabb_rec = viewer_make_aabb_record(self_box);
    ID other_aabb_rec = viewer_make_aabb_record(other_box);
    if (!self_aabb_rec || !other_aabb_rec) {
        RELEASE(self_aabb_rec);
        RELEASE(other_aabb_rec);
        return NULL;
    }
    ID slot_id = policy->slot_id ? policy->slot_id : bundle->slots[bundle->game_slot_index].id;
    ID values[15] = {
        source_spatial,
        policy->rule_id,
        slot_id,
        policy->kind,
        phase,
        policy->self_entity_id,
        policy->other_entity_id,
        policy->rule,
        fixnum((int32_t)snapshot_gen),
        self_aabb_rec,
        other_aabb_rec,
        policy->self_prototype,
        policy->other_prototype,
        fixnum(policy->radius_px),
        policy->channel,
    };
    ID event_rec = (ID)make_record_with_descriptor_values(desc, values, 15u);
    RELEASE(self_aabb_rec);
    RELEASE(other_aabb_rec);
    return event_rec;
}

static bool viewer_load_spatial_rules_from_scene(FrameScene *game_scene,
                                                 ViewerSpatialRuleSet *io_rule_set) {
    if (!game_scene || !io_rule_set) {
        return false;
    }
    static CljSymbol *k_collision_rules = NULL;
    static CljSymbol *k_id = NULL;
    static CljSymbol *k_slot = NULL;
    static CljSymbol *k_kind = NULL;
    static CljSymbol *k_channel = NULL;
    static CljSymbol *k_radius = NULL;
    static CljSymbol *k_self = NULL;
    static CljSymbol *k_other = NULL;
    static CljSymbol *k_a_id = NULL;
    static CljSymbol *k_b_id = NULL;
    if (!k_collision_rules) {
        k_collision_rules = intern_symbol_global(":collision-rules");
        k_id = intern_symbol_global(":id");
        k_slot = intern_symbol_global(":slot");
        k_kind = intern_symbol_global(":kind");
        k_channel = intern_symbol_global(":channel");
        k_radius = intern_symbol_global(":radius");
        k_self = intern_symbol_global(":self");
        k_other = intern_symbol_global(":other");
        k_a_id = intern_symbol_global(":a-id");
        k_b_id = intern_symbol_global(":b-id");
    }
    if (!k_collision_rules || !k_id || !k_slot || !k_kind || !k_channel || !k_radius ||
        !k_self || !k_other || !k_a_id || !k_b_id) {
        return false;
    }
    bool ok = true;
    ID rules = tiny_fx_gfx_get_field((ID)game_scene, k_collision_rules, NULL);
    if (!rules) {
        destroy_spatial_rule_set(io_rule_set);
        return true;
    }
    if (!is_vector(rules)) {
        destroy_spatial_rule_set(io_rule_set);
        return false;
    }
    CljPersistentVector *rules_vec = as_vector(rules);
    if (!rules_vec) {
        destroy_spatial_rule_set(io_rule_set);
        return false;
    }
    ViewerSpatialRuleSet next_rule_set = {0};
    uint32_t rule_count = vector_count(rules_vec);
    if (rule_count > VIEWER_MAX_SPATIAL_RULES) {
        rule_count = VIEWER_MAX_SPATIAL_RULES;
    }
    for (uint32_t i = 0; i < rule_count && ok; i++) {
        ID rule = vector_nth(rules_vec, i);
        if (!rule || TAG(rule) != CLJ_RECORD) {
            destroy_spatial_rule_set(&next_rule_set);
            ok = false;
            break;
        }
        ID slot_obj = tiny_fx_gfx_get_field(rule, k_slot, NULL);
        ID id_obj = tiny_fx_gfx_get_field(rule, k_id, NULL);
        ID kind_obj = tiny_fx_gfx_get_field(rule, k_kind, intern_symbol_global(":collision"));
        ID channel_obj = tiny_fx_gfx_get_field(rule, k_channel, NULL);
        ID radius_obj = tiny_fx_gfx_get_field(rule, k_radius, fixnum(0));
        ID self_selector = tiny_fx_gfx_get_field(rule, k_self, NULL);
        ID other_selector = tiny_fx_gfx_get_field(rule, k_other, NULL);
        if (!self_selector) {
            self_selector = tiny_fx_gfx_get_field(rule, k_a_id, NULL);
        }
        if (!other_selector) {
            other_selector = tiny_fx_gfx_get_field(rule, k_b_id, NULL);
        }
        if (!is_fixnum(radius_obj) || !self_selector || !other_selector) {
            destroy_spatial_rule_set(&next_rule_set);
            ok = false;
            break;
        }
        ID root = game_scene->root;
        ID self_ids[VIEWER_MAX_SPATIAL_RULES] = {0};
        ID other_ids[VIEWER_MAX_SPATIAL_RULES] = {0};
        uint32_t self_count = viewer_collect_selector_entity_ids(root,
                                                                 self_selector,
                                                                 self_ids,
                                                                 VIEWER_MAX_SPATIAL_RULES);
        uint32_t other_count = viewer_collect_selector_entity_ids(root,
                                                                  other_selector,
                                                                  other_ids,
                                                                  VIEWER_MAX_SPATIAL_RULES);
        for (uint32_t self_i = 0;
             self_i < self_count && next_rule_set.count < VIEWER_MAX_SPATIAL_RULES && ok;
             self_i++) {
            for (uint32_t other_i = 0;
                 other_i < other_count && next_rule_set.count < VIEWER_MAX_SPATIAL_RULES;
                 other_i++) {
                ViewerCollisionPolicy *dst = &next_rule_set.items[next_rule_set.count];
                ID self_rec = map_get_sentinel(root, self_ids[self_i], NULL);
                ID other_rec = map_get_sentinel(root, other_ids[other_i], NULL);
                if (!self_rec || !other_rec) {
                    continue;
                }
                dst->self_entity_id = RETAIN(self_ids[self_i]);
                dst->other_entity_id = RETAIN(other_ids[other_i]);
                dst->self_prototype = RETAIN(viewer_entity_prototype(self_rec));
                dst->other_prototype = RETAIN(viewer_entity_prototype(other_rec));
                dst->radius_px = AS_FIXNUM(radius_obj);
                dst->slot_id = RETAIN(slot_obj);
                dst->rule = RETAIN(rule);
                dst->rule_id = RETAIN(id_obj);
                dst->kind = RETAIN(kind_obj);
                dst->channel = RETAIN(channel_obj);
                for (uint32_t j = 0; j < io_rule_set->count; j++) {
                    if (viewer_collision_policy_same_identity(dst, &io_rule_set->items[j])) {
                        next_rule_set.states[next_rule_set.count] = io_rule_set->states[j];
                        break;
                    }
                }
                next_rule_set.count++;
            }
        }
    }
    if (ok) {
        destroy_spatial_rule_set(io_rule_set);
        *io_rule_set = next_rule_set;
    }
    return ok;
}

static bool viewer_load_game_demo_config(EvalState *st,
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
    if (!require_namespace_by_name(st, "tiny-fx.game-demo")) {
        return false;
    }
    ID cfg = eval_string("(tiny-fx.game-demo/game-demo-config)", st);
    if (!is_map(cfg)) {
        return viewer_fail_game_demo_config(NULL,
                                            "tiny-fx.game-demo/game-demo-config must return a map");
    }
    static CljSymbol *k_slots = NULL;
    static CljSymbol *k_spatial_callback = NULL;
    static CljSymbol *k_game_scene_atom = NULL;
    if (!k_slots) {
        k_slots = intern_symbol_global(":slots");
        k_spatial_callback = intern_symbol_global(":spatial-callback");
        k_game_scene_atom = intern_symbol_global(":game-scene-atom");
    }
    if (!k_slots || !k_spatial_callback || !k_game_scene_atom) {
        return viewer_fail_game_demo_config(NULL,
                                            "viewer failed to intern required tiny-fx.game-demo config keys");
    }
    ID slots = map_get_sentinel(cfg, k_slots, NULL);
    ID spatial_callback = map_get_sentinel(cfg, k_spatial_callback, NULL);
    ID game_scene_atom = map_get_sentinel(cfg, k_game_scene_atom, NULL);
    if (!viewer_extract_scene_slots(slots, out_bundle)) {
        return viewer_fail_game_demo_config(NULL,
                                            "tiny-fx.game-demo/game-demo-config contains invalid :slots data");
    }
    if (!spatial_callback || !game_scene_atom || TAG(game_scene_atom) != CLJ_ATOM) {
        return viewer_fail_game_demo_config(
            out_bundle,
            "tiny-fx.game-demo/game-demo-config must provide function :spatial-callback and atom :game-scene-atom");
    }
    unsigned char fn_tag = TAG(spatial_callback);
    if ((fn_tag != CLJ_FUNC && fn_tag != CLJ_CLOSURE)) {
        return viewer_fail_game_demo_config(
            out_bundle,
            "tiny-fx.game-demo/game-demo-config :spatial-callback must be callable");
    }
    out_bundle->spatial_callback = RETAIN(spatial_callback);
    out_bundle->game_scene_atom = (CljAtom *)RETAIN(game_scene_atom);
    out_bundle->game_scene = viewer_frame_scene_from_atom(out_bundle->game_scene_atom);
    if (!out_bundle->game_scene) {
        return viewer_fail_game_demo_config(
            out_bundle,
            "tiny-fx.game-demo/game-demo-config :game-scene-atom must deref to a frame-scene");
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
            "tiny-fx.game-demo/game-demo-config must include :game-scene-atom in :slots");
    }
    out_bundle->slots[out_bundle->game_slot_index].scene = out_bundle->game_scene;
    if (!viewer_load_spatial_rules_from_scene(out_bundle->game_scene, out_rule_set)) {
        return viewer_fail_game_demo_config(
            out_bundle,
            "tiny-fx.game-demo/game-demo-config game scene contains invalid spatial rules");
    }
    return true;
}

static void timing_accumulator_reset(TimingAccumulator *acc) {
    if (!acc) {
        return;
    }
    acc->min_ns = UINT64_MAX;
    acc->max_ns = 0u;
    acc->sum_ns = 0u;
    acc->count = 0u;
}

static void timing_accumulator_add(TimingAccumulator *acc, uint64_t sample_ns) {
    if (!acc) {
        return;
    }
    acc->sum_ns += sample_ns;
    acc->count++;
    if (sample_ns < acc->min_ns) {
        acc->min_ns = sample_ns;
    }
    if (sample_ns > acc->max_ns) {
        acc->max_ns = sample_ns;
    }
}

static double timing_accumulator_avg_ms(const TimingAccumulator *acc) {
    if (!acc || acc->count == 0u) {
        return 0.0;
    }
    return (double)acc->sum_ns / (double)acc->count / 1e6;
}

static double timing_accumulator_max_ms(const TimingAccumulator *acc) {
    if (!acc || acc->max_ns == 0u) {
        return 0.0;
    }
    return (double)acc->max_ns / 1e6;
}

/* Initialize rolling perf window counters for throughput estimation. */
static void perf_window_init(ViewerPerfWindow *perf, double start_s) {
    if (!perf) {
        return;
    }
    memset(perf, 0, sizeof(*perf));
    perf->window_start_s = start_s;
}

/* Accumulate dirty-area and slot-change stats for one rendered frame. */
static void perf_window_record_frame(ViewerPerfWindow *perf, uint32_t dirty_pixels, uint32_t changed_slots) {
    if (!perf) {
        return;
    }
    perf->window_frames++;
    perf->window_dirty_pixels += dirty_pixels;
    perf->window_changed_slots += changed_slots;
    if (dirty_pixels > perf->max_dirty_px_frame) {
        perf->max_dirty_px_frame = dirty_pixels;
    }
}

/* Emit one-second rolling perf snapshot and reset counters. */
static bool perf_window_take_snapshot_if_due(ViewerPerfWindow *perf,
                                              double now_s,
                                              ViewerPerfSnapshot *out_snapshot) {
    if (out_snapshot) {
        memset(out_snapshot, 0, sizeof(*out_snapshot));
    }
    if (!perf) {
        return false;
    }
    double elapsed_s = now_s - perf->window_start_s;
    if (elapsed_s < 1.0 || perf->window_frames == 0u) {
        return false;
    }
    if (out_snapshot) {
        out_snapshot->fps = (double)perf->window_frames / elapsed_s;
        out_snapshot->avg_dirty_px_per_frame = (double)perf->window_dirty_pixels / (double)perf->window_frames;
        out_snapshot->dirty_ratio = out_snapshot->avg_dirty_px_per_frame / (double)(VIEW_W * VIEW_H);
        out_snapshot->dirty_bytes_per_s =
            ((double)perf->window_dirty_pixels * (double)RGB565_BYTES_PER_PIXEL) / elapsed_s;
        out_snapshot->full_bytes_per_s = out_snapshot->fps * (double)(VIEW_W * VIEW_H * RGB565_BYTES_PER_PIXEL);
        out_snapshot->avg_changed_slots = (double)perf->window_changed_slots / (double)perf->window_frames;
        out_snapshot->max_dirty_bytes_per_frame = (double)perf->max_dirty_px_frame * (double)RGB565_BYTES_PER_PIXEL;
    }
    perf->window_start_s = now_s;
    perf->window_frames = 0u;
    perf->window_dirty_pixels = 0u;
    perf->window_changed_slots = 0u;
    perf->max_dirty_px_frame = 0u;
    return true;
}

static CljAtom **g_scene_slot_atoms = NULL;
static uint8_t *g_slot_render_priority = NULL;
static uint8_t g_viewer_slot_count = 0u;
static VgSlotChangeTracker g_slot_change_tracker;

/*
 * Two-buffer model matching ESP32 SPI/I80 hardware:
 *
 *   g_render_buffer  = MCU-local render target (private to render thread)
 *   g_gram_pixels    = display GRAM (read by UI thread for presentation)
 *
 * The render thread erases + draws into g_render_buffer. After rendering is
 * complete, the dirty region is copied to g_gram_pixels — simulating the
 * SPI/DMA transfer from MCU RAM to the display's internal GRAM.
 *
 * The UI thread only reads g_gram_pixels, so it never sees the intermediate
 * erased state of g_render_buffer. Each pixel in the GRAM transitions
 * directly from old → new, matching real SPI display behavior.
 */
static uint16_t g_render_buffer[VIEW_W * VIEW_H];
static uint16_t *g_gram_pixels = NULL;

typedef struct {
    uint16_t *gram_pixels;
    uint16_t width;
    uint16_t height;
} ViewerGramBackend;

static ViewerGramBackend g_gram_backend = {0};

static bool viewer_backend_begin_frame(void *ctx, uint32_t frame_id) {
    (void)ctx;
    (void)frame_id;
    return true;
}

static bool viewer_backend_submit_rect(void *ctx,
                                       VgBackendRect rect,
                                       const uint16_t *rgb565_pixels,
                                       uint16_t stride_px) {
    ViewerGramBackend *backend = (ViewerGramBackend *)ctx;
    if (!backend || !backend->gram_pixels || !rgb565_pixels || rect.w <= 0 || rect.h <= 0) {
        return false;
    }
    if (rect.x < 0 || rect.y < 0) {
        return false;
    }
    if ((uint16_t)(rect.x + rect.w) > backend->width || (uint16_t)(rect.y + rect.h) > backend->height) {
        return false;
    }
    for (int16_t row = 0; row < rect.h; row++) {
        size_t dst_off = (size_t)(rect.y + row) * (size_t)backend->width + (size_t)rect.x;
        size_t src_off = (size_t)row * (size_t)stride_px;
        memcpy(&backend->gram_pixels[dst_off], &rgb565_pixels[src_off], (size_t)rect.w * sizeof(uint16_t));
    }
    return true;
}

static bool viewer_backend_end_frame(void *ctx, uint32_t frame_id) {
    (void)ctx;
    (void)frame_id;
    return true;
}

static const VgBackendOps g_viewer_backend_ops = {
    .begin_frame = viewer_backend_begin_frame,
    .submit_rect = viewer_backend_submit_rect,
    .end_frame = viewer_backend_end_frame,
};
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    atomic_bool running;
    bool started;
    VgRenderSlotState *slot_states;
    uint32_t *slot_seen_generations;
    atomic_uint_fast32_t rendered_frame_serial;
    atomic_uint_fast32_t last_dirty_pixels;
    atomic_uint_fast32_t last_changed_slots;
    uint32_t *last_rendered_generation;
    atomic_uint_fast64_t render_lock_hold_ns_total;
    atomic_uint_fast64_t render_lock_hold_ns_max;
    atomic_uint_fast64_t render_lock_samples;
    atomic_uint_fast64_t skipped_generations_total;
    atomic_uint_fast64_t skipped_max_frame;
    atomic_uint_fast64_t skipped_max_slot;
    atomic_uint_fast32_t animated_slots_mask;
} ViewerRenderThread;
static ViewerRenderThread g_render_thread = {0};

static uint32_t viewer_compute_animated_slots_mask(const VgRenderSlotState *slot_states) {
    if (!slot_states || g_viewer_slot_count == 0u) {
        return 0u;
    }
    uint32_t mask = 0u;
    for (uint8_t i = 0; i < g_viewer_slot_count; i++) {
        if (slot_states[i].initialized && slot_states[i].has_animation) {
            mask |= (1u << i);
        }
    }
    return mask;
}

static void viewer_destroy_slot_runtime_buffers(void) {
    CLJ_FREE(g_render_thread.slot_states);
    CLJ_FREE(g_render_thread.slot_seen_generations);
    CLJ_FREE(g_render_thread.last_rendered_generation);
    g_render_thread.slot_states = NULL;
    g_render_thread.slot_seen_generations = NULL;
    g_render_thread.last_rendered_generation = NULL;
    CLJ_FREE(g_scene_slot_atoms);
    CLJ_FREE(g_slot_render_priority);
    g_scene_slot_atoms = NULL;
    g_slot_render_priority = NULL;
    g_viewer_slot_count = 0u;
}

static bool viewer_init_slot_runtime_buffers(const ViewerSceneBundle *bundle) {
    if (!bundle || !bundle->slots || bundle->slot_count == 0u || bundle->slot_count > VIEWER_MAX_SLOTS) {
        return false;
    }
    viewer_destroy_slot_runtime_buffers();
    g_scene_slot_atoms = (CljAtom **)CLJ_CALLOC(bundle->slot_count, sizeof(CljAtom *));
    g_slot_render_priority = (uint8_t *)CLJ_MALLOC(bundle->slot_count * sizeof(uint8_t));
    g_render_thread.slot_states =
        (VgRenderSlotState *)CLJ_CALLOC(bundle->slot_count, sizeof(VgRenderSlotState));
    g_render_thread.slot_seen_generations =
        (uint32_t *)CLJ_CALLOC(bundle->slot_count, sizeof(uint32_t));
    g_render_thread.last_rendered_generation =
        (uint32_t *)CLJ_CALLOC(bundle->slot_count, sizeof(uint32_t));
    if (!g_scene_slot_atoms || !g_slot_render_priority || !g_render_thread.slot_states ||
        !g_render_thread.slot_seen_generations || !g_render_thread.last_rendered_generation) {
        viewer_destroy_slot_runtime_buffers();
        return false;
    }
    g_viewer_slot_count = bundle->slot_count;
    for (uint8_t i = 0; i < g_viewer_slot_count; i++) {
        g_scene_slot_atoms[i] = bundle->slots[i].scene_atom;
        g_slot_render_priority[i] = i;
    }
    return true;
}

static uint64_t monotonic_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

#if defined(__APPLE__)
static mach_timebase_info_data_t g_mach_timebase = {0};

static void viewer_init_mach_timebase(void) {
    if (g_mach_timebase.denom == 0) {
        (void)mach_timebase_info(&g_mach_timebase);
    }
}

static uint64_t ns_to_mach_abs(uint64_t ns) {
    return (ns * g_mach_timebase.denom) / g_mach_timebase.numer;
}
#endif

static void viewer_set_realtime_thread_policy(void) {
#if defined(__APPLE__)
    viewer_init_mach_timebase();
    thread_time_constraint_policy_data_t policy;
    policy.period      = (uint32_t)ns_to_mach_abs(16666667u);
    policy.computation = (uint32_t)ns_to_mach_abs(2000000u);
    policy.constraint  = (uint32_t)ns_to_mach_abs(16666667u);
    policy.preemptible = true;
    (void)thread_policy_set(
        mach_thread_self(),
        THREAD_TIME_CONSTRAINT_POLICY,
        (thread_policy_t)&policy,
        THREAD_TIME_CONSTRAINT_POLICY_COUNT);
#endif
}

static void collect_render_thread_metrics(ViewerPerfSnapshot *out_snapshot) {
    if (!out_snapshot) {
        return;
    }
    uint64_t render_samples = atomic_exchange_explicit(&g_render_thread.render_lock_samples,
                                                       0u,
                                                       memory_order_acq_rel);
    uint64_t render_hold_ns = atomic_exchange_explicit(&g_render_thread.render_lock_hold_ns_total,
                                                       0u,
                                                       memory_order_acq_rel);
    uint64_t render_hold_max_ns = atomic_exchange_explicit(&g_render_thread.render_lock_hold_ns_max,
                                                           0u,
                                                           memory_order_acq_rel);
    out_snapshot->skipped_generations =
        atomic_exchange_explicit(&g_render_thread.skipped_generations_total, 0u, memory_order_acq_rel);
    out_snapshot->skipped_max_frame =
        atomic_exchange_explicit(&g_render_thread.skipped_max_frame, 0u, memory_order_acq_rel);
    out_snapshot->skipped_max_slot =
        atomic_exchange_explicit(&g_render_thread.skipped_max_slot, 0u, memory_order_acq_rel);
    out_snapshot->avg_render_lock_hold_us =
        render_samples ? ((double)render_hold_ns / (double)render_samples) / 1000.0 : 0.0;
    out_snapshot->max_render_lock_hold_us = (double)render_hold_max_ns / 1000.0;
}

static void viewer_sync_configured_slots(ViewerSceneBundle *bundle,
                                         ViewerSpatialRuleSet *rule_set,
                                         bool publish_changes) {
    if (!bundle || !bundle->slots) {
        return;
    }
    for (uint8_t i = 0; i < bundle->slot_count; i++) {
        FrameScene *scene = viewer_frame_scene_from_atom(bundle->slots[i].scene_atom);
        if (!scene || scene == bundle->slots[i].scene) {
            continue;
        }
        bundle->slots[i].scene = scene;
        if (bundle->has_game_slot && i == bundle->game_slot_index) {
            bundle->game_scene = scene;
            if (rule_set) {
                (void)viewer_load_spatial_rules_from_scene(scene, rule_set);
            }
        }
        if (publish_changes) {
            (void)vg_slot_change_tracker_publish(&g_slot_change_tracker, i, NULL);
        }
    }
}

/* Render-thread loop: changed slots render immediately; animated slots tick continuously. */
static void *viewer_render_thread_main(void *arg) {
    VgFrameBuffer *fb = (VgFrameBuffer *)arg;
    if (!fb || !g_render_thread.slot_states || !g_render_thread.slot_seen_generations ||
        !g_render_thread.last_rendered_generation || !g_scene_slot_atoms || !g_slot_render_priority ||
        g_viewer_slot_count == 0u) {
        return NULL;
    }
    viewer_set_realtime_thread_policy();
    while (atomic_load_explicit(&g_render_thread.running, memory_order_acquire)) {
        uint32_t animated_mask = (uint32_t)atomic_load_explicit(&g_render_thread.animated_slots_mask,
                                                                 memory_order_acquire);
        uint32_t wait_timeout_ms = (animated_mask == 0u) ? UINT32_MAX : VIEWER_ANIMATED_WAIT_TIMEOUT_MS;
        uint32_t slot_generations[VIEWER_MAX_SLOTS] = {0};
        uint32_t changed_mask = vg_slot_change_tracker_wait_for_changes(&g_slot_change_tracker,
                                                                        g_render_thread.slot_seen_generations,
                                                                        slot_generations,
                                                                        wait_timeout_ms);
        if (!atomic_load_explicit(&g_render_thread.running, memory_order_acquire)) {
            break;
        }
        if (changed_mask == 0u && animated_mask == 0u) {
            continue;
        }

        uint32_t frame_now_ms = platform_current_time_ms();
        uint32_t frame_dirty_pixels = 0u;
        uint32_t frame_changed_slots = 0u;
        uint64_t frame_skipped_total = 0u;
        VgClipRect frame_dirty_rects[VIEWER_MAX_SLOTS] = {0};
        uint8_t frame_dirty_rect_count = 0u;
        if (pthread_mutex_lock(&g_render_thread.mutex) != 0) {
            continue;
        }
        uint64_t lock_acquired_ns = monotonic_now_ns();
        for (size_t p = 0; p < g_viewer_slot_count; p++) {
            uint8_t i = g_slot_render_priority[p];
            bool slot_changed = (changed_mask & (1u << i)) != 0u;
            bool slot_animated_tick = !slot_changed && ((animated_mask & (1u << i)) != 0u);
            if (!slot_changed && !slot_animated_tick) {
                continue;
            }
            if (!g_scene_slot_atoms[i]) {
                continue;
            }
            ID snapshot = g_scene_slot_atoms[i]->value;
            if (!snapshot) {
                continue;
            }
            VgRenderFrameSlotResult slot_result = {0};
            vg_rendered_state_capture_begin(i, slot_generations[i], frame_now_ms);
            bool rendered = vg_render_frame_slot_record_result_at_ms(snapshot,
                                                                     &g_render_thread.slot_states[i],
                                                                     fb,
                                                                     slot_generations[i],
                                                                     frame_now_ms,
                                                                     slot_animated_tick,
                                                                     &slot_result);
            if (rendered) {
                vg_rendered_state_capture_commit();
            } else {
                vg_rendered_state_capture_discard();
            }
            if (rendered) {
                uint32_t prev_gen = g_render_thread.last_rendered_generation[i];
                uint32_t curr_gen = slot_generations[i];
                if (slot_changed && curr_gen > (prev_gen + 1u)) {
                    uint64_t skipped = (uint64_t)(curr_gen - prev_gen - 1u);
                    frame_skipped_total += skipped;
                    atomic_fetch_add_explicit(&g_render_thread.skipped_generations_total,
                                              skipped,
                                              memory_order_relaxed);
                    uint64_t slot_max = atomic_load_explicit(&g_render_thread.skipped_max_slot, memory_order_relaxed);
                    while (skipped > slot_max &&
                           !atomic_compare_exchange_weak_explicit(&g_render_thread.skipped_max_slot,
                                                                  &slot_max,
                                                                  skipped,
                                                                  memory_order_relaxed,
                                                                  memory_order_relaxed)) {
                    }
                }
                g_render_thread.last_rendered_generation[i] = curr_gen;
                frame_changed_slots++;
                frame_dirty_pixels += slot_result.dirty_pixels;
                if (frame_dirty_rect_count < g_viewer_slot_count) {
                    frame_dirty_rects[frame_dirty_rect_count++] = slot_result.dirty_rect;
                }
            }
        }
        if (frame_skipped_total > 0u) {
            uint64_t frame_max = atomic_load_explicit(&g_render_thread.skipped_max_frame, memory_order_relaxed);
            while (frame_skipped_total > frame_max &&
                   !atomic_compare_exchange_weak_explicit(&g_render_thread.skipped_max_frame,
                                                          &frame_max,
                                                          frame_skipped_total,
                                                          memory_order_relaxed,
                                                          memory_order_relaxed)) {
            }
        }
        memcpy(g_render_thread.slot_seen_generations,
               slot_generations,
               (size_t)g_viewer_slot_count * sizeof(uint32_t));
        atomic_store_explicit(&g_render_thread.animated_slots_mask,
                              viewer_compute_animated_slots_mask(g_render_thread.slot_states),
                              memory_order_release);
        atomic_store_explicit(&g_render_thread.last_dirty_pixels, frame_dirty_pixels, memory_order_relaxed);
        atomic_store_explicit(&g_render_thread.last_changed_slots, frame_changed_slots, memory_order_relaxed);
        uint64_t lock_release_ns = monotonic_now_ns();
        uint64_t hold_ns = (lock_release_ns > lock_acquired_ns)
                               ? (lock_release_ns - lock_acquired_ns)
                               : 0u;
        atomic_fetch_add_explicit(&g_render_thread.render_lock_hold_ns_total, hold_ns, memory_order_relaxed);
        atomic_fetch_add_explicit(&g_render_thread.render_lock_samples, 1u, memory_order_relaxed);
        uint64_t max_hold = atomic_load_explicit(&g_render_thread.render_lock_hold_ns_max, memory_order_relaxed);
        while (hold_ns > max_hold &&
               !atomic_compare_exchange_weak_explicit(&g_render_thread.render_lock_hold_ns_max,
                                                      &max_hold,
                                                      hold_ns,
                                                      memory_order_relaxed,
                                                      memory_order_relaxed)) {
        }
        (void)pthread_mutex_unlock(&g_render_thread.mutex);
        /*
         * Simulate SPI/I80 dirty-rect transfer into display GRAM.
         * The render buffer remains private to the render thread; only the
         * finished dirty rects are copied into the live GRAM after rendering.
         */
        if (g_gram_pixels && frame_dirty_rect_count > 0u) {
            VgBackend backend = {
                .ops = &g_viewer_backend_ops,
                .ctx = &g_gram_backend,
            };
            uint32_t frame_id = (uint32_t)atomic_load_explicit(&g_render_thread.rendered_frame_serial,
                                                               memory_order_relaxed) +
                                1u;
            if (vg_backend_begin_frame(&backend, frame_id)) {
                for (uint8_t rect_i = 0; rect_i < frame_dirty_rect_count; rect_i++) {
                    (void)vg_backend_submit_clip_rect(&backend, fb, frame_dirty_rects[rect_i]);
                }
                (void)vg_backend_end_frame(&backend, frame_id);
            }
        }
        atomic_fetch_add_explicit(&g_render_thread.rendered_frame_serial, 1u, memory_order_release);
    }
    return NULL;
}

/* Start dedicated render thread that owns slot rendering. */
static bool start_render_thread(VgFrameBuffer *fb) {
    if (!fb || !g_render_thread.slot_states || !g_render_thread.slot_seen_generations ||
        !g_render_thread.last_rendered_generation) {
        return false;
    }
    memset(g_render_thread.slot_states, 0, (size_t)g_viewer_slot_count * sizeof(VgRenderSlotState));
    memset(g_render_thread.slot_seen_generations, 0, (size_t)g_viewer_slot_count * sizeof(uint32_t));
    memset(g_render_thread.last_rendered_generation, 0, (size_t)g_viewer_slot_count * sizeof(uint32_t));
    atomic_store_explicit(&g_render_thread.rendered_frame_serial, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.last_dirty_pixels, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.last_changed_slots, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.render_lock_hold_ns_total, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.render_lock_hold_ns_max, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.render_lock_samples, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.skipped_generations_total, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.skipped_max_frame, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.skipped_max_slot, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.animated_slots_mask, 0u, memory_order_release);
    if (pthread_mutex_init(&g_render_thread.mutex, NULL) != 0) {
        return false;
    }
    atomic_store_explicit(&g_render_thread.running, true, memory_order_release);
    if (pthread_create(&g_render_thread.thread, NULL, viewer_render_thread_main, fb) != 0) {
        atomic_store_explicit(&g_render_thread.running, false, memory_order_release);
        (void)pthread_mutex_destroy(&g_render_thread.mutex);
        return false;
    }
    g_render_thread.started = true;
    return true;
}

/* Stop render thread and free synchronization primitives. */
static void stop_render_thread(void) {
    if (!g_render_thread.started) {
        return;
    }
    atomic_store_explicit(&g_render_thread.running, false, memory_order_release);
    /* Wake blocked render wait to allow clean shutdown. */
    (void)vg_slot_change_tracker_publish(&g_slot_change_tracker, 0u, NULL);
    (void)pthread_join(g_render_thread.thread, NULL);
    (void)pthread_mutex_destroy(&g_render_thread.mutex);
    g_render_thread.started = false;
}

static bool viewer_renderer_start_callback(ID slot_atoms, void *user_data) {
    (void)slot_atoms;
    VgFrameBuffer *fb = (VgFrameBuffer *)user_data;
    return start_render_thread(fb);
}

static bool viewer_renderer_stop_callback(void *user_data) {
    (void)user_data;
    stop_render_thread();
    return true;
}

/* Publish one already-built FrameScene record into one slot atom and mark it dirty. */
static void publish_frame_scene_slot_record(size_t slot_index, ID scene, uint32_t *out_generation) {
    (void)scene;
    if (slot_index >= g_viewer_slot_count || !g_scene_slot_atoms || !g_scene_slot_atoms[slot_index]) {
        return;
    }
    (void)vg_slot_change_tracker_publish(&g_slot_change_tracker, (uint8_t)slot_index, out_generation);
}

static bool viewer_invoke_collision_callback(const ViewerSceneBundle *bundle,
                                             EvalState *st,
                                             ID event_payload) {
    if (!bundle || !bundle->spatial_callback || !st || !event_payload) {
        return false;
    }
    bool success = false;
    TRY {
        if (!event_loop_enqueue_ingress_call(bundle->spatial_callback, event_payload)) {
            success = false;
        } else {
            /*
             * Dispatch callback through the event-loop API so execution happens
             * on the Clojure runloop path. Callback return values are intentionally
             * ignored by the C host bridge.
             *
             * A single run_next() is not enough here: older queued tasks can run
             * before the freshly enqueued spatial callback, which makes collision
             * enter/exit phases appear one step late and desynchronizes the latch
             * from scene mutations triggered by the callback.
             */
            while (event_loop_has_pending_tasks()) {
                if (!event_loop_run_next(NULL, st)) {
                    break;
                }
                success = true;
            }
        }
    } CATCH(ex) {
        (void)ex;
        success = false;
    } END_TRY
    return success;
}

static bool viewer_apply_collision_step(ViewerSceneBundle *bundle,
                                        ViewerSpatialRuleSet *rule_set,
                                        EvalState *st,
                                        uint32_t now_ms) {
    if (!bundle || !rule_set || !st || !bundle->game_scene || !bundle->has_game_slot) {
        return false;
    }

    (void)now_ms;
    bool any_triggered = false;
    uint8_t game_slot = bundle->game_slot_index;
    ViewerEntityStateCacheEntry state_cache[VIEWER_MAX_SPATIAL_RULES * 2u] = {0};
    uint32_t state_cache_count = 0u;
    static ID phase_enter = NULL;
    static ID phase_exit = NULL;
    if (!phase_enter) {
        phase_enter = intern_symbol_global(":enter");
        phase_exit = intern_symbol_global(":exit");
    }
    for (uint32_t i = 0; i < rule_set->count; i++) {
        ViewerCollisionPolicy *policy = &rule_set->items[i];
        VgCollisionState *state = &rule_set->states[i];
        VgRenderedEntityState self_state;
        VgRenderedEntityState other_state;
        bool found_self = false;
        bool found_other = false;
        bool have_self = false;
        bool have_other = false;
        for (uint32_t cache_i = 0; cache_i < state_cache_count; cache_i++) {
            ViewerEntityStateCacheEntry *entry = &state_cache[cache_i];
            if (entry->resolved && entry->entity_id == policy->self_entity_id) {
                found_self = true;
                have_self = entry->present;
                if (have_self) {
                    self_state = entry->state;
                }
                break;
            }
        }
        if (!found_self) {
            have_self = vg_rendered_state_query_entity(game_slot,
                                                       (uintptr_t)policy->self_entity_id,
                                                       &self_state);
            if (state_cache_count < (VIEWER_MAX_SPATIAL_RULES * 2u)) {
                ViewerEntityStateCacheEntry *entry = &state_cache[state_cache_count++];
                entry->entity_id = policy->self_entity_id;
                entry->resolved = true;
                entry->present = have_self;
                if (have_self) {
                    entry->state = self_state;
                }
            }
        }
        for (uint32_t cache_i = 0; cache_i < state_cache_count; cache_i++) {
            ViewerEntityStateCacheEntry *entry = &state_cache[cache_i];
            if (entry->resolved && entry->entity_id == policy->other_entity_id) {
                found_other = true;
                have_other = entry->present;
                if (have_other) {
                    other_state = entry->state;
                }
                break;
            }
        }
        if (!found_other) {
            have_other = vg_rendered_state_query_entity(game_slot,
                                                        (uintptr_t)policy->other_entity_id,
                                                        &other_state);
            if (state_cache_count < (VIEWER_MAX_SPATIAL_RULES * 2u)) {
                ViewerEntityStateCacheEntry *entry = &state_cache[state_cache_count++];
                entry->entity_id = policy->other_entity_id;
                entry->resolved = true;
                entry->present = have_other;
                if (have_other) {
                    entry->state = other_state;
                }
            }
        }
        if (!have_self || !have_other) {
            continue;
        }
        if (!self_state.has_world_aabb || !other_state.has_world_aabb) {
            continue;
        }
        VgAabb self_box = self_state.world_aabb;
        VgAabb other_box = other_state.world_aabb;
        VgAabb sense_box = self_box;
        sense_box.min_x -= policy->radius_px;
        sense_box.max_x += policy->radius_px;
        sense_box.min_y -= policy->radius_px;
        sense_box.max_y += policy->radius_px;
        bool inside = vg_collision_detect_aabb_overlap(&sense_box, &other_box);
        bool was_inside = state->collision_latched;
        if (inside == was_inside) {
            continue;
        }
        state->collision_latched = inside;
        ID phase = inside ? phase_enter : phase_exit;
        ID event_payload = viewer_make_spatial_event(bundle,
                                                     policy,
                                                     phase,
                                                     other_state.snapshot_generation,
                                                     &self_box,
                                                     &other_box);
        if (!event_payload) {
            continue;
        }
        bool invoked = viewer_invoke_collision_callback(bundle, st, event_payload);
        RELEASE(event_payload);
        if (!invoked) {
            continue;
        }
        viewer_sync_configured_slots(bundle, rule_set, true);
        any_triggered = true;
    }
    return any_triggered;
}

static bool viewer_wait_for_frame_pacing(struct mfb_window *window,
                                         bool use_mfb_waitsync,
                                         uint64_t target_frame_ns,
                                         uint64_t *next_frame_deadline_ns,
                                         TimingAccumulator *waitsync_stats) {
    uint64_t waitsync_begin_ns = monotonic_now_ns();
    bool waitsync_ok = true;
    if (use_mfb_waitsync) {
        waitsync_ok = mfb_wait_sync(window);
    } else {
#if defined(__APPLE__)
        uint64_t deadline_mach = mach_absolute_time() +
            ns_to_mach_abs((*next_frame_deadline_ns > waitsync_begin_ns)
                           ? (*next_frame_deadline_ns - waitsync_begin_ns)
                           : 0u);
        (void)mach_wait_until(deadline_mach);
#else
        uint64_t t = waitsync_begin_ns;
        while (t < *next_frame_deadline_ns) {
            uint64_t remaining_ns = *next_frame_deadline_ns - t;
            if (remaining_ns > 1500000u) {
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = (long)(remaining_ns - 800000u);
                (void)nanosleep(&ts, NULL);
            } else {
                sched_yield();
            }
            t = monotonic_now_ns();
        }
#endif
        uint64_t now_ns = monotonic_now_ns();
        *next_frame_deadline_ns += target_frame_ns;
        if (now_ns > *next_frame_deadline_ns + (target_frame_ns * 3u)) {
            *next_frame_deadline_ns = now_ns + target_frame_ns;
        }
    }
    uint64_t waitsync_end_ns = monotonic_now_ns();
    uint64_t waitsync_ns = (waitsync_end_ns > waitsync_begin_ns)
                               ? (waitsync_end_ns - waitsync_begin_ns)
                               : 0u;
    timing_accumulator_add(waitsync_stats, waitsync_ns);
    return waitsync_ok;
}

/*
 * Lock-free frame polling: reads atomic counters from the render thread.
 * The main thread reads fb_pixels directly (no copy), faithfully simulating
 * ESP32 SPI/I80 displays where the bus reads the live GRAM with no double-buffer.
 * Tearing is possible and accepted — same as on real hardware.
 */
static ViewerFrameRenderResult viewer_poll_render_frame(void) {
    ViewerFrameRenderResult result = {0};
    result.frame_serial = atomic_load_explicit(&g_render_thread.rendered_frame_serial, memory_order_acquire);
    result.dirty_pixels = atomic_load_explicit(&g_render_thread.last_dirty_pixels, memory_order_relaxed);
    result.changed_slots = atomic_load_explicit(&g_render_thread.last_changed_slots, memory_order_relaxed);
    return result;
}

static void viewer_expand_rgb565_to_window(const uint16_t *src, uint32_t *dst, size_t count) {
    if (!src || !dst) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        dst[i] = rgb565_to_xrgb8888(src[i]);
    }
}
#endif

int main(void) {
#if !defined(TINYCLJ_WITH_MINIFB)
    fprintf(stderr, "MiniFB support is disabled for this build.\n");
    return 1;
#else
    uint16_t fb_pixels[VIEW_W * VIEW_H];
    uint32_t window_pixels[VIEW_W * VIEW_H];
    struct mfb_window *window = NULL;
    struct mfb_timer *timer = NULL;
    bool slot_runtime_initialized = false;
    bool slot_tracker_initialized = false;
    bool render_thread_started = false;
    bool demo_bundle_initialized = false;
    ViewerSceneBundle demo_bundle = {0};
    ViewerSpatialRuleSet spatial_rules = {0};
    int exit_code = 1;
    viewer_set_realtime_thread_policy();

    g_gram_pixels = fb_pixels;
    g_gram_backend.gram_pixels = fb_pixels;
    g_gram_backend.width = VIEW_W;
    g_gram_backend.height = VIEW_H;
    memset(g_render_buffer, 0, sizeof(g_render_buffer));
    memset(fb_pixels, 0, sizeof(uint16_t) * VIEW_W * VIEW_H);
    VgFrameBuffer fb;
    if (!vg_framebuffer_init(&fb, VIEW_W, VIEW_H, g_render_buffer, VIEW_W * VIEW_H)) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    runtime_init(&g_runtime);
    event_loop_init();
    vg_rendered_state_reset_all();
    EvalState *viewer_eval_state = evalstate_new(true);
    if (!viewer_eval_state) {
        fprintf(stderr, "Failed to initialize eval state\n");
        goto cleanup;
    }
    evalstate_set_ns(viewer_eval_state, "user");
    if (!tiny_fx_gfx_ensure_schema(viewer_eval_state)) {
        fprintf(stderr, "Failed to initialize vector scene record schema via tiny-fx.gfx\n");
        goto cleanup;
    }
    TRY {
        if (!viewer_load_game_demo_config(viewer_eval_state, &demo_bundle, &spatial_rules)) {
            fprintf(stderr, "Failed to load game-demo config from tiny-fx.game-demo/game-demo-config\n");
            goto cleanup;
        }
    } CATCH(ex) {
        fprintf(stderr, "Failed to load game-demo config from tiny-fx.game-demo/game-demo-config\n");
        if (ex) {
            print_exception(ex);
        }
        goto cleanup;
    } END_TRY
    demo_bundle_initialized = true;
    if (!viewer_init_slot_runtime_buffers(&demo_bundle)) {
        fprintf(stderr, "Failed to initialize configured slot runtime\n");
        goto cleanup;
    }
    slot_runtime_initialized = true;
    if (!vg_slot_change_tracker_init(&g_slot_change_tracker, demo_bundle.slot_count)) {
        fprintf(stderr, "Failed to initialize slot change tracker\n");
        goto cleanup;
    }
    slot_tracker_initialized = true;
    tiny_renderer_lifecycle_set_callbacks(viewer_renderer_start_callback,
                                          viewer_renderer_stop_callback,
                                          &fb);
    if (!tiny_renderer_lifecycle_start(NULL)) {
        fprintf(stderr, "Failed to start render thread\n");
        goto cleanup;
    }
    render_thread_started = true;

#if defined(__APPLE__)
    macos_viewer_install_menu();
    macos_viewer_begin_performance_activity();
#endif
    const unsigned default_win_w = VIEW_W * VIEW_DEFAULT_WINDOW_SCALE;
    const unsigned default_win_h = VIEW_H * VIEW_DEFAULT_WINDOW_SCALE;
    window = mfb_open_ex("tiny-clj game demo", default_win_w, default_win_h, 0);
    if (!window) {
        fprintf(stderr, "Failed to open MiniFB window\n");
        goto cleanup;
    }
#if defined(__APPLE__)
    macos_viewer_register_window_callbacks();
    macos_viewer_restore_window_position();
#endif
    mfb_show_cursor(window, true);
    (void)mfb_set_viewport(window, 0, 0, default_win_w, default_win_h);
    timer = mfb_timer_create();
    if (!timer) {
        fprintf(stderr, "Failed to create MiniFB timer\n");
        goto cleanup;
    }
    ViewerPerfWindow perf_window;
    perf_window_init(&perf_window, mfb_timer_now(timer));

    uint_fast32_t last_presented_frame_serial = 0u;
    ViewerRuntimeFlags runtime_flags = {
        .use_mfb_waitsync = true,
        .w_key_was_down = false
    };
    const uint64_t target_frame_ns = 1000000000ull / TARGET_FPS;
    uint64_t next_frame_deadline_ns = monotonic_now_ns() + target_frame_ns;
    uint64_t last_present_ns = 0u;
    TimingAccumulator frame_dt_stats;
    TimingAccumulator waitsync_stats;
    TimingAccumulator update_stats;
    timing_accumulator_reset(&frame_dt_stats);
    timing_accumulator_reset(&waitsync_stats);
    timing_accumulator_reset(&update_stats);
    uint32_t long_frame_count = 0u;
    vg_framebuffer_clear(&fb, SCENE_ERASE_COLOR);
    memcpy(fb_pixels, g_render_buffer, sizeof(g_render_buffer));
    for (uint8_t i = 0; i < demo_bundle.slot_count; i++) {
        publish_frame_scene_slot_record(i, (ID)demo_bundle.slots[i].scene, NULL);
    }

    while (true) {
        float time_s = (float)mfb_timer_now(timer);
        if (!viewer_wait_for_frame_pacing(window,
                                          runtime_flags.use_mfb_waitsync,
                                          target_frame_ns,
                                          &next_frame_deadline_ns,
                                          &waitsync_stats)) {
            break;
        }

        const uint8_t *keys = mfb_get_key_buffer(window);
        if (viewer_should_exit_for_keys(keys)) {
            break;
        }
        viewer_update_runtime_flags(keys, &runtime_flags, &next_frame_deadline_ns, target_frame_ns);
        viewer_simulate_gpio_keys(keys, &runtime_flags, viewer_eval_state);
        viewer_sync_configured_slots(&demo_bundle, &spatial_rules, true);

        ViewerFrameRenderResult frame_result = viewer_poll_render_frame();
        (void)viewer_apply_collision_step(&demo_bundle,
                                          &spatial_rules,
                                          viewer_eval_state,
                                          platform_current_time_ms());

        viewer_expand_rgb565_to_window(fb_pixels, window_pixels, (size_t)VIEW_W * (size_t)VIEW_H);

        if (frame_result.frame_serial != last_presented_frame_serial) {
            perf_window_record_frame(&perf_window, frame_result.dirty_pixels, frame_result.changed_slots);
            last_presented_frame_serial = frame_result.frame_serial;
        }

        ViewerPerfSnapshot perf_snapshot;
        bool perf_ready = perf_window_take_snapshot_if_due(&perf_window, (double)time_s, &perf_snapshot);
        if (perf_ready) {
            collect_render_thread_metrics(&perf_snapshot);
        }
#if defined(__APPLE__)
        if (perf_ready) {
            double dt_avg_ms = timing_accumulator_avg_ms(&frame_dt_stats);
            double dt_max_ms = timing_accumulator_max_ms(&frame_dt_stats);
            double ws_avg_ms = timing_accumulator_avg_ms(&waitsync_stats);
            double up_avg_ms = timing_accumulator_avg_ms(&update_stats);
            timing_accumulator_reset(&frame_dt_stats);
            timing_accumulator_reset(&waitsync_stats);
            timing_accumulator_reset(&update_stats);
            /*
             * Keep the title intentionally short: macOS truncates long titles,
             * and skip diagnostics should stay visible even in narrow windows.
             */
            double full_frame_kb = (double)(VIEW_W * VIEW_H * RGB565_BYTES_PER_PIXEL) / 1024.0;
            double max_bw_kb = perf_snapshot.max_dirty_bytes_per_frame / 1024.0;
            char title[200];
            (void)snprintf(title,
                           sizeof(title),
                           "[%s] FPS %.1f bw %.1f/%.0fKB sk %llu/%llu lf %u dmx %.1f lk %.0fus dt %.1f up %.1f",
                           runtime_flags.use_mfb_waitsync ? "WAITSYNC" : "CUSTOM",
                           perf_snapshot.fps,
                           max_bw_kb,
                           full_frame_kb,
                           (unsigned long long)perf_snapshot.skipped_generations,
                           (unsigned long long)perf_snapshot.skipped_max_frame,
                           long_frame_count,
                           dt_max_ms,
                           perf_snapshot.max_render_lock_hold_us,
                           dt_avg_ms,
                           up_avg_ms);
            macos_viewer_set_window_title(title);
            if (perf_snapshot.skipped_generations > 0u) {
                fprintf(stderr,
                        "[viewer] skip-diag: total=%llu frame-max=%llu slot-max=%llu "
                        "lock-max=%.0fus fps=%.1f dt=%.1f ws=%.1f up=%.1f\n",
                        (unsigned long long)perf_snapshot.skipped_generations,
                        (unsigned long long)perf_snapshot.skipped_max_frame,
                        (unsigned long long)perf_snapshot.skipped_max_slot,
                        perf_snapshot.max_render_lock_hold_us,
                        perf_snapshot.fps,
                        dt_avg_ms,
                        ws_avg_ms,
                        up_avg_ms);
            }
            if (long_frame_count > 0u) {
                fprintf(stderr,
                        "[viewer] stutter-diag: long=%u dt-max=%.1fms "
                        "fps=%.1f dt=%.1f ws=%.1f up=%.1f\n",
                        long_frame_count,
                        dt_max_ms,
                        perf_snapshot.fps,
                        dt_avg_ms,
                        ws_avg_ms,
                        up_avg_ms);
            }
            long_frame_count = 0u;
        }
#else
        (void)perf_snapshot;
        (void)perf_ready;
#endif

        uint64_t now_ns = monotonic_now_ns();
        if (last_present_ns > 0u) {
            uint64_t dt = (now_ns > last_present_ns) ? (now_ns - last_present_ns) : 0u;
            timing_accumulator_add(&frame_dt_stats, dt);
            if (dt > 20000000ull) {
                long_frame_count++;
            }
        }
        last_present_ns = now_ns;

        uint64_t update_begin_ns = monotonic_now_ns();
        mfb_update_state state = mfb_update_ex(window, window_pixels, VIEW_W, VIEW_H);
        uint64_t update_end_ns = monotonic_now_ns();
        uint64_t update_ns = (update_end_ns > update_begin_ns)
                                 ? (update_end_ns - update_begin_ns)
                                 : 0u;
        timing_accumulator_add(&update_stats, update_ns);
        if (state != STATE_OK) {
            break;
        }
    }

    exit_code = 0;

cleanup:
    if (timer) {
        mfb_timer_destroy(timer);
    }
#if defined(__APPLE__)
    if (window) {
        macos_viewer_save_window_position();
    }
    macos_viewer_end_performance_activity();
#endif
    if (window) {
        mfb_close(window);
    }
    if (render_thread_started) {
        (void)tiny_renderer_lifecycle_stop();
    }
    tiny_renderer_lifecycle_set_callbacks(NULL, NULL, NULL);
    if (slot_runtime_initialized) {
        viewer_destroy_slot_runtime_buffers();
    }
    if (slot_tracker_initialized) {
        vg_slot_change_tracker_destroy(&g_slot_change_tracker);
    }
    if (demo_bundle_initialized) {
        destroy_scene_bundle(&demo_bundle);
    }
    destroy_spatial_rule_set(&spatial_rules);
    g_gram_pixels = NULL;
    memset(&g_gram_backend, 0, sizeof(g_gram_backend));
    runtime_reset(&g_runtime);
    return exit_code;
#endif
}
