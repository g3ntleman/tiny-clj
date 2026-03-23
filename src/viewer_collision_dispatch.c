#include "viewer_collision_bridge.h"

#include <pthread.h>
#include <string.h>

#include "event_loop.h"
#include "memory.h"
#include "rendered_state_snapshot.h"
#include "viewer_host_slots.h"

#define VIEWER_COLLISION_EVENT_HEADROOM_BYTES (4u * 1024u)
#define VIEWER_COLLISION_EVENT_MAX_PENDING 48u
#define VIEWER_COLLISION_RAW_HIT_CAP 512u

typedef struct {
    uint16_t policy_index;
    uint32_t rule_set_version;
    uint32_t snapshot_gen;
    bool entering;
    VgAabb self_box;
    VgAabb other_box;
} ViewerRawCollisionHit;

typedef struct {
    pthread_mutex_t mutex;
    struct ViewerSceneBundle *bundle;
    ViewerSpatialRuleSet *rule_set;
    ViewerRawCollisionHit hits[VIEWER_COLLISION_RAW_HIT_CAP];
    uint16_t head;
    uint16_t count;
} ViewerCollisionDispatchState;

static ViewerCollisionDispatchState g_viewer_collision_dispatch = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};

static ID g_viewer_collision_kw_phase_enter;
static ID g_viewer_collision_kw_phase_exit;

static IdSymbolCacheEntry g_viewer_collision_dispatch_symbols[] = {
    { &g_viewer_collision_kw_phase_enter, ":enter" },
    { &g_viewer_collision_kw_phase_exit, ":exit" },
};
#define VC_DISPATCH_SYM_COUNT (sizeof(g_viewer_collision_dispatch_symbols) / sizeof(g_viewer_collision_dispatch_symbols[0]))

static pthread_once_t g_viewer_collision_dispatch_symbols_once = PTHREAD_ONCE_INIT;

static void viewer_collision_dispatch_symbols_init_once(void) {
    for (size_t i = 0; i < VC_DISPATCH_SYM_COUNT; i++) {
        *g_viewer_collision_dispatch_symbols[i].slot =
            intern_symbol_global(g_viewer_collision_dispatch_symbols[i].cname);
    }
}

static bool viewer_collision_dispatch_symbols_ready(void) {
    (void)pthread_once(&g_viewer_collision_dispatch_symbols_once,
                       viewer_collision_dispatch_symbols_init_once);
    for (size_t i = 0; i < VC_DISPATCH_SYM_COUNT; i++) {
        if (!*g_viewer_collision_dispatch_symbols[i].slot) {
            return false;
        }
    }
    return true;
}

static void viewer_collision_reset_dispatch_state_locked(void) {
    g_viewer_collision_dispatch.bundle = NULL;
    g_viewer_collision_dispatch.rule_set = NULL;
    g_viewer_collision_dispatch.head = 0u;
    g_viewer_collision_dispatch.count = 0u;
    memset(g_viewer_collision_dispatch.hits, 0, sizeof(g_viewer_collision_dispatch.hits));
}

static bool viewer_collision_enqueue_raw_hit(uint16_t policy_index,
                                             uint32_t rule_set_version,
                                             bool entering,
                                             uint32_t snapshot_gen,
                                             const VgAabb *self_box,
                                             const VgAabb *other_box) {
    if (!self_box || !other_box) {
        return false;
    }
    viewer_collision_dispatch_state_lock();
    bool ok = false;
    if (g_viewer_collision_dispatch.bundle &&
        g_viewer_collision_dispatch.rule_set &&
        g_viewer_collision_dispatch.count < VIEWER_COLLISION_RAW_HIT_CAP) {
        uint16_t tail = (uint16_t)((g_viewer_collision_dispatch.head + g_viewer_collision_dispatch.count) %
                                   VIEWER_COLLISION_RAW_HIT_CAP);
        ViewerRawCollisionHit *dst = &g_viewer_collision_dispatch.hits[tail];
        dst->policy_index = policy_index;
        dst->rule_set_version = rule_set_version;
        dst->snapshot_gen = snapshot_gen;
        dst->entering = entering;
        dst->self_box = *self_box;
        dst->other_box = *other_box;
        g_viewer_collision_dispatch.count++;
        ok = true;
    }
    viewer_collision_dispatch_state_unlock();
    return ok;
}

static bool viewer_invoke_collision_callback(const ViewerSceneBundle *bundle,
                                             ID event_payload) {
    if (!bundle || !bundle->spatial_callback || !event_payload) {
        return false;
    }
    return event_loop_enqueue_ingress_call(bundle->spatial_callback, event_payload);
}

static bool viewer_collision_event_budget_available(void) {
    /*
     * Spatial-event creation allocates AABB records + event record + ingress task.
     * Under tight heap pressure we intentionally drop events instead of risking
     * throw_oom() on the host UI thread.
     */
    size_t heap_limit = memory_get_heap_limit_bytes();
    if (heap_limit < SIZE_MAX) {
        size_t heap_used = memory_current_usage_bytes();
        if (heap_used >= heap_limit) {
            return false;
        }
        size_t heap_free = heap_limit - heap_used;
        if (heap_free < VIEWER_COLLISION_EVENT_HEADROOM_BYTES) {
            return false;
        }
    }
    EventLoopIngressStats ingress_stats = {0};
    if (event_loop_ingress_stats(&ingress_stats) &&
        ingress_stats.pending_count > VIEWER_COLLISION_EVENT_MAX_PENDING) {
        return false;
    }
    return true;
}

static bool viewer_aabb_intersects_clip_rect(const VgAabb *box, VgClipRect rect) {
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

static bool viewer_aabb_intersects_any_dirty_rect(const VgAabb *box,
                                                  const VgClipRect *dirty_rects,
                                                  size_t dirty_rect_count) {
    if (!box || !dirty_rects || dirty_rect_count == 0u) {
        return false;
    }
    for (size_t i = 0; i < dirty_rect_count; i++) {
        if (viewer_aabb_intersects_clip_rect(box, dirty_rects[i])) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Acquires the shared host collision dispatch lock.
 *
 * @param void
 * @return void
 */
void viewer_collision_dispatch_state_lock(void) {
    (void)pthread_mutex_lock(&g_viewer_collision_dispatch.mutex);
}

/**
 * @brief Releases the shared host collision dispatch lock.
 *
 * @param void
 * @return void
 */
void viewer_collision_dispatch_state_unlock(void) {
    (void)pthread_mutex_unlock(&g_viewer_collision_dispatch.mutex);
}

/**
 * @brief Publishes the currently active bundle/rule-set pair for deferred collision dispatch.
 *
 * @param bundle Scene bundle whose callback should receive drained hits.
 * @param rule_set Active rule set generation for stale-hit suppression.
 * @return void
 */
void viewer_collision_set_dispatch_context(struct ViewerSceneBundle *bundle,
                                           ViewerSpatialRuleSet *rule_set) {
    viewer_collision_dispatch_state_lock();
    if (!bundle || !rule_set) {
        viewer_collision_reset_dispatch_state_locked();
    } else {
        g_viewer_collision_dispatch.bundle = bundle;
        g_viewer_collision_dispatch.rule_set = rule_set;
    }
    viewer_collision_dispatch_state_unlock();
}

/**
 * @brief Clears the deferred collision dispatch queue and active context.
 *
 * @param void
 * @return void
 */
void viewer_collision_reset_dispatch_state(void) {
    viewer_collision_dispatch_state_lock();
    viewer_collision_reset_dispatch_state_locked();
    viewer_collision_dispatch_state_unlock();
}

/**
 * @brief Detects collision edges for the current rendered snapshot without allocating events.
 *
 * @param bundle Active scene bundle with the game slot.
 * @param rule_set Concrete collision policies and latch state.
 * @param now_ms Current host clock in milliseconds.
 * @param dirty_rects Optional dirty-rect filter for incremental rendering.
 * @param dirty_rect_count Number of entries in `dirty_rects`.
 * @return `true` when at least one collision edge was queued for deferred dispatch.
 */
bool viewer_collision_detect_step(ViewerSceneBundle *bundle,
                                  ViewerSpatialRuleSet *rule_set,
                                  uint32_t now_ms,
                                  const VgClipRect *dirty_rects,
                                  size_t dirty_rect_count) {
    if (!bundle || !rule_set || !bundle->game_scene || !bundle->has_game_slot) {
        return false;
    }

    (void)now_ms;
    bool use_dirty_filter = dirty_rects && dirty_rect_count > 0u;
    bool any_triggered = false;
    uint8_t game_slot = bundle->game_slot_index;
    typedef struct {
        ID entity_id;
        bool resolved;
        bool present;
        VgRenderedEntityState state;
    } ViewerEntityStateCacheEntry;
    ViewerEntityStateCacheEntry state_cache[VIEWER_MAX_SPATIAL_RULES * 2u] = {0};
    uint32_t state_cache_count = 0u;
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
            bool self_touched = viewer_aabb_intersects_any_dirty_rect(&self_box, dirty_rects, dirty_rect_count);
            bool other_touched = viewer_aabb_intersects_any_dirty_rect(&other_box, dirty_rects, dirty_rect_count);
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
        bool queued = viewer_collision_enqueue_raw_hit((uint16_t)i,
                                                       rule_set->version,
                                                       inside,
                                                       other_state.snapshot_generation,
                                                       &self_box,
                                                       &other_box);
        if (!queued) {
            continue;
        }
        state->collision_latched = inside;
        any_triggered = true;
    }
    return any_triggered;
}

/**
 * @brief Drains queued raw collision hits into `SpatialEvent` callbacks on the scheduler thread.
 *
 * @param void
 * @return `true` when at least one queued hit was consumed or discarded.
 */
bool viewer_collision_poll_drain(void) {
    bool drained_any = false;
    if (!viewer_collision_dispatch_symbols_ready()) {
        return false;
    }

    while (viewer_collision_event_budget_available()) {
        ID event_payload = NULL;
        bool enqueued = false;

        viewer_collision_dispatch_state_lock();
        if (g_viewer_collision_dispatch.count == 0u ||
            !g_viewer_collision_dispatch.bundle ||
            !g_viewer_collision_dispatch.rule_set) {
            viewer_collision_dispatch_state_unlock();
            break;
        }

        ViewerRawCollisionHit *hit = &g_viewer_collision_dispatch.hits[g_viewer_collision_dispatch.head];
        ViewerSceneBundle *bundle = g_viewer_collision_dispatch.bundle;
        ViewerSpatialRuleSet *rule_set = g_viewer_collision_dispatch.rule_set;
        if (hit->rule_set_version != rule_set->version ||
            hit->policy_index >= rule_set->count ||
            !bundle->spatial_callback) {
            memset(hit, 0, sizeof(*hit));
            g_viewer_collision_dispatch.head =
                (uint16_t)((g_viewer_collision_dispatch.head + 1u) % VIEWER_COLLISION_RAW_HIT_CAP);
            g_viewer_collision_dispatch.count--;
            drained_any = true;
            viewer_collision_dispatch_state_unlock();
            continue;
        }

        ViewerCollisionPolicy *policy = &rule_set->items[hit->policy_index];
        ID phase = hit->entering ? g_viewer_collision_kw_phase_enter : g_viewer_collision_kw_phase_exit;
        event_payload = viewer_collision_make_spatial_event(bundle,
                                                            policy,
                                                            phase,
                                                            hit->snapshot_gen,
                                                            &hit->self_box,
                                                            &hit->other_box);
        if (event_payload) {
            enqueued = viewer_invoke_collision_callback(bundle, event_payload);
        }
        if (enqueued || !event_payload) {
            memset(hit, 0, sizeof(*hit));
            g_viewer_collision_dispatch.head =
                (uint16_t)((g_viewer_collision_dispatch.head + 1u) % VIEWER_COLLISION_RAW_HIT_CAP);
            g_viewer_collision_dispatch.count--;
            drained_any = true;
        }
        viewer_collision_dispatch_state_unlock();

        RELEASE(event_payload);
        if (!event_payload || !enqueued) {
            break;
        }
    }

    return drained_any;
}
