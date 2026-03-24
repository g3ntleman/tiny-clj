#include "fx_spatial_bridge.h"

#include <pthread.h>
#include <string.h>

#include "memory.h"
#include "record.h"
#include "runtime.h"
#include "tiny_fx_gfx.h"
#include "vector.h"
#include "fx_config_loader.h"

static ID g_fx_collision_kw_prototype;
static ID g_fx_collision_sym_aabb;
static ID g_fx_collision_sym_spatial_event;
static ID g_fx_collision_kw_source_spatial;
static ID g_fx_collision_kw_collision_rules;
static ID g_fx_collision_kw_id;
static ID g_fx_collision_kw_slot;
static ID g_fx_collision_kw_kind;
static ID g_fx_collision_kw_channel;
static ID g_fx_collision_kw_radius;
static ID g_fx_collision_kw_self;
static ID g_fx_collision_kw_other;
static ID g_fx_collision_kw_a_id;
static ID g_fx_collision_kw_b_id;
static ID g_fx_collision_kw_collision;

static IdSymbolCacheEntry g_fx_collision_scene_symbols[] = {
    { &g_fx_collision_kw_prototype, ":prototype" },
    { &g_fx_collision_sym_aabb, "Aabb" },
    { &g_fx_collision_sym_spatial_event, "SpatialEvent" },
    { &g_fx_collision_kw_source_spatial, ":spatial" },
    { &g_fx_collision_kw_collision_rules, ":collision-rules" },
    { &g_fx_collision_kw_id, ":id" },
    { &g_fx_collision_kw_slot, ":slot" },
    { &g_fx_collision_kw_kind, ":kind" },
    { &g_fx_collision_kw_channel, ":channel" },
    { &g_fx_collision_kw_radius, ":radius" },
    { &g_fx_collision_kw_self, ":self" },
    { &g_fx_collision_kw_other, ":other" },
    { &g_fx_collision_kw_a_id, ":a-id" },
    { &g_fx_collision_kw_b_id, ":b-id" },
    { &g_fx_collision_kw_collision, ":collision" },
};
#define VC_SCENE_SYM_COUNT (sizeof(g_fx_collision_scene_symbols) / sizeof(g_fx_collision_scene_symbols[0]))

static CljRecordDescriptor *g_fx_collision_desc_aabb = NULL;
static CljRecordDescriptor *g_fx_collision_desc_spatial_event = NULL;
static CljHashMap *g_fx_collision_desc_cache_registry = NULL;

static pthread_once_t g_fx_collision_scene_symbols_once = PTHREAD_ONCE_INIT;

static void fx_collision_scene_symbols_init_once(void) {
    for (size_t i = 0; i < VC_SCENE_SYM_COUNT; i++) {
        *g_fx_collision_scene_symbols[i].slot =
            intern_symbol_global(g_fx_collision_scene_symbols[i].cname);
    }
}

static bool fx_collision_scene_symbols_ready(void) {
    (void)pthread_once(&g_fx_collision_scene_symbols_once,
                       fx_collision_scene_symbols_init_once);
    for (size_t i = 0; i < VC_SCENE_SYM_COUNT; i++) {
        if (!*g_fx_collision_scene_symbols[i].slot) {
            return false;
        }
    }
    return true;
}

static bool fx_collision_scene_descriptors_ready(void) {
    if (!fx_collision_scene_symbols_ready()) {
        return false;
    }

    CljHashMap *registry = g_runtime.record_registry;
    if (g_fx_collision_desc_aabb &&
        g_fx_collision_desc_spatial_event &&
        registry &&
        g_fx_collision_desc_cache_registry == registry) {
        return true;
    }

    // Record descriptors are runtime-lifecycle scoped. Ensure schema is present
    // and rebind descriptor pointers whenever the registry instance changes.
    if (!tiny_fx_gfx_ensure_schema(NULL)) {
        return false;
    }
    registry = g_runtime.record_registry;
    if (!registry) {
        g_fx_collision_desc_cache_registry = NULL;
        g_fx_collision_desc_aabb = NULL;
        g_fx_collision_desc_spatial_event = NULL;
        return false;
    }

    g_fx_collision_desc_aabb = record_descriptor_lookup(g_fx_collision_sym_aabb);
    g_fx_collision_desc_spatial_event = record_descriptor_lookup(g_fx_collision_sym_spatial_event);
    if (!g_fx_collision_desc_aabb || !g_fx_collision_desc_spatial_event) {
        return false;
    }
    g_fx_collision_desc_cache_registry = registry;
    return true;
}

static bool fx_collision_policy_same_identity(const ViewerCollisionPolicy *a,
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

static ID fx_entity_prototype(ID entity_rec) {
    if (!entity_rec || TAG(entity_rec) != CLJ_RECORD || !fx_collision_scene_symbols_ready()) {
        return NULL;
    }
    return tiny_fx_gfx_get_field(entity_rec, g_fx_collision_kw_prototype, NULL);
}

static bool fx_selector_is_prototype(ID selector) {
    return selector && TAG(selector) == CLJ_RECORD;
}

static uint32_t fx_collect_selector_entity_ids(ID entity_index,
                                                   ID selector,
                                                   ID *out_ids,
                                                   uint32_t max_ids) {
    if (!entity_index || !is_map(entity_index) || !selector || !out_ids || max_ids == 0u) {
        return 0u;
    }
    if (!fx_selector_is_prototype(selector)) {
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
        ID entity_prototype = fx_entity_prototype(entity_rec);
        if (vg_collision_selector_matches_entity_prototype(entity_prototype, selector)) {
            out_ids[count++] = entity_id;
        }
    }
    return count;
}

static ID fx_make_aabb_record(const VgAabb *box) {
    if (!box) {
        return NULL;
    }
    if (!fx_collision_scene_descriptors_ready()) {
        return NULL;
    }
    ID values[4] = {
        fixnum(box->min_x),
        fixnum(box->min_y),
        fixnum(box->max_x),
        fixnum(box->max_y),
    };
    return (ID)make_record_with_descriptor_values(g_fx_collision_desc_aabb, values, 4u);
}

static uint32_t fx_next_rule_set_version(const ViewerSpatialRuleSet *rule_set) {
    uint32_t version = rule_set ? (rule_set->version + 1u) : 1u;
    if (version == 0u) {
        version = 1u;
    }
    return version;
}

/**
 * @brief Returns the entity index map for a frame scene when present.
 *
 * @param scene Frame scene whose `:index` map should be exposed.
 * @return Entity-map root or `NULL` when the scene does not publish one.
 */
ID fx_collision_scene_entity_map(FrameScene *scene) {
    if (!scene) {
        return NULL;
    }
    return (scene->index && is_map(scene->index)) ? scene->index : NULL;
}

/**
 * @brief Releases all retained fields of a collision policy.
 *
 * @param policy Policy to clear in place.
 * @return void
 */
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

/**
 * @brief Releases all policies and latch state held by a spatial rule set.
 *
 * @param rule_set Rule set to clear in place.
 * @return void
 */
void destroy_spatial_rule_set(ViewerSpatialRuleSet *rule_set) {
    if (!rule_set) {
        return;
    }
    for (uint32_t i = 0; i < rule_set->count && i < FX_MAX_SPATIAL_RULES; i++) {
        destroy_collision_policy(&rule_set->items[i]);
    }
    memset(rule_set, 0, sizeof(*rule_set));
}

/**
 * @brief Builds a `SpatialEvent` record from a concrete collision policy.
 *
 * @param bundle Active viewer scene bundle.
 * @param policy Concrete collision policy for the hit.
 * @param phase Collision edge symbol such as `:enter` or `:exit`.
 * @param snapshot_gen Snapshot generation observed during detection.
 * @param self_box World-space AABB of the initiating entity.
 * @param other_box World-space AABB of the collided entity.
 * @return Newly created event record or `NULL` when required metadata is unavailable.
 */
ID fx_collision_make_spatial_event_snapshot(const ViewerCollisionPolicy *policy,
                                                ID slot_id,
                                                ID phase,
                                                uint32_t snapshot_gen,
                                                ID self_entity,
                                                ID other_entity,
                                                const VgAabb *self_box,
                                                const VgAabb *other_box) {
    if (!fx_collision_scene_descriptors_ready()) {
        return NULL;
    }
    if (!policy || !phase || !self_box || !other_box) {
        return NULL;
    }
    ID self_aabb_rec = fx_make_aabb_record(self_box);
    ID other_aabb_rec = fx_make_aabb_record(other_box);
    if (!self_aabb_rec || !other_aabb_rec) {
        RELEASE(self_aabb_rec);
        RELEASE(other_aabb_rec);
        return NULL;
    }
    ID values[17] = {
        g_fx_collision_kw_source_spatial,
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
    ID event_rec = (ID)make_record_with_descriptor_values(g_fx_collision_desc_spatial_event,
                                                          values,
                                                          17u);
    RELEASE(self_aabb_rec);
    RELEASE(other_aabb_rec);
    return event_rec;
}

ID fx_collision_make_spatial_event(const ViewerSceneBundle *bundle,
                                       const ViewerCollisionPolicy *policy,
                                       ID phase,
                                       uint32_t snapshot_gen,
                                       const VgAabb *self_box,
                                       const VgAabb *other_box) {
    if (!bundle || !policy || !bundle->primary_scene || !bundle->has_primary_slot ||
        bundle->primary_slot_index >= bundle->slot_count) {
        return NULL;
    }
    ID entity_index = fx_collision_scene_entity_map(bundle->primary_scene);
    ID self_entity = entity_index ? map_get_sentinel(entity_index, policy->self_entity_id, NULL) : NULL;
    ID other_entity = entity_index ? map_get_sentinel(entity_index, policy->other_entity_id, NULL) : NULL;
    ID slot_id = policy->slot_id ? policy->slot_id : bundle->slots[bundle->primary_slot_index].id;
    return fx_collision_make_spatial_event_snapshot(policy,
                                                        slot_id,
                                                        phase,
                                                        snapshot_gen,
                                                        self_entity,
                                                        other_entity,
                                                        self_box,
                                                        other_box);
}

/**
 * @brief Expands scene-level spatial rules into concrete host collision policies.
 *
 * @param scene Scene whose `:collision-rules` should be expanded.
 * @param io_rule_set Rule set updated in place on success.
 * @return `true` when the scene publishes a valid collision rule shape.
 */
bool fx_collision_load_rules_from_scene(FrameScene *scene,
                                            ViewerSpatialRuleSet *io_rule_set) {
    if (!fx_collision_scene_symbols_ready()) {
        return false;
    }
    if (!scene || !io_rule_set) {
        return false;
    }
    bool ok = true;
    uint32_t next_version = fx_next_rule_set_version(io_rule_set);
    ID rules = tiny_fx_gfx_get_field((ID)scene, g_fx_collision_kw_collision_rules, NULL);
    if (!rules) {
        destroy_spatial_rule_set(io_rule_set);
        io_rule_set->version = next_version;
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
    next_rule_set.version = next_version;
    const char *rule_capacity_msg =
        "FX_MAX_SPATIAL_RULES is too small for the scene's collision policies";
    uint32_t rule_count = vector_count(rules_vec);
    if (rule_count > FX_MAX_SPATIAL_RULES) {
        CLJ_ASSERT(false && rule_capacity_msg);
        destroy_spatial_rule_set(io_rule_set);
        return false;
    }
    for (uint32_t i = 0; i < rule_count && ok; i++) {
        ID rule = vector_nth(rules_vec, i);
        if (!rule || TAG(rule) != CLJ_RECORD) {
            destroy_spatial_rule_set(&next_rule_set);
            ok = false;
            break;
        }
        ID slot_obj = tiny_fx_gfx_get_field(rule, g_fx_collision_kw_slot, NULL);
        ID id_obj = tiny_fx_gfx_get_field(rule, g_fx_collision_kw_id, NULL);
        ID kind_obj = tiny_fx_gfx_get_field(rule,
                                            g_fx_collision_kw_kind,
                                            g_fx_collision_kw_collision);
        ID channel_obj = tiny_fx_gfx_get_field(rule, g_fx_collision_kw_channel, NULL);
        ID radius_obj = tiny_fx_gfx_get_field(rule, g_fx_collision_kw_radius, fixnum(0));
        ID self_selector = tiny_fx_gfx_get_field(rule, g_fx_collision_kw_self, NULL);
        ID other_selector = tiny_fx_gfx_get_field(rule, g_fx_collision_kw_other, NULL);
        if (!self_selector) {
            self_selector = tiny_fx_gfx_get_field(rule, g_fx_collision_kw_a_id, NULL);
        }
        if (!other_selector) {
            other_selector = tiny_fx_gfx_get_field(rule, g_fx_collision_kw_b_id, NULL);
        }
        if (!is_fixnum(radius_obj) || !self_selector || !other_selector) {
            destroy_spatial_rule_set(&next_rule_set);
            ok = false;
            break;
        }
        ID entity_index = fx_collision_scene_entity_map(scene);
        if (!entity_index || !is_map(entity_index)) {
            destroy_spatial_rule_set(&next_rule_set);
            ok = false;
            break;
        }
        ID self_ids[FX_MAX_SPATIAL_RULES] = {0};
        ID other_ids[FX_MAX_SPATIAL_RULES] = {0};
        uint32_t self_count = fx_collect_selector_entity_ids(entity_index,
                                                                 self_selector,
                                                                 self_ids,
                                                                 FX_MAX_SPATIAL_RULES);
        uint32_t other_count = fx_collect_selector_entity_ids(entity_index,
                                                                  other_selector,
                                                                  other_ids,
                                                                  FX_MAX_SPATIAL_RULES);
        for (uint32_t self_i = 0; self_i < self_count && ok; self_i++) {
            for (uint32_t other_i = 0; other_i < other_count; other_i++) {
                if (next_rule_set.count >= FX_MAX_SPATIAL_RULES) {
                    CLJ_ASSERT(false && rule_capacity_msg);
                    destroy_spatial_rule_set(&next_rule_set);
                    return false;
                }
                ViewerCollisionPolicy *dst = &next_rule_set.items[next_rule_set.count];
                ID self_rec = map_get_sentinel(entity_index, self_ids[self_i], NULL);
                ID other_rec = map_get_sentinel(entity_index, other_ids[other_i], NULL);
                if (!self_rec || !other_rec) {
                    continue;
                }
                dst->self_entity_id = RETAIN(self_ids[self_i]);
                dst->other_entity_id = RETAIN(other_ids[other_i]);
                dst->self_prototype = RETAIN(fx_entity_prototype(self_rec));
                dst->other_prototype = RETAIN(fx_entity_prototype(other_rec));
                dst->radius_px = AS_FIXNUM(radius_obj);
                dst->slot_id = RETAIN(slot_obj);
                dst->rule = RETAIN(rule);
                dst->rule_id = RETAIN(id_obj);
                dst->kind = RETAIN(kind_obj);
                dst->channel = RETAIN(channel_obj);
                for (uint32_t j = 0; j < io_rule_set->count; j++) {
                    if (fx_collision_policy_same_identity(dst, &io_rule_set->items[j])) {
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
