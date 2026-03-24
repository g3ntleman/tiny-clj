#include "fx_spatial_bridge.h"

#include <string.h>

#include "event_loop.h"
#include "eval.h"
#include "memory.h"
#include "rendered_state_snapshot.h"
#include "fx_config_loader.h"

#define FX_COLLISION_EVENT_HEADROOM_BYTES (4u * 1024u)
#define FX_COLLISION_EVENT_MAX_PENDING 48u
typedef struct {
    ID callback_fn;
    ID rule_id;
    ID slot_id;
    ID kind;
    ID phase;
    ID self_entity_id;
    ID other_entity_id;
    ID self_entity;
    ID other_entity;
    ID rule;
    ID self_prototype;
    ID other_prototype;
    ID channel;
    int radius_px;
    uint32_t snapshot_gen;
    VgAabb self_box;
    VgAabb other_box;
} ViewerCollisionIngressCtx;

static ID g_fx_collision_kw_phase_enter;
static ID g_fx_collision_kw_phase_exit;

static IdSymbolCacheEntry g_fx_collision_dispatch_symbols[] = {
    { &g_fx_collision_kw_phase_enter, ":enter" },
    { &g_fx_collision_kw_phase_exit, ":exit" },
};
#define VC_DISPATCH_SYM_COUNT (sizeof(g_fx_collision_dispatch_symbols) / sizeof(g_fx_collision_dispatch_symbols[0]))

static pthread_once_t g_fx_collision_dispatch_symbols_once = PTHREAD_ONCE_INIT;

static bool fx_collision_event_budget_available(void);

typedef struct {
    ID entity_id;
    bool resolved;
    bool present;
    VgRenderedEntityState state;
} ViewerEntityStateCacheEntry;

static void fx_collision_dispatch_symbols_init_once(void) {
    for (size_t i = 0; i < VC_DISPATCH_SYM_COUNT; i++) {
        *g_fx_collision_dispatch_symbols[i].slot =
            intern_symbol_global(g_fx_collision_dispatch_symbols[i].cname);
    }
}

static bool fx_collision_dispatch_symbols_ready(void) {
    (void)pthread_once(&g_fx_collision_dispatch_symbols_once,
                       fx_collision_dispatch_symbols_init_once);
    for (size_t i = 0; i < VC_DISPATCH_SYM_COUNT; i++) {
        if (!*g_fx_collision_dispatch_symbols[i].slot) {
            return false;
        }
    }
    return true;
}

static void fx_collision_ingress_cleanup(void *ctx) {
    ViewerCollisionIngressCtx *event_ctx = (ViewerCollisionIngressCtx *)ctx;
    if (!event_ctx) {
        return;
    }
    RELEASE(event_ctx->callback_fn);
    RELEASE(event_ctx->rule_id);
    RELEASE(event_ctx->slot_id);
    RELEASE(event_ctx->kind);
    RELEASE(event_ctx->self_entity_id);
    RELEASE(event_ctx->other_entity_id);
    RELEASE(event_ctx->self_entity);
    RELEASE(event_ctx->other_entity);
    RELEASE(event_ctx->rule);
    RELEASE(event_ctx->self_prototype);
    RELEASE(event_ctx->other_prototype);
    RELEASE(event_ctx->channel);
    CLJ_FREE(event_ctx);
}

static void fx_collision_ingress_run(void *ctx, EvalState *st) {
    ViewerCollisionIngressCtx *event_ctx = (ViewerCollisionIngressCtx *)ctx;
    if (!event_ctx || !event_ctx->callback_fn) {
        return;
    }

    ViewerCollisionPolicy policy = {
        .self_entity_id = event_ctx->self_entity_id,
        .other_entity_id = event_ctx->other_entity_id,
        .self_prototype = event_ctx->self_prototype,
        .other_prototype = event_ctx->other_prototype,
        .radius_px = event_ctx->radius_px,
        .slot_id = event_ctx->slot_id,
        .rule = event_ctx->rule,
        .rule_id = event_ctx->rule_id,
        .kind = event_ctx->kind,
        .channel = event_ctx->channel,
    };

    ID event_payload = fx_collision_make_spatial_event_snapshot(&policy,
                                                                    event_ctx->slot_id,
                                                                    event_ctx->phase,
                                                                    event_ctx->snapshot_gen,
                                                                    event_ctx->self_entity,
                                                                    event_ctx->other_entity,
                                                                    &event_ctx->self_box,
                                                                    &event_ctx->other_box);
    if (!event_payload) {
        return;
    }

    ID args[1] = { event_payload };
    (void)eval_function_call(event_ctx->callback_fn, args, 1u, NULL, st);
    RELEASE(event_payload);
}

static bool fx_collision_enqueue_event(const ViewerSceneBundle *bundle,
                                           const ViewerCollisionPolicy *policy,
                                           ID phase,
                                           uint32_t snapshot_gen,
                                           ID self_entity,
                                           ID other_entity,
                                           const VgAabb *self_box,
                                           const VgAabb *other_box) {
    if (!bundle || !policy || !bundle->spatial_callback || !phase || !self_box || !other_box) {
        return false;
    }
    if (!bundle->has_primary_slot || bundle->primary_slot_index >= bundle->slot_count) {
        return false;
    }
    if (!fx_collision_event_budget_available()) {
        return false;
    }

    ViewerCollisionIngressCtx *ctx =
        (ViewerCollisionIngressCtx *)CLJ_MALLOC(sizeof(ViewerCollisionIngressCtx));
    if (!ctx) {
        return false;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->callback_fn = RETAIN(bundle->spatial_callback);
    ctx->rule_id = RETAIN(policy->rule_id);
    ctx->slot_id = RETAIN(policy->slot_id ? policy->slot_id : bundle->slots[bundle->primary_slot_index].id);
    ctx->kind = RETAIN(policy->kind);
    ctx->phase = phase;
    ctx->self_entity_id = RETAIN(policy->self_entity_id);
    ctx->other_entity_id = RETAIN(policy->other_entity_id);
    ctx->self_entity = RETAIN(self_entity);
    ctx->other_entity = RETAIN(other_entity);
    ctx->rule = RETAIN(policy->rule);
    ctx->self_prototype = RETAIN(policy->self_prototype);
    ctx->other_prototype = RETAIN(policy->other_prototype);
    ctx->channel = RETAIN(policy->channel);
    ctx->radius_px = policy->radius_px;
    ctx->snapshot_gen = snapshot_gen;
    ctx->self_box = *self_box;
    ctx->other_box = *other_box;

    if (!event_loop_enqueue_ingress_native(fx_collision_ingress_run,
                                           ctx,
                                           fx_collision_ingress_cleanup)) {
        fx_collision_ingress_cleanup(ctx);
        return false;
    }
    return true;
}

static bool fx_collision_lookup_entity_state(ViewerEntityStateCacheEntry *cache,
                                                 uint32_t *cache_count,
                                                 ID entity_id,
                                                 uint8_t primary_slot,
                                                 VgRenderedEntityState *out_state) {
    if (!cache || !cache_count || !entity_id || !out_state) {
        return false;
    }

    for (uint32_t i = 0; i < *cache_count; i++) {
        ViewerEntityStateCacheEntry *entry = &cache[i];
        if (entry->resolved && entry->entity_id == entity_id) {
            if (entry->present) {
                *out_state = entry->state;
            }
            return entry->present;
        }
    }

    bool present = vg_rendered_state_query_entity(primary_slot, (uintptr_t)entity_id, out_state);
    if (*cache_count < (FX_MAX_SPATIAL_RULES * 2u)) {
        ViewerEntityStateCacheEntry *entry = &cache[(*cache_count)++];
        entry->entity_id = entity_id;
        entry->resolved = true;
        entry->present = present;
        if (present) {
            entry->state = *out_state;
        }
    }
    return present;
}

static bool fx_collision_event_budget_available(void) {
    /*
     * Spatial ingress still allocates a small retained context and later the
     * actual event record on the interpreter thread. Under tight heap pressure
     * we intentionally drop events instead of risking throw_oom() on the host
     * UI thread.
     */
    size_t heap_limit = memory_get_heap_limit_bytes();
    if (heap_limit < SIZE_MAX) {
        size_t heap_used = memory_current_usage_bytes();
        if (heap_used >= heap_limit) {
            return false;
        }
        size_t heap_free = heap_limit - heap_used;
        if (heap_free < FX_COLLISION_EVENT_HEADROOM_BYTES) {
            return false;
        }
    }
    EventLoopIngressStats ingress_stats = {0};
    if (event_loop_ingress_stats(&ingress_stats) &&
        ingress_stats.pending_count > FX_COLLISION_EVENT_MAX_PENDING) {
        return false;
    }
    return true;
}

static bool fx_aabb_intersects_clip_rect(const VgAabb *box, VgClipRect rect) {
    if (!box || vg_clip_rect_is_empty(rect)) {
        return false;
    }
    int rect_min_x = rect.x;
    int rect_min_y = rect.y;
    int rect_max_x = (int)rect.x + (int)rect.w - 1;
    int rect_max_y = (int)rect.y + (int)rect.h - 1;
    if (box->max_x < rect_min_x || box->min_x > rect_max_x) {
        return false;
    }
    if (box->max_y < rect_min_y || box->min_y > rect_max_y) {
        return false;
    }
    return true;
}

static bool fx_aabb_intersects_any_dirty_rect(const VgAabb *box,
                                                  const VgClipRect *dirty_rects,
                                                  size_t dirty_rect_count) {
    if (!box || !dirty_rects || dirty_rect_count == 0u) {
        return false;
    }
    for (size_t i = 0; i < dirty_rect_count; i++) {
        if (fx_aabb_intersects_clip_rect(box, dirty_rects[i])) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Detects collision edges for the current rendered snapshot and pushes callbacks via ingress.
 *
 * @param bundle Active scene bundle with the game slot.
 * @param rule_set Concrete collision policies and latch state.
 * @param now_ms Current host clock in milliseconds.
 * @param dirty_rects Optional dirty-rect filter for incremental rendering.
 * @param dirty_rect_count Number of entries in `dirty_rects`.
 * @return `true` when at least one collision edge was observed.
 */
bool fx_collision_detect_step(ViewerSceneBundle *bundle,
                                  ViewerSpatialRuleSet *rule_set,
                                  uint32_t now_ms,
                                  const VgClipRect *dirty_rects,
                                  size_t dirty_rect_count) {
    if (!bundle || !rule_set || !bundle->primary_scene || !bundle->has_primary_slot) {
        return false;
    }
    if (!fx_collision_dispatch_symbols_ready()) {
        return false;
    }

    (void)now_ms;
    bool use_dirty_filter = dirty_rects && dirty_rect_count > 0u;
    bool any_triggered = false;
    uint8_t primary_slot = bundle->primary_slot_index;
    ID entity_index = fx_collision_scene_entity_map(bundle->primary_scene);
    ViewerEntityStateCacheEntry state_cache[FX_MAX_SPATIAL_RULES * 2u] = {0};
    uint32_t state_cache_count = 0u;
    for (uint32_t i = 0; i < rule_set->count; i++) {
        ViewerCollisionPolicy *policy = &rule_set->items[i];
        VgCollisionState *state = &rule_set->states[i];
        VgRenderedEntityState self_state;
        VgRenderedEntityState other_state;
        bool have_self = fx_collision_lookup_entity_state(state_cache,
                                                              &state_cache_count,
                                                              policy->self_entity_id,
                                                              primary_slot,
                                                              &self_state);
        bool have_other = fx_collision_lookup_entity_state(state_cache,
                                                               &state_cache_count,
                                                               policy->other_entity_id,
                                                               primary_slot,
                                                               &other_state);
        if (!have_self || !have_other) {
            /*
             * Dirty-rect rendering can transiently omit entities from a single
             * snapshot. Drop the latch in that case so the next visible overlap
             * can re-emit an enter edge instead of getting stuck suppressed.
             */
            state->collision_latched = false;
            continue;
        }
        if (!self_state.has_world_aabb || !other_state.has_world_aabb) {
            state->collision_latched = false;
            continue;
        }
        VgAabb self_box = self_state.world_aabb;
        VgAabb other_box = other_state.world_aabb;
        if (use_dirty_filter) {
            bool self_touched = fx_aabb_intersects_any_dirty_rect(&self_box, dirty_rects, dirty_rect_count);
            bool other_touched = fx_aabb_intersects_any_dirty_rect(&other_box, dirty_rects, dirty_rect_count);
            if (!self_touched && !other_touched) {
                continue;
            }
        }
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
        ID self_entity = entity_index ? map_get_sentinel(entity_index, policy->self_entity_id, NULL) : NULL;
        ID other_entity = entity_index ? map_get_sentinel(entity_index, policy->other_entity_id, NULL) : NULL;
        ID phase = inside ? g_fx_collision_kw_phase_enter : g_fx_collision_kw_phase_exit;
        (void)fx_collision_enqueue_event(bundle,
                                             policy,
                                             phase,
                                             other_state.snapshot_generation,
                                             self_entity,
                                             other_entity,
                                             &self_box,
                                             &other_box);
        any_triggered = true;
    }
    return any_triggered;
}
