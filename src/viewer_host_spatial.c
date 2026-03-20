#include "viewer_host_spatial.h"

#include <string.h>

#include "event_loop.h"
#include "memory.h"
#include "record.h"
#include "rendered_state_snapshot.h"
#include "tiny_fx_gfx.h"
#include "vector.h"
#include "viewer_collision.h"
#include "viewer_host_slots.h"

void destroy_collision_policy(ViewerCollisionPolicy *policy) {
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

void destroy_spatial_rule_set(ViewerSpatialRuleSet *rule_set) {
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
    k_prototype = intern_symbol_global(":prototype");
    if (!entity_rec || TAG(entity_rec) != CLJ_RECORD || !k_prototype) {
        return NULL;
    }
    return tiny_fx_gfx_get_field(entity_rec, k_prototype, NULL);
}

ID viewer_scene_entity_map(FrameScene *scene) {
    if (!scene) {
        return NULL;
    }
    return (scene->index && is_map(scene->index)) ? scene->index : NULL;
}

static bool viewer_selector_is_prototype(ID selector) {
    return selector && TAG(selector) == CLJ_RECORD;
}

static uint32_t viewer_collect_selector_entity_ids(ID entity_index,
                                                   ID selector,
                                                   ID *out_ids,
                                                   uint32_t max_ids) {
    if (!entity_index || !is_map(entity_index) || !selector || !out_ids || max_ids == 0u) {
        return 0u;
    }
    if (!viewer_selector_is_prototype(selector)) {
        ID entity = map_get_sentinel(entity_index, selector, NULL);
        if (!entity) {
            return 0u;
        }
        out_ids[0] = selector;
        return 1u;
    }

    CljPersistentMap *entity_map = as_map(entity_index);
    if (!entity_map) {
        return 0u;
    }
    uint32_t count = 0u;
    MAP_FOR_EACH(entity_map, entity_id, entity_rec) {
        if (count >= max_ids) {
            break;
        }
        ID entity_prototype = viewer_entity_prototype(entity_rec);
        if (vg_collision_selector_matches_entity_prototype(entity_prototype, selector)) {
            out_ids[count++] = entity_id;
        }
    }
    return count;
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

ID viewer_make_spatial_event(const ViewerSceneBundle *bundle,
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
    ID entity_index = viewer_scene_entity_map(bundle->game_scene);
    ID self_entity = entity_index ? map_get_sentinel(entity_index, policy->self_entity_id, NULL) : NULL;
    ID other_entity = entity_index ? map_get_sentinel(entity_index, policy->other_entity_id, NULL) : NULL;
    ID slot_id = policy->slot_id ? policy->slot_id : bundle->slots[bundle->game_slot_index].id;
    ID values[17] = {
        source_spatial,
        policy->rule_id,
        slot_id,
        policy->kind,
        phase,
        policy->self_entity_id,
        policy->other_entity_id,
        self_entity,
        other_entity,
        policy->rule,
        fixnum((int32_t)snapshot_gen),
        self_aabb_rec,
        other_aabb_rec,
        policy->self_prototype,
        policy->other_prototype,
        fixnum(policy->radius_px),
        policy->channel,
    };
    ID event_rec = (ID)make_record_with_descriptor_values(desc, values, 17u);
    RELEASE(self_aabb_rec);
    RELEASE(other_aabb_rec);
    return event_rec;
}

bool viewer_load_spatial_rules_from_scene(FrameScene *game_scene,
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
        ID entity_index = viewer_scene_entity_map(game_scene);
        if (!entity_index || !is_map(entity_index)) {
            destroy_spatial_rule_set(&next_rule_set);
            ok = false;
            break;
        }
        ID self_ids[VIEWER_MAX_SPATIAL_RULES] = {0};
        ID other_ids[VIEWER_MAX_SPATIAL_RULES] = {0};
        uint32_t self_count = viewer_collect_selector_entity_ids(entity_index,
                                                                 self_selector,
                                                                 self_ids,
                                                                 VIEWER_MAX_SPATIAL_RULES);
        uint32_t other_count = viewer_collect_selector_entity_ids(entity_index,
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
                ID self_rec = map_get_sentinel(entity_index, self_ids[self_i], NULL);
                ID other_rec = map_get_sentinel(entity_index, other_ids[other_i], NULL);
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

static bool viewer_invoke_collision_callback(const ViewerSceneBundle *bundle,
                                             ID event_payload) {
    if (!bundle || !bundle->spatial_callback || !event_payload) {
        return false;
    }
    return event_loop_enqueue_ingress_call(bundle->spatial_callback, event_payload);
}

bool viewer_apply_collision_step(ViewerSceneBundle *bundle,
                                 ViewerSpatialRuleSet *rule_set,
                                 VgSlotChangeTracker *slot_change_tracker,
                                 uint32_t now_ms) {
    if (!bundle || !rule_set || !bundle->game_scene || !bundle->has_game_slot) {
        return false;
    }

    (void)now_ms;
    bool any_triggered = false;
    bool needs_slot_sync = false;
    uint8_t game_slot = bundle->game_slot_index;
    typedef struct {
        ID entity_id;
        bool resolved;
        bool present;
        VgRenderedEntityState state;
    } ViewerEntityStateCacheEntry;
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
        bool invoked = viewer_invoke_collision_callback(bundle, event_payload);
        RELEASE(event_payload);
        if (!invoked) {
            continue;
        }
        any_triggered = true;
        needs_slot_sync = true;
    }
    if (needs_slot_sync) {
        /*
         * Spatial callbacks may replace the game scene and rebuild the spatial
         * rules asynchronously. Defer slot/rule synchronization until after the
         * iteration completes so we never reload or destroy rule storage while
         * this loop is still walking it.
         */
        viewer_sync_configured_slots(bundle, rule_set, slot_change_tracker, true);
    }
    return any_triggered;
}
