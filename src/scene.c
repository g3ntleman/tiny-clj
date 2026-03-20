#include "scene.h"
#include "gfx.h"
#include "tiny_fx_gfx.h"
#include "rendered_state_snapshot.h"
#include "platform.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include "callbacks.h"
#include "hashmap.h"
#include "map.h"
#include "record.h"
#include "symbol_cache.h"
#include "strings.h"
#include "symbol.h"
#include "thread_local.h"
#include "value.h"
#include "vector.h"

#define STATIC_SYMBOL_DATA(name, cname_literal) \
    static StaticSymbolData name = { \
        .sym = { \
            .base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = 0}, \
            .ns_name = NULL, \
            .unqualified = NULL, \
            .cname = cname_literal \
        } \
    }

STATIC_SYMBOL_DATA(sym_entity_root_data, "root");

#undef STATIC_SYMBOL_DATA

typedef struct {
    ID entity_map;
    CljHashMap *index;
} VgFlatSceneLookup;

static THREAD_LOCAL CljHashMap *g_flat_scene_lookup_scratch = NULL;
static CljSymbol *g_kw_timeline_ease = NULL;
static CljSymbol *g_kw_timeline_easing = NULL;
static const SymbolCacheEntry g_scene_symbol_cache[] = {
    {&g_kw_timeline_ease, ":ease", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_kw_timeline_easing, ":easing", SYMBOL_CACHE_SCOPE_GLOBAL},
};

static inline void scene_ensure_timeline_keyword_cache(void) {
    if (!g_kw_timeline_ease || !g_kw_timeline_easing) {
        (void)symbol_cache_init_global(
            g_scene_symbol_cache,
            sizeof(g_scene_symbol_cache) / sizeof(g_scene_symbol_cache[0]));
    }
}

static void vg_flat_scene_lookup_reset_borrowed(CljHashMap *index) {
    if (!index) {
        return;
    }
    for (unsigned int i = 0; i < index->capacity; i++) {
        KV_SET_KEY(index->data, i, HASHMAP_EMPTY);
        KV_SET_VALUE(index->data, i, NULL);
    }
    index->count = 0u;
    index->tombstones = 0u;
}

static bool vg_flat_scene_lookup_reserve(unsigned int entry_count) {
    unsigned int required = entry_count > 0u ? (entry_count * 2u) : 8u;
    if (g_flat_scene_lookup_scratch && g_flat_scene_lookup_scratch->capacity >= required) {
        vg_flat_scene_lookup_reset_borrowed(g_flat_scene_lookup_scratch);
        return true;
    }

    CljHashMap *replacement = make_hashmap(required);
    if (!replacement) {
        return false;
    }

    if (g_flat_scene_lookup_scratch) {
        vg_flat_scene_lookup_reset_borrowed(g_flat_scene_lookup_scratch);
        RELEASE(g_flat_scene_lookup_scratch);
    }
    g_flat_scene_lookup_scratch = replacement;
    return true;
}

static unsigned int vg_flat_scene_lookup_find_slot(CljHashMap *index, ID key) {
    CLJ_ASSERT(index && index->capacity > 0u);
    unsigned int mask = index->capacity - 1u;
    unsigned int start_idx = clj_hash(key) & mask;
    unsigned int idx = start_idx;

    do {
        ID stored_key = KV_KEY(index->data, idx);
        if (stored_key == HASHMAP_EMPTY) {
            return idx;
        }
        if (clj_equal(stored_key, key)) {
            return idx;
        }
        idx = (idx + 1u) & mask;
    } while (idx != start_idx);

    return UINT_MAX;
}

static bool vg_flat_scene_lookup_insert_borrowed(CljHashMap *index, ID key, ID value) {
    if (!index) {
        return false;
    }
    unsigned int idx = vg_flat_scene_lookup_find_slot(index, key);
    if (idx == UINT_MAX) {
        return false;
    }
    if (KV_KEY(index->data, idx) == HASHMAP_EMPTY) {
        KV_SET_KEY(index->data, idx, key);
        KV_SET_VALUE(index->data, idx, value);
        index->count++;
        return true;
    }
    KV_SET_VALUE(index->data, idx, value);
    return true;
}

static bool vg_flat_scene_lookup_build(ID entity_map, VgFlatSceneLookup *out_lookup) {
    if (!out_lookup) {
        return false;
    }
    out_lookup->entity_map = entity_map;
    out_lookup->index = NULL;

    CljPersistentMap *backing = map_backing(entity_map);
    if (!backing) {
        return true;
    }
    unsigned int entry_count = (unsigned int)((backing->count > 0) ? backing->count : 0);
    if (!vg_flat_scene_lookup_reserve(entry_count)) {
        return false;
    }

    out_lookup->index = g_flat_scene_lookup_scratch;
    if (entry_count == 0u) {
        return true;
    }

    MAP_FOR_EACH(entity_map, key, value) {
        if (!vg_flat_scene_lookup_insert_borrowed(out_lookup->index, key, value)) {
            vg_flat_scene_lookup_reset_borrowed(out_lookup->index);
            out_lookup->index = NULL;
            return false;
        }
    }
    return true;
}

static inline ID vg_flat_scene_lookup_get(const VgFlatSceneLookup *lookup, ID entity_map, ID key) {
    if (lookup && lookup->index && lookup->entity_map == entity_map) {
        return hashmap_get_sentinel(lookup->index, key, NULL);
    }
    return map_get_sentinel(entity_map, key, NULL);
}

static inline bool vg_scene_root_is_canonical(ID root_field) {
    return root_field && clj_equal(root_field, (ID)&sym_entity_root_data.sym);
}

static inline uint32_t record_type_hash(ID obj) {
    CljPersistentRecord *r = (CljPersistentRecord *)obj;
    return r->descriptor ? clj_hash(r->descriptor->type_symbol) : 0;
}

static uint32_t slot_change_tracker_snapshot_mask(const VgSlotChangeTracker *tracker,
                                                  const uint32_t *last_seen_generations,
                                                  uint32_t *out_generations) {
    if (!tracker || tracker->slot_count == 0 || tracker->slot_count > VG_SLOT_CHANGE_TRACKER_MAX_SLOTS) {
        return 0;
    }
    uint32_t changed_mask = 0;
    for (uint8_t i = 0; i < tracker->slot_count; i++) {
        uint32_t current = tracker->generations[i];
        if (out_generations) {
            out_generations[i] = current;
        }
        uint32_t prev = last_seen_generations ? last_seen_generations[i] : 0u;
        if (current != prev) {
            changed_mask |= (1u << i);
        }
    }
    return changed_mask;
}

#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD
static void slot_change_tracker_deadline_from_now(uint32_t timeout_ms, struct timespec *out_deadline) {
    if (!out_deadline) {
        return;
    }
    (void)clock_gettime(CLOCK_REALTIME, out_deadline);
    out_deadline->tv_sec += (time_t)(timeout_ms / 1000u);
    long ns = out_deadline->tv_nsec + (long)(timeout_ms % 1000u) * 1000000L;
    if (ns >= 1000000000L) {
        out_deadline->tv_sec += 1;
        ns -= 1000000000L;
    }
    out_deadline->tv_nsec = ns;
}
#endif

bool vg_slot_change_tracker_init(VgSlotChangeTracker *tracker, uint8_t slot_count) {
    if (!tracker || slot_count == 0 || slot_count > VG_SLOT_CHANGE_TRACKER_MAX_SLOTS) {
        return false;
    }
    memset(tracker, 0, sizeof(*tracker));
    tracker->slot_count = slot_count;
#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD
    if (pthread_mutex_init(&tracker->mutex, NULL) != 0) {
        memset(tracker, 0, sizeof(*tracker));
        return false;
    }
    if (pthread_cond_init(&tracker->cond, NULL) != 0) {
        (void)pthread_mutex_destroy(&tracker->mutex);
        memset(tracker, 0, sizeof(*tracker));
        return false;
    }
#endif
    return true;
}

void vg_slot_change_tracker_destroy(VgSlotChangeTracker *tracker) {
    if (!tracker) {
        return;
    }
#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD
    if (tracker->slot_count > 0 && tracker->slot_count <= VG_SLOT_CHANGE_TRACKER_MAX_SLOTS) {
        (void)pthread_cond_destroy(&tracker->cond);
        (void)pthread_mutex_destroy(&tracker->mutex);
    }
#endif
    memset(tracker, 0, sizeof(*tracker));
}

bool vg_slot_change_tracker_publish(VgSlotChangeTracker *tracker, uint8_t slot_index, uint32_t *out_generation) {
    if (!tracker || slot_index >= tracker->slot_count || tracker->slot_count > VG_SLOT_CHANGE_TRACKER_MAX_SLOTS) {
        return false;
    }
#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD
    if (pthread_mutex_lock(&tracker->mutex) != 0) {
        return false;
    }
#endif
    uint32_t next = tracker->generations[slot_index] + 1u;
    if (next == 0u) {
        next = 1u;
    }
    tracker->generations[slot_index] = next;
    tracker->sequence++;
    if (out_generation) {
        *out_generation = next;
    }
#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD
    (void)pthread_cond_broadcast(&tracker->cond);
    (void)pthread_mutex_unlock(&tracker->mutex);
#endif
    return true;
}

uint32_t vg_slot_change_tracker_wait_for_changes(VgSlotChangeTracker *tracker,
                                                 const uint32_t *last_seen_generations,
                                                 uint32_t *out_generations,
                                                 uint32_t timeout_ms) {
    if (!tracker || tracker->slot_count == 0 || tracker->slot_count > VG_SLOT_CHANGE_TRACKER_MAX_SLOTS) {
        return 0;
    }
#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD
    if (pthread_mutex_lock(&tracker->mutex) != 0) {
        return 0;
    }
    uint32_t changed_mask = slot_change_tracker_snapshot_mask(tracker, last_seen_generations, out_generations);
    if (changed_mask == 0u && timeout_ms > 0u) {
        if (timeout_ms == UINT32_MAX) {
            while (changed_mask == 0u) {
                (void)pthread_cond_wait(&tracker->cond, &tracker->mutex);
                changed_mask = slot_change_tracker_snapshot_mask(tracker, last_seen_generations, out_generations);
            }
        } else {
            struct timespec deadline;
            slot_change_tracker_deadline_from_now(timeout_ms, &deadline);
            while (changed_mask == 0u) {
                int rc = pthread_cond_timedwait(&tracker->cond, &tracker->mutex, &deadline);
                if (rc == ETIMEDOUT) {
                    break;
                }
                changed_mask = slot_change_tracker_snapshot_mask(tracker, last_seen_generations, out_generations);
            }
            if (changed_mask == 0u) {
                changed_mask = slot_change_tracker_snapshot_mask(tracker, last_seen_generations, out_generations);
            }
        }
    }
    (void)pthread_mutex_unlock(&tracker->mutex);
    return changed_mask;
#else
    uint32_t changed_mask = slot_change_tracker_snapshot_mask(tracker, last_seen_generations, out_generations);
    if (changed_mask != 0u || timeout_ms == 0u) {
        return changed_mask;
    }
    if (timeout_ms == UINT32_MAX) {
        while (changed_mask == 0u) {
            platform_sleep_ms(1u);
            changed_mask = slot_change_tracker_snapshot_mask(tracker, last_seen_generations, out_generations);
        }
        return changed_mask;
    }
    platform_sleep_ms(timeout_ms);
    return slot_change_tracker_snapshot_mask(tracker, last_seen_generations, out_generations);
#endif
}

static inline void transform_point(VgTransformFixed t, int16_t x, int16_t y, int *ox, int *oy) {
    vg_transform_fixed_apply_px(t, x, y, ox, oy);
}

static inline bool aabb_outside_fb(int min_x, int min_y, int max_x, int max_y,
                                   int fb_w, int fb_h,
                                   bool use_clip, VgClipRect clip) {
    int lo_x = 0, lo_y = 0, hi_x = fb_w, hi_y = fb_h;
    if (use_clip) {
        lo_x = clip.x; lo_y = clip.y;
        hi_x = clip.x + clip.w; hi_y = clip.y + clip.h;
    }
    return max_x < lo_x || min_x >= hi_x || max_y < lo_y || min_y >= hi_y;
}

static inline bool node_culled_line(VgTransformFixed t, int16_t x1, int16_t y1,
                                    int16_t x2, int16_t y2, int sw,
                                    int fb_w, int fb_h, bool use_clip, VgClipRect clip) {
    int ax, ay, bx, by;
    transform_point(t, x1, y1, &ax, &ay);
    transform_point(t, x2, y2, &bx, &by);
    int mn_x = (ax < bx ? ax : bx) - sw;
    int mn_y = (ay < by ? ay : by) - sw;
    int mx_x = (ax > bx ? ax : bx) + sw;
    int mx_y = (ay > by ? ay : by) + sw;
    return aabb_outside_fb(mn_x, mn_y, mx_x, mx_y, fb_w, fb_h, use_clip, clip);
}

static inline bool node_culled_rect(VgTransformFixed t, int16_t x, int16_t y,
                                    int16_t w, int16_t h, int sw,
                                    int fb_w, int fb_h, bool use_clip, VgClipRect clip) {
    int c[8];
    int16_t x_max = (w > 0) ? (int16_t)(x + w - 1) : x;
    int16_t y_max = (h > 0) ? (int16_t)(y + h - 1) : y;
    transform_point(t, x,     y,     &c[0], &c[1]);
    transform_point(t, x_max, y,     &c[2], &c[3]);
    transform_point(t, x_max, y_max, &c[4], &c[5]);
    transform_point(t, x,     y_max, &c[6], &c[7]);
    int mn_x = c[0], mx_x = c[0], mn_y = c[1], mx_y = c[1];
    for (int i = 2; i < 8; i += 2) {
        if (c[i]   < mn_x) mn_x = c[i];
        if (c[i]   > mx_x) mx_x = c[i];
        if (c[i+1] < mn_y) mn_y = c[i+1];
        if (c[i+1] > mx_y) mx_y = c[i+1];
    }
    return aabb_outside_fb(mn_x - sw, mn_y - sw, mx_x + sw, mx_y + sw, fb_w, fb_h, use_clip, clip);
}

static inline bool node_culled_tri(VgTransformFixed t,
                                   int16_t x1, int16_t y1,
                                   int16_t x2, int16_t y2,
                                   int16_t x3, int16_t y3, int sw,
                                   int fb_w, int fb_h, bool use_clip, VgClipRect clip) {
    int ax, ay, bx, by, cx, cy;
    transform_point(t, x1, y1, &ax, &ay);
    transform_point(t, x2, y2, &bx, &by);
    transform_point(t, x3, y3, &cx, &cy);
    int mn_x = ax, mx_x = ax, mn_y = ay, mx_y = ay;
    if (bx < mn_x) {
        mn_x = bx;
    }
    if (bx > mx_x) {
        mx_x = bx;
    }
    if (by < mn_y) {
        mn_y = by;
    }
    if (by > mx_y) {
        mx_y = by;
    }
    if (cx < mn_x) {
        mn_x = cx;
    }
    if (cx > mx_x) {
        mx_x = cx;
    }
    if (cy < mn_y) {
        mn_y = cy;
    }
    if (cy > mx_y) {
        mx_y = cy;
    }
    return aabb_outside_fb(mn_x - sw, mn_y - sw, mx_x + sw, mx_y + sw, fb_w, fb_h, use_clip, clip);
}

static bool world_aabb_from_points(VgTransformFixed t,
                                   const VgPoint *points,
                                   size_t point_count,
                                   VgAabb *out_box) {
    if (!points || point_count == 0u || !out_box) {
        return false;
    }
    int wx = 0;
    int wy = 0;
    transform_point(t, points[0].x, points[0].y, &wx, &wy);
    out_box->min_x = wx;
    out_box->max_x = wx;
    out_box->min_y = wy;
    out_box->max_y = wy;
    for (size_t i = 1; i < point_count; i++) {
        transform_point(t, points[i].x, points[i].y, &wx, &wy);
        if (wx < out_box->min_x) out_box->min_x = wx;
        if (wx > out_box->max_x) out_box->max_x = wx;
        if (wy < out_box->min_y) out_box->min_y = wy;
        if (wy > out_box->max_y) out_box->max_y = wy;
    }
    return true;
}

static void capture_entity_world_aabb_points(ID entity_id,
                                             VgTransformFixed world_t,
                                             const VgPoint *points,
                                             size_t point_count) {
    if (!entity_id || !points || point_count == 0u) {
        return;
    }
    VgAabb world_aabb = {0};
    if (!world_aabb_from_points(world_t, points, point_count, &world_aabb)) {
        return;
    }
    vg_rendered_state_capture_record_entity_aabb((uintptr_t)entity_id, world_aabb);
}

/*
 * Collision proxies can be visually hidden, but they still need current world
 * bounds in the rendered-state snapshot.
 */
static bool leaf_visible_after_aabb_capture(ID entity_id,
                                            VgTransformFixed world_t,
                                            const VgPoint *points,
                                            size_t point_count,
                                            bool visible) {
    capture_entity_world_aabb_points(entity_id, world_t, points, point_count);
    return visible;
}

static int32_t fixed_payload_raw(ID v) {
    return (int32_t)((intptr_t)v >> TAG_BITS);
}

static int32_t fixed_raw_to_int_trunc_zero(int32_t raw) {
    return raw / CLJ_FIXED_SCALE;
}

static bool id_to_bool_default(ID v, bool default_value) {
    if (!v) {
        return default_value;
    }
    return v != clj_false;
}

/** Decode a Clojure numeric value to raw Q19.13 fixed-point (same as CLJ_FIXED_SCALE). */
static int32_t id_to_fixed_raw_default(ID v, int32_t default_value) {
    if (!v) return default_value;
    if (is_fixnum(v)) {
        return (int32_t)as_fixnum(v) << CLJ_FIXED_FRAC_BITS;
    }
    if (is_fixed(v)) {
        return fixed_payload_raw(v);
    }
    return default_value;
}

static int16_t id_to_i16_default(ID v, int16_t default_value) {
    if (!v) {
        return default_value;
    }
    if (is_fixnum(v)) {
        return (int16_t)as_fixnum(v);
    }
    if (is_fixed(v)) {
        return (int16_t)fixed_raw_to_int_trunc_zero(fixed_payload_raw(v));
    }
    return default_value;
}

static uint16_t id_to_u16_default(ID v, uint16_t default_value) {
    if (!v) {
        return default_value;
    }
    if (is_fixnum(v)) {
        return (uint16_t)as_fixnum(v);
    }
    if (is_fixed(v)) {
        return (uint16_t)fixed_raw_to_int_trunc_zero(fixed_payload_raw(v));
    }
    return default_value;
}

static uint8_t id_to_u8_default(ID v, uint8_t default_value) {
    if (!v) {
        return default_value;
    }
    if (is_fixnum(v)) {
        return (uint8_t)as_fixnum(v);
    }
    if (is_fixed(v)) {
        return (uint8_t)fixed_raw_to_int_trunc_zero(fixed_payload_raw(v));
    }
    return default_value;
}

static const char *id_to_text_cstr(ID v) {
    if (!v) {
        return "";
    }
    CljType v_tag = TAG(v);
    if (v_tag == CLJ_STRING) {
        const char *s = string_data(v);
        return s ? s : "";
    }
    if (v_tag == CLJ_SYMBOL) {
        CljSymbol *sym = as_symbol(v);
        if (sym && sym->cname) {
            return sym->cname;
        }
    }
    return "";
}

static bool id_is_vector(ID v) {
    if (!v) {
        return false;
    }
    CljType tag = TAG(v);
    return tag == CLJ_VECTOR_PERSISTENT || tag == CLJ_VECTOR_TRANSIENT;
}

static uint32_t id_to_u32_default(ID v, uint32_t default_value) {
    if (!v) {
        return default_value;
    }
    if (is_fixnum(v)) {
        int32_t value = as_fixnum(v);
        if (value <= 0) {
            return 0u;
        }
        return (uint32_t)value;
    }
    if (is_fixed(v)) {
        int32_t raw = fixed_payload_raw(v);
        int32_t value = fixed_raw_to_int_trunc_zero(raw);
        if (value <= 0) {
            return 0u;
        }
        return (uint32_t)value;
    }
    return default_value;
}

static bool id_to_fixed_raw(ID v, int32_t *out_raw) {
    if (!v || !out_raw) {
        return false;
    }
    if (is_fixnum(v)) {
        *out_raw = (int32_t)as_fixnum(v) << CLJ_FIXED_FRAC_BITS;
        return true;
    }
    if (is_fixed(v)) {
        *out_raw = fixed_payload_raw(v);
        return true;
    }
    return false;
}

static ID fixed_raw_to_id(int32_t raw) {
    if ((raw % CLJ_FIXED_SCALE) == 0) {
        int32_t integer = raw / CLJ_FIXED_SCALE;
        if (integer >= FIXNUM_MIN && integer <= FIXNUM_MAX) {
            return fixnum(integer);
        }
    }
    return (ID)(((intptr_t)raw << TAG_BITS) | TAG_FIXED);
}

static bool timeline_keyframe_at(ID keyframes_obj, unsigned int index, uint32_t *out_time_ms, ID *out_value) {
    if (!id_is_vector(keyframes_obj)) {
        return false;
    }
    CljPersistentVector *keyframes = as_vector(keyframes_obj);
    if (!keyframes || index >= vector_count(keyframes)) {
        return false;
    }
    ID entry = vector_nth(keyframes, index);
    if (!id_is_vector(entry)) {
        return false;
    }
    CljPersistentVector *pair = as_vector(entry);
    if (!pair || vector_count(pair) < 2) {
        return false;
    }
    if (out_time_ms) {
        *out_time_ms = id_to_u32_default(vector_nth(pair, 0), 0u);
    }
    if (out_value) {
        *out_value = vector_nth(pair, 1);
    }
    return true;
}

static uint32_t timeline_phase_ms(uint32_t now_ms, uint32_t period_ms, bool loop) {
    if (loop && period_ms > 0u) {
        return now_ms % period_ms;
    }
    return now_ms;
}

typedef struct {
    bool is_timeline;
    uint16_t step_index;
    uint16_t keyframe_count;
    uint32_t phase_ms;
    uint32_t period_ms;
    bool loop;
} TimelineResolveInfo;

static void mark_has_animation(bool *out_has_animation) {
    if (out_has_animation) {
        *out_has_animation = true;
    }
}

static bool timeline_record_fields(ID timeline_obj,
                                   const VgRecordSchema *sc,
                                   ID *out_keyframes,
                                   ID *out_loop) {
    if (!timeline_obj || TAG(timeline_obj) != CLJ_RECORD || !sc || !out_keyframes || !out_loop) {
        return false;
    }

    if (record_type_hash(timeline_obj) == sc->h_timeline) {
        Timeline *timeline = (Timeline *)timeline_obj;
        *out_keyframes = timeline->keyframes;
        *out_loop = timeline->loop;
        return true;
    }

    const VgRecordKeys *keys = tiny_fx_gfx_record_keys();
    if (!keys || !keys->k_keyframes || !keys->k_loop) {
        return false;
    }

    ID keyframes = tiny_fx_gfx_get_field(timeline_obj, keys->k_keyframes, NOT_FOUND);
    if (keyframes == NOT_FOUND) {
        return false;
    }

    *out_keyframes = keyframes;
    *out_loop = tiny_fx_gfx_get_field(timeline_obj, keys->k_loop, NULL);
    return true;
}

static ID resolve_timeline_value_with_info(ID raw_value,
                                           uint32_t now_ms,
                                           const VgRecordSchema *sc,
                                           TimelineResolveInfo *out_info,
                                           bool *out_has_animation) {
    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
    }
    ID keyframes_obj = NULL;
    ID loop_obj = NULL;
    if (!raw_value || !sc || !timeline_record_fields(raw_value, sc, &keyframes_obj, &loop_obj)) {
        return raw_value;
    }
    mark_has_animation(out_has_animation);
    if (!id_is_vector(keyframes_obj)) {
        return NULL;
    }
    CljPersistentVector *keyframes = as_vector(keyframes_obj);
    unsigned int count = keyframes ? vector_count(keyframes) : 0u;
    if (count == 0u) {
        return NULL;
    }
    if (out_info) {
        out_info->is_timeline = true;
        out_info->keyframe_count = (uint16_t)count;
    }

    uint32_t first_ms = 0u;
    ID first_value = NULL;
    if (!timeline_keyframe_at(keyframes_obj, 0u, &first_ms, &first_value)) {
        return NULL;
    }
    if (count == 1u) {
        if (out_info) {
            out_info->step_index = 0u;
            out_info->phase_ms = 0u;
            out_info->period_ms = first_ms;
            out_info->loop = false;
        }
        return first_value;
    }

    uint32_t last_ms = first_ms;
    ID last_value = first_value;
    for (unsigned int i = 1; i < count; i++) {
        uint32_t frame_ms = 0u;
        ID frame_value = NULL;
        if (!timeline_keyframe_at(keyframes_obj, i, &frame_ms, &frame_value)) {
            continue;
        }
        last_ms = frame_ms;
        last_value = frame_value;
    }

    bool loop = id_to_bool_default(loop_obj, false);
    uint32_t phase_ms = timeline_phase_ms(now_ms, last_ms, loop);
    if (out_info) {
        out_info->phase_ms = phase_ms;
        out_info->period_ms = last_ms;
        out_info->loop = loop;
    }
    if (!loop && phase_ms >= last_ms) {
        if (out_info) {
            out_info->phase_ms = last_ms;
            out_info->step_index = (count > 0u) ? (uint16_t)(count - 1u) : 0u;
        }
        return last_value;
    }
    if (phase_ms <= first_ms) {
        if (out_info) {
            out_info->phase_ms = first_ms;
            out_info->step_index = 0u;
        }
        return first_value;
    }

    uint32_t prev_ms = first_ms;
    ID prev_value = first_value;
    unsigned int prev_index = 0u;
    for (unsigned int i = 1; i < count; i++) {
        uint32_t curr_ms = 0u;
        ID curr_value = NULL;
        if (!timeline_keyframe_at(keyframes_obj, i, &curr_ms, &curr_value)) {
            continue;
        }
        if (phase_ms < curr_ms) {
            if (out_info) {
                out_info->step_index = (uint16_t)prev_index;
            }
            if (curr_ms <= prev_ms) {
                return prev_value;
            }
            int32_t from_raw = 0;
            int32_t to_raw = 0;
            if (id_to_fixed_raw(prev_value, &from_raw) && id_to_fixed_raw(curr_value, &to_raw)) {
                uint32_t elapsed = phase_ms - prev_ms;
                uint32_t duration = curr_ms - prev_ms;
                int32_t progress = vg_anim_progress_q13(elapsed, duration);
                int32_t interpolated = vg_anim_lerp_q13(from_raw, to_raw, progress);
                return fixed_raw_to_id(interpolated);
            }
            return prev_value;
        }
        prev_ms = curr_ms;
        prev_value = curr_value;
        prev_index = i;
    }
    if (out_info) {
        out_info->step_index = (uint16_t)prev_index;
    }
    return prev_value;
}

static ID resolve_timeline_value(ID raw_value,
                                 uint32_t now_ms,
                                 const VgRecordSchema *sc,
                                 bool *out_has_animation) {
    return resolve_timeline_value_with_info(raw_value, now_ms, sc, NULL, out_has_animation);
}

static bool timeline_transform_keyframe_at(ID keyframes_obj,
                                           unsigned int index,
                                           uint32_t *out_time_ms,
                                           Transform **out_transform,
                                           const VgRecordSchema *sc) {
    ID value = NULL;
    if (!timeline_keyframe_at(keyframes_obj, index, out_time_ms, &value)) {
        return false;
    }
    if (!value || TAG(value) != CLJ_RECORD || record_type_hash(value) != sc->h_transform) {
        return false;
    }
    if (out_transform) {
        *out_transform = (Transform *)value;
    }
    return true;
}

static VgAnimEase timeline_ease_kind(ID timeline_obj) {
    scene_ensure_timeline_keyword_cache();
    ID ease_obj = tiny_fx_gfx_get_field(timeline_obj, g_kw_timeline_ease, NULL);
    if (!ease_obj) {
        ease_obj = tiny_fx_gfx_get_field(timeline_obj, g_kw_timeline_easing, NULL);
    }
    if (!ease_obj) {
        return VG_ANIM_EASE_LINEAR;
    }

    const char *name = NULL;
    if (TAG(ease_obj) == CLJ_SYMBOL) {
        CljSymbol *sym = as_symbol(ease_obj);
        name = sym ? sym->cname : NULL;
    } else if (TAG(ease_obj) == CLJ_STRING) {
        name = string_data(ease_obj);
    }
    if (!name) {
        return VG_ANIM_EASE_LINEAR;
    }
    while (*name == ':') {
        name++;
    }
    if (strcmp(name, "in-quad") == 0) return VG_ANIM_EASE_IN_QUAD;
    if (strcmp(name, "out-quad") == 0) return VG_ANIM_EASE_OUT_QUAD;
    if (strcmp(name, "in-out-quad") == 0) return VG_ANIM_EASE_IN_OUT_QUAD;
    if (strcmp(name, "out-cubic") == 0) return VG_ANIM_EASE_OUT_CUBIC;
    return VG_ANIM_EASE_LINEAR;
}

static VgTransformFixed interpolate_transform_keyframes(Transform *from,
                                                        Transform *to,
                                                        uint32_t from_ms,
                                                        uint32_t to_ms,
                                                        uint32_t phase_ms,
                                                        VgAnimEase ease) {
    if (!from || !to || to_ms <= from_ms) {
        return vg_transform_fixed_identity();
    }
    uint32_t elapsed = phase_ms - from_ms;
    uint32_t duration = to_ms - from_ms;
    int32_t progress = vg_anim_ease_q13(ease, vg_anim_progress_q13(elapsed, duration));

    int32_t from_tx = id_to_fixed_raw_default(from->tx, 0);
    int32_t from_ty = id_to_fixed_raw_default(from->ty, 0);
    int32_t from_sx = id_to_fixed_raw_default(from->sx, VG_SCALE_ONE);
    int32_t from_sy = id_to_fixed_raw_default(from->sy, VG_SCALE_ONE);
    int32_t from_rot = id_to_fixed_raw_default(from->rot, 0);

    int32_t to_tx = id_to_fixed_raw_default(to->tx, 0);
    int32_t to_ty = id_to_fixed_raw_default(to->ty, 0);
    int32_t to_sx = id_to_fixed_raw_default(to->sx, VG_SCALE_ONE);
    int32_t to_sy = id_to_fixed_raw_default(to->sy, VG_SCALE_ONE);
    int32_t to_rot = id_to_fixed_raw_default(to->rot, 0);

    VgTransform interpolated = vg_transform_identity();
    interpolated.tx = (int16_t)fixed_raw_to_int_trunc_zero(vg_anim_lerp_q13(from_tx, to_tx, progress));
    interpolated.ty = (int16_t)fixed_raw_to_int_trunc_zero(vg_anim_lerp_q13(from_ty, to_ty, progress));
    interpolated.sx = vg_anim_lerp_q13(from_sx, to_sx, progress);
    interpolated.sy = vg_anim_lerp_q13(from_sy, to_sy, progress);
    interpolated.rot_deg = (int16_t)fixed_raw_to_int_trunc_zero(vg_anim_lerp_q13(from_rot, to_rot, progress));
    return vg_transform_fixed_from_transform(interpolated);
}

static VgTransformFixed decode_transform_record_fixed(const Transform *tr) {
    if (!tr) {
        return vg_transform_fixed_identity();
    }
    VgTransform t = vg_transform_identity();
    t.tx = id_to_i16_default(tr->tx, 0);
    t.ty = id_to_i16_default(tr->ty, 0);
    t.sx = id_to_fixed_raw_default(tr->sx, VG_SCALE_ONE);
    t.sy = id_to_fixed_raw_default(tr->sy, VG_SCALE_ONE);
    t.rot_deg = id_to_i16_default(tr->rot, 0);
    return vg_transform_fixed_from_transform(t);
}

static VgTransformFixed decode_transform_fixed_with_info(ID obj,
                                                         uint32_t now_ms,
                                                         const VgRecordSchema *s,
                                                         TimelineResolveInfo *out_info) {
    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
    }
    if (!obj) {
        return vg_transform_fixed_identity();
    }
    ID keyframes_obj = NULL;
    ID loop_obj = NULL;
    if (timeline_record_fields(obj, s, &keyframes_obj, &loop_obj)) {
        VgAnimEase ease = timeline_ease_kind(obj);
        if (!id_is_vector(keyframes_obj)) {
            return vg_transform_fixed_identity();
        }
        CljPersistentVector *keyframes = as_vector(keyframes_obj);
        unsigned int count = keyframes ? vector_count(keyframes) : 0u;
        if (count == 0u) {
            return vg_transform_fixed_identity();
        }
        if (out_info) {
            out_info->is_timeline = true;
            out_info->keyframe_count = (uint16_t)count;
        }

        uint32_t first_ms = 0u;
        Transform *first_transform = NULL;
        if (!timeline_transform_keyframe_at(keyframes_obj, 0u, &first_ms, &first_transform, s)) {
            return vg_transform_fixed_identity();
        }
        if (count == 1u) {
            if (out_info) {
                out_info->step_index = 0u;
                out_info->phase_ms = 0u;
                out_info->period_ms = first_ms;
                out_info->loop = false;
            }
            return decode_transform_record_fixed(first_transform);
        }

        uint32_t last_ms = first_ms;
        Transform *last_transform = first_transform;
        for (unsigned int i = 1; i < count; i++) {
            uint32_t frame_ms = 0u;
            Transform *frame_transform = NULL;
            if (!timeline_transform_keyframe_at(keyframes_obj, i, &frame_ms, &frame_transform, s)) {
                continue;
            }
            last_ms = frame_ms;
            last_transform = frame_transform;
        }

        bool loop = id_to_bool_default(loop_obj, false);
        uint32_t phase_ms = timeline_phase_ms(now_ms, last_ms, loop);
        if (out_info) {
            out_info->phase_ms = phase_ms;
            out_info->period_ms = last_ms;
            out_info->loop = loop;
        }
        if (!loop && phase_ms >= last_ms) {
            if (out_info) {
                out_info->phase_ms = last_ms;
                out_info->step_index = (count > 0u) ? (uint16_t)(count - 1u) : 0u;
            }
            return decode_transform_record_fixed(last_transform);
        }
        if (phase_ms <= first_ms) {
            if (out_info) {
                out_info->phase_ms = first_ms;
                out_info->step_index = 0u;
            }
            return decode_transform_record_fixed(first_transform);
        }

        uint32_t prev_ms = first_ms;
        Transform *prev_transform = first_transform;
        unsigned int prev_index = 0u;
        for (unsigned int i = 1; i < count; i++) {
            uint32_t curr_ms = 0u;
            Transform *curr_transform = NULL;
            if (!timeline_transform_keyframe_at(keyframes_obj, i, &curr_ms, &curr_transform, s)) {
                continue;
            }
            if (phase_ms < curr_ms) {
                if (out_info) {
                    out_info->step_index = (uint16_t)prev_index;
                }
                return interpolate_transform_keyframes(prev_transform,
                                                       curr_transform,
                                                       prev_ms,
                                                       curr_ms,
                                                       phase_ms,
                                                       ease);
            }
            prev_ms = curr_ms;
            prev_transform = curr_transform;
            prev_index = i;
        }
        if (out_info) {
            out_info->step_index = (uint16_t)prev_index;
        }
        return decode_transform_record_fixed(prev_transform);
    }
    if (TAG(obj) == CLJ_RECORD && record_type_hash(obj) == s->h_transform) {
        return decode_transform_record_fixed((Transform *)obj);
    }
    return vg_transform_fixed_identity();
}

static ID node_style_field(ID node_obj, uint32_t h, const VgRecordSchema *s) {
    if (h == s->h_group)    return ((Group *)node_obj)->style;
    if (h == s->h_line)     return ((Line *)node_obj)->style;
    if (h == s->h_polyline) return ((Polyline *)node_obj)->style;
    if (h == s->h_rect)     return ((Rect *)node_obj)->style;
    if (h == s->h_tri)      return ((Tri *)node_obj)->style;
    if (h == s->h_vtext)    return ((VText *)node_obj)->style;
    return NULL;
}

static ID node_id_field(ID node_obj, uint32_t h, const VgRecordSchema *s) {
    if (h == s->h_group)    return ((Group *)node_obj)->id;
    if (h == s->h_line)     return ((Line *)node_obj)->id;
    if (h == s->h_polyline) return ((Polyline *)node_obj)->id;
    if (h == s->h_rect)     return ((Rect *)node_obj)->id;
    if (h == s->h_tri)      return ((Tri *)node_obj)->id;
    if (h == s->h_vtext)    return ((VText *)node_obj)->id;
    return NULL;
}

static ID node_visible_field(ID node_obj, uint32_t h, const VgRecordSchema *s) {
    if (h == s->h_group)    return ((Group *)node_obj)->visible;
    if (h == s->h_line)     return ((Line *)node_obj)->visible;
    if (h == s->h_polyline) return ((Polyline *)node_obj)->visible;
    if (h == s->h_rect)     return ((Rect *)node_obj)->visible;
    if (h == s->h_tri)      return ((Tri *)node_obj)->visible;
    if (h == s->h_vtext)    return ((VText *)node_obj)->visible;
    return NULL;
}

static void capture_timeline_for_entity_field(ID entity_id, VgRenderedField field, const TimelineResolveInfo *info) {
    if (!entity_id || !info || !info->is_timeline || field == VG_RENDERED_FIELD_NONE) {
        return;
    }
    VgRenderedTimelineSample sample;
    sample.step_index = info->step_index;
    sample.keyframe_count = info->keyframe_count;
    sample.phase_ms = info->phase_ms;
    sample.period_ms = info->period_ms;
    sample.loop = info->loop;
    vg_rendered_state_capture_record_timeline((uintptr_t)entity_id, field, sample);
}

static ID resolve_entity_field_value(ID entity_id,
                                     VgRenderedField field,
                                     ID raw_value,
                                     uint32_t now_ms,
                                     const VgRecordSchema *sc,
                                     bool *out_has_animation) {
    TimelineResolveInfo info;
    ID resolved = resolve_timeline_value_with_info(raw_value, now_ms, sc, &info, out_has_animation);
    capture_timeline_for_entity_field(entity_id, field, &info);
    return resolved;
}

static VgStyle decode_style(ID node_obj,
                            ID entity_id,
                            uint32_t node_h,
                            uint32_t now_ms,
                            const VgRecordSchema *sc,
                            bool *out_has_animation) {
    VgStyle st = vg_style_default();
    if (!node_obj) {
        return st;
    }
    ID style_obj = resolve_entity_field_value(entity_id,
                                              VG_RENDERED_FIELD_STYLE,
                                              node_style_field(node_obj, node_h, sc),
                                              now_ms,
                                              sc,
                                              out_has_animation);
    if (style_obj && TAG(style_obj) == CLJ_RECORD && record_type_hash(style_obj) == sc->h_style) {
        Style *sr = style_obj;
        st.stroke_color = id_to_u16_default(resolve_timeline_value(sr->stroke_color, now_ms, sc, out_has_animation),
                                            st.stroke_color);
        st.stroke_width = id_to_u8_default(resolve_timeline_value(sr->stroke_width, now_ms, sc, out_has_animation),
                                           st.stroke_width);
        st.has_fill = id_to_bool_default(resolve_timeline_value(sr->has_fill, now_ms, sc, out_has_animation),
                                         st.has_fill);
        st.fill_color = id_to_u16_default(resolve_timeline_value(sr->fill_color, now_ms, sc, out_has_animation),
                                          st.fill_color);
        st.has_bg_color = id_to_bool_default(resolve_timeline_value(sr->has_bg_color, now_ms, sc, out_has_animation),
                                             st.has_bg_color);
        st.bg_color = id_to_u16_default(resolve_timeline_value(sr->bg_color, now_ms, sc, out_has_animation),
                                        st.bg_color);
        st.visible = id_to_bool_default(resolve_timeline_value(sr->visible, now_ms, sc, out_has_animation),
                                        st.visible);
    }
    ID node_visible = resolve_entity_field_value(entity_id,
                                                 VG_RENDERED_FIELD_VISIBLE,
                                                 node_visible_field(node_obj, node_h, sc),
                                                 now_ms,
                                                 sc,
                                                 out_has_animation);
    if (node_visible) {
        st.visible = id_to_bool_default(node_visible, st.visible);
    }
    if (st.stroke_width == 0) {
        st.stroke_width = 1;
    }
    return st;
}

static bool render_record_node(ID node_obj,
                               ID entity_map,
                               const VgFlatSceneLookup *lookup,
                               VgTransformFixed parent_t,
                               VgFrameBuffer *fb,
                               bool use_clip,
                               VgClipRect clip_rect,
                               uint32_t now_ms,
                               bool *out_has_animation);

static bool decode_rect(ID obj, VgClipRect *out_rect, const VgRecordSchema *sc) {
    if (!obj || !out_rect) {
        return false;
    }
    if (id_is_vector(obj)) {
        CljPersistentVector *v = as_vector(obj);
        if (vector_count(v) < 4) {
            return false;
        }
        out_rect->x = id_to_i16_default(vector_nth(v, 0), 0);
        out_rect->y = id_to_i16_default(vector_nth(v, 1), 0);
        out_rect->w = id_to_i16_default(vector_nth(v, 2), 0);
        out_rect->h = id_to_i16_default(vector_nth(v, 3), 0);
        return !vg_clip_rect_is_empty(*out_rect);
    }
    if (TAG(obj) == CLJ_RECORD && record_type_hash(obj) == sc->h_rect) {
        Rect *r = obj;
        out_rect->x = id_to_i16_default(r->x, 0);
        out_rect->y = id_to_i16_default(r->y, 0);
        out_rect->w = id_to_i16_default(r->w, 0);
        out_rect->h = id_to_i16_default(r->h, 0);
        return !vg_clip_rect_is_empty(*out_rect);
    }
    return false;
}

static bool render_one_temp_node(const VgNode *node, VgTransformFixed world_t, VgFrameBuffer *fb, bool use_clip, VgClipRect clip_rect) {
    if (!node || !fb) {
        return false;
    }
    if (use_clip) {
        vg_render_node_fixed_clipped(node, world_t, fb, clip_rect);
    } else {
        vg_render_node_fixed(node, world_t, fb);
    }
    return true;
}

static bool render_polyline_record(ID node_obj,
                                   ID entity_id,
                                   VgTransformFixed world_t,
                                   VgStyle style,
                                   VgFrameBuffer *fb,
                                   bool use_clip,
                                   VgClipRect clip_rect,
                                   uint32_t now_ms,
                                   const VgRecordSchema *sc,
                                   bool *out_has_animation) {
    Polyline *pr = node_obj;
    ID pts = resolve_entity_field_value(entity_id, VG_RENDERED_FIELD_PTS, pr->pts, now_ms, sc, out_has_animation);
    ID closed = resolve_entity_field_value(entity_id,
                                           VG_RENDERED_FIELD_CLOSED,
                                           pr->closed,
                                           now_ms,
                                           sc,
                                           out_has_animation);
    if (!id_is_vector(pts)) {
        return true;
    }
    CljPersistentVector *pv = as_vector(pts);
    unsigned int n = vector_count(pv);
    if (n < 2 || n > GFX_FILL_MAX_VERTS) {
        return n < 2;
    }
    VgPoint points[GFX_FILL_MAX_VERTS];
    for (unsigned int i = 0; i < n; i++) {
        ID point = vector_nth(pv, i);
        if (!id_is_vector(point)) {
            return false;
        }
        CljPersistentVector *xy = as_vector(point);
        if (vector_count(xy) < 2) {
            return false;
        }
        points[i].x = id_to_i16_default(vector_nth(xy, 0), 0);
        points[i].y = id_to_i16_default(vector_nth(xy, 1), 0);
    }

    if (!leaf_visible_after_aabb_capture(entity_id, world_t, points, n, style.visible)) {
        return true;
    }

    int sw = style.stroke_width ? style.stroke_width : 1;
    {
        int mn_x = INT_MAX, mn_y = INT_MAX, mx_x = INT_MIN, mx_y = INT_MIN;
        for (unsigned int i = 0; i < n; i++) {
            int wx, wy;
            transform_point(world_t, points[i].x, points[i].y, &wx, &wy);
            if (wx < mn_x) {
                mn_x = wx;
            }
            if (wx > mx_x) {
                mx_x = wx;
            }
            if (wy < mn_y) {
                mn_y = wy;
            }
            if (wy > mx_y) {
                mx_y = wy;
            }
        }
        if (aabb_outside_fb(mn_x - sw, mn_y - sw, mx_x + sw, mx_y + sw,
                            fb->width, fb->height, use_clip, clip_rect))
            return true;
    }

    VgNode temp;
    memset(&temp, 0, sizeof(temp));
    temp.type = VG_NODE_POLYLINE;
    temp.has_transform = false;
    temp.style = style;
    temp.data.polyline.points = points;
    temp.data.polyline.point_count = (size_t)n;
    temp.data.polyline.closed = id_to_bool_default(closed, false);
    return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
}

static bool render_record_node(ID node_obj,
                               ID entity_map,
                               const VgFlatSceneLookup *lookup,
                               VgTransformFixed parent_t,
                               VgFrameBuffer *fb,
                               bool use_clip,
                               VgClipRect clip_rect,
                               uint32_t now_ms,
                               bool *out_has_animation) {
    if (!node_obj || TAG(node_obj) != CLJ_RECORD || !fb) {
        return false;
    }
    const VgRecordSchema *sc = tiny_fx_gfx_schema();
    uint32_t h = record_type_hash(node_obj);
    ID entity_id = node_id_field(node_obj, h, sc);

    ID local_t_obj = NULL;
    if      (h == sc->h_group)    local_t_obj = ((Group *)node_obj)->t;
    else if (h == sc->h_line)     local_t_obj = ((Line *)node_obj)->t;
    else if (h == sc->h_polyline) local_t_obj = ((Polyline *)node_obj)->t;
    else if (h == sc->h_rect)     local_t_obj = ((Rect *)node_obj)->t;
    else if (h == sc->h_tri)      local_t_obj = ((Tri *)node_obj)->t;
    else if (h == sc->h_vtext)    local_t_obj = ((VText *)node_obj)->t;

    VgTransformFixed world_t = parent_t;
    if (local_t_obj) {
        TimelineResolveInfo t_info;
        VgTransformFixed local_t = decode_transform_fixed_with_info(local_t_obj, now_ms, sc, &t_info);
        if (t_info.is_timeline) {
            mark_has_animation(out_has_animation);
        }
        capture_timeline_for_entity_field(entity_id, VG_RENDERED_FIELD_T, &t_info);
        world_t = vg_transform_fixed_compose(parent_t, local_t);
    }
    if (entity_id) {
        vg_rendered_state_capture_record_entity((uintptr_t)entity_id, world_t);
    }
    VgStyle style = decode_style(node_obj, entity_id, h, now_ms, sc, out_has_animation);

    if (h == sc->h_group) {
        if (!style.visible) {
            return true;
        }
        Group *group = node_obj;
        ID children = resolve_entity_field_value(entity_id,
                                                 VG_RENDERED_FIELD_CHILDREN,
                                                 group->children,
                                                 now_ms,
                                                 sc,
                                                 out_has_animation);
        if (!children || !id_is_vector(children)) {
            return true;
        }
        CljPersistentVector *vec = as_vector(children);
        unsigned int count = vector_count(vec);
        for (unsigned int i = 0; i < count; i++) {
            ID child_ref = vector_nth(vec, i);
            if (!child_ref) {
                continue;
            }
            ID child_node = child_ref;
            if (TAG(child_node) != CLJ_RECORD) {
                if (!entity_map || !is_map(entity_map)) {
                    return false;
                }
                child_node = vg_flat_scene_lookup_get(lookup, entity_map, child_ref);
                if (!child_node) {
                    return false;
                }
            }
            if (TAG(child_node) != CLJ_RECORD) {
                return false;
            }
            if (!render_record_node(child_node,
                                    entity_map,
                                    lookup,
                                    world_t,
                                    fb,
                                    use_clip,
                                    clip_rect,
                                    now_ms,
                                    out_has_animation)) {
                return false;
            }
        }
        return true;
    }

    int sw = style.stroke_width ? style.stroke_width : 1;
    int fb_w = fb->width, fb_h = fb->height;

    VgNode temp;
    memset(&temp, 0, sizeof(temp));
    temp.has_transform = false;
    temp.style = style;

    if (h == sc->h_line) {
        temp.type = VG_NODE_LINE;
        Line *line = node_obj;
        temp.data.line.x1 = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                         VG_RENDERED_FIELD_X1,
                                                                         line->x1,
                                                                         now_ms,
                                                                         sc,
                                                                         out_has_animation),
                                              0);
        temp.data.line.y1 = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                         VG_RENDERED_FIELD_Y1,
                                                                         line->y1,
                                                                         now_ms,
                                                                         sc,
                                                                         out_has_animation),
                                              0);
        temp.data.line.x2 = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                         VG_RENDERED_FIELD_X2,
                                                                         line->x2,
                                                                         now_ms,
                                                                         sc,
                                                                         out_has_animation),
                                              0);
        temp.data.line.y2 = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                         VG_RENDERED_FIELD_Y2,
                                                                         line->y2,
                                                                         now_ms,
                                                                         sc,
                                                                         out_has_animation),
                                              0);
        {
            VgPoint points[2] = {
                {.x = temp.data.line.x1, .y = temp.data.line.y1},
                {.x = temp.data.line.x2, .y = temp.data.line.y2},
            };
            if (!leaf_visible_after_aabb_capture(entity_id, world_t, points, 2u, style.visible)) {
                return true;
            }
        }
        if (node_culled_line(world_t, temp.data.line.x1, temp.data.line.y1,
                             temp.data.line.x2, temp.data.line.y2, sw,
                             fb_w, fb_h, use_clip, clip_rect))
            return true;
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (h == sc->h_rect) {
        temp.type = VG_NODE_RECT;
        Rect *rect = node_obj;
        temp.data.rect.x = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_X,
                                                                        rect->x,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        temp.data.rect.y = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_Y,
                                                                        rect->y,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        temp.data.rect.w = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_W,
                                                                        rect->w,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        temp.data.rect.h = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_H,
                                                                        rect->h,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        {
            int16_t x_max = (temp.data.rect.w > 0)
                                ? (int16_t)(temp.data.rect.x + temp.data.rect.w - 1)
                                : temp.data.rect.x;
            int16_t y_max = (temp.data.rect.h > 0)
                                ? (int16_t)(temp.data.rect.y + temp.data.rect.h - 1)
                                : temp.data.rect.y;
            VgPoint points[4] = {
                {.x = temp.data.rect.x, .y = temp.data.rect.y},
                {.x = x_max, .y = temp.data.rect.y},
                {.x = x_max, .y = y_max},
                {.x = temp.data.rect.x, .y = y_max},
            };
            if (!leaf_visible_after_aabb_capture(entity_id, world_t, points, 4u, style.visible)) {
                return true;
            }
        }
        if (node_culled_rect(world_t, temp.data.rect.x, temp.data.rect.y,
                             temp.data.rect.w, temp.data.rect.h, sw,
                             fb_w, fb_h, use_clip, clip_rect))
            return true;
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (h == sc->h_tri) {
        temp.type = VG_NODE_TRI;
        Tri *tri = node_obj;
        temp.data.tri.x1 = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_X1,
                                                                        tri->x1,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        temp.data.tri.y1 = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_Y1,
                                                                        tri->y1,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        temp.data.tri.x2 = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_X2,
                                                                        tri->x2,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        temp.data.tri.y2 = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_Y2,
                                                                        tri->y2,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        temp.data.tri.x3 = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_X3,
                                                                        tri->x3,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        temp.data.tri.y3 = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_Y3,
                                                                        tri->y3,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        {
            VgPoint points[3] = {
                {.x = temp.data.tri.x1, .y = temp.data.tri.y1},
                {.x = temp.data.tri.x2, .y = temp.data.tri.y2},
                {.x = temp.data.tri.x3, .y = temp.data.tri.y3},
            };
            if (!leaf_visible_after_aabb_capture(entity_id, world_t, points, 3u, style.visible)) {
                return true;
            }
        }
        if (node_culled_tri(world_t,
                            temp.data.tri.x1, temp.data.tri.y1,
                            temp.data.tri.x2, temp.data.tri.y2,
                            temp.data.tri.x3, temp.data.tri.y3, sw,
                            fb_w, fb_h, use_clip, clip_rect))
            return true;
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (h == sc->h_vtext) {
        temp.type = VG_NODE_VTEXT;
        VText *text = node_obj;
        temp.data.text.x = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_X,
                                                                        text->x,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        temp.data.text.y = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                        VG_RENDERED_FIELD_Y,
                                                                        text->y,
                                                                        now_ms,
                                                                        sc,
                                                                        out_has_animation),
                                             0);
        temp.data.text.scale = id_to_fixed_raw_default(resolve_entity_field_value(entity_id,
                                                                                   VG_RENDERED_FIELD_SCALE,
                                                                                   text->scale,
                                                                                   now_ms,
                                                                                   sc,
                                                                                   out_has_animation),
                                                       VG_SCALE_ONE);
        temp.data.text.rot_deg = id_to_i16_default(resolve_entity_field_value(entity_id,
                                                                              VG_RENDERED_FIELD_ROT,
                                                                              text->rot,
                                                                              now_ms,
                                                                              sc,
                                                                              out_has_animation),
                                                   0);
        temp.data.text.text = id_to_text_cstr(resolve_entity_field_value(entity_id,
                                                                         VG_RENDERED_FIELD_TEXT,
                                                                         text->text,
                                                                         now_ms,
                                                                         sc,
                                                                         out_has_animation));
        if (!style.visible) {
            return true;
        }
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (h == sc->h_polyline) {
        return render_polyline_record(node_obj,
                                      entity_id,
                                      world_t,
                                      style,
                                      fb,
                                      use_clip,
                                      clip_rect,
                                      now_ms,
                                      sc,
                                      out_has_animation);
    }
    return false;
}

static bool resolve_root_node(ID root_field,
                              ID index_field,
                              const VgFlatSceneLookup *lookup,
                              ID *out_root_node,
                              ID *out_entity_map) {
    if (out_root_node) {
        *out_root_node = NULL;
    }
    if (out_entity_map) {
        *out_entity_map = NULL;
    }
    if (!out_root_node) {
        return false;
    }
    if (!vg_scene_root_is_canonical(root_field) || !index_field || !is_map(index_field)) {
        return false;
    }
    if (out_entity_map) {
        *out_entity_map = index_field;
    }
    *out_root_node = vg_flat_scene_lookup_get(lookup, index_field, (ID)&sym_entity_root_data.sym);
    return *out_root_node != NULL;
}

static bool decode_scene_fields(ID scene_record, ID *out_root, ID *out_index, ID *out_clip, ID *out_erase) {
    if (!scene_record || TAG(scene_record) != CLJ_RECORD) return false;
    const VgRecordSchema *sc = tiny_fx_gfx_schema();
    uint32_t h = record_type_hash(scene_record);
    if (h == sc->h_frame_scene) {
        FrameScene *fs = scene_record;
        *out_root = fs->root;
        if (out_index) *out_index = fs->index;
        *out_clip = fs->clip_rect;
        if (out_erase) *out_erase = fs->erase_color;
        return true;
    }
    if (h == sc->h_scene) {
        Scene *s = scene_record;
        *out_root = s->root;
        if (out_index) *out_index = s->index;
        *out_clip = s->clip_rect;
        if (out_erase) *out_erase = s->erase_color;
        return true;
    }
    return false;
}

bool vg_render_scene_record_at_ms(ID scene_record, VgFrameBuffer *fb, uint32_t now_ms) {
    if (!scene_record || !fb || TAG(scene_record) != CLJ_RECORD) {
        return false;
    }

    ID root = NULL;
    ID index = NULL;
    ID clip_source = NULL;
    ID erase_source = NULL;
    if (!decode_scene_fields(scene_record, &root, &index, &clip_source, &erase_source)) {
        return false;
    }

    const VgRecordSchema *sc = tiny_fx_gfx_schema();
    ID resolved_root = resolve_timeline_value(root, now_ms, sc, NULL);
    ID resolved_clip_source = resolve_timeline_value(clip_source, now_ms, sc, NULL);
    VgClipRect effective_rect = {0, 0, 0, 0};
    bool has_effective_rect = decode_rect(resolved_clip_source, &effective_rect, sc);
    ID entity_map = NULL;
    ID root_node = NULL;
    VgFlatSceneLookup lookup = {0};
    if (!is_map(index) || !vg_flat_scene_lookup_build(index, &lookup)) {
        return false;
    }
    if (!resolve_root_node(resolved_root, index, &lookup, &root_node, &entity_map)) {
        return false;
    }

    ID resolved_erase = resolve_timeline_value(erase_source, now_ms, sc, NULL);
    if (has_effective_rect) {
        uint16_t erase_color = id_to_u16_default(resolved_erase, 0x0000u);
        vg_framebuffer_clear_rect(fb, effective_rect, erase_color);
    }

    if (!root_node) {
        return true;
    }
    return render_record_node(root_node,
                              entity_map,
                              &lookup,
                              vg_transform_fixed_identity(),
                              fb,
                              has_effective_rect,
                              effective_rect,
                              now_ms,
                              NULL);
}

bool vg_render_scene_record(ID scene_record, VgFrameBuffer *fb) {
    return vg_render_scene_record_at_ms(scene_record, fb, platform_current_time_ms());
}

static bool render_scene_record_clipped_at_ms_internal(ID scene_record,
                                                       VgFrameBuffer *fb,
                                                       VgClipRect clip_rect,
                                                       uint32_t now_ms,
                                                       bool *out_has_animation) {
    if (!scene_record || !fb || TAG(scene_record) != CLJ_RECORD) {
        return false;
    }

    ID root = NULL;
    ID index = NULL;
    ID clip_source = NULL;
    if (!decode_scene_fields(scene_record, &root, &index, &clip_source, NULL)) {
        return false;
    }

    const VgRecordSchema *sc = tiny_fx_gfx_schema();
    ID resolved_root = resolve_timeline_value(root, now_ms, sc, out_has_animation);
    ID resolved_clip_source = resolve_timeline_value(clip_source, now_ms, sc, out_has_animation);
    VgClipRect effective_clip = clip_rect;
    VgClipRect scene_clip = {0, 0, 0, 0};
    bool has_scene_clip = decode_rect(resolved_clip_source, &scene_clip, sc);
    if (has_scene_clip) {
        if (!vg_clip_rect_intersect(clip_rect, scene_clip, &effective_clip)) {
            return true;
        }
    }
    ID entity_map = NULL;
    ID root_node = NULL;
    VgFlatSceneLookup lookup = {0};
    if (is_map(resolved_root) && !vg_flat_scene_lookup_build(resolved_root, &lookup)) {
        return false;
    }
    if (!resolve_root_node(resolved_root, index, &lookup, &root_node, &entity_map)) {
        return false;
    }
    if (!root_node) {
        return true;
    }
    return render_record_node(root_node,
                              entity_map,
                              &lookup,
                              vg_transform_fixed_identity(),
                              fb,
                              true,
                              effective_clip,
                              now_ms,
                              out_has_animation);
}

bool vg_render_scene_record_clipped_at_ms(ID scene_record, VgFrameBuffer *fb, VgClipRect clip_rect, uint32_t now_ms) {
    return render_scene_record_clipped_at_ms_internal(scene_record, fb, clip_rect, now_ms, NULL);
}

bool vg_render_scene_record_clipped(ID scene_record, VgFrameBuffer *fb, VgClipRect clip_rect) {
    return vg_render_scene_record_clipped_at_ms(scene_record, fb, clip_rect, platform_current_time_ms());
}

bool vg_decode_frame_slot_record(ID frame_scene_record, VgRenderSlot *out_slot) {
    if (!frame_scene_record || !out_slot || TAG(frame_scene_record) != CLJ_RECORD) {
        return false;
    }
    const VgRecordSchema *sc = tiny_fx_gfx_schema();
    if (record_type_hash(frame_scene_record) != sc->h_frame_scene) {
        return false;
    }

    FrameScene *scene = frame_scene_record;
    VgClipRect clip = {0, 0, 0, 0};
    if (!decode_rect(scene->clip_rect, &clip, sc)) {
        return false;
    }

    if (!vg_scene_root_is_canonical(scene->root) || !scene->index || !is_map(scene->index)) {
        return false;
    }
    VgFlatSceneLookup lookup = {0};
    if (!vg_flat_scene_lookup_build(scene->index, &lookup)) {
        return false;
    }
    ID root_node = vg_flat_scene_lookup_get(&lookup, scene->index, (ID)&sym_entity_root_data.sym);
    if (!root_node) {
        return false;
    }
    out_slot->root = root_node;
    out_slot->clip_rect = clip;
    out_slot->z = id_to_i16_default(scene->z, 0);
    out_slot->visible = id_to_bool_default(scene->visible, true);
    out_slot->opaque = id_to_bool_default(scene->opaque, true);
    out_slot->clear_color = id_to_u16_default(scene->erase_color, 0x0000u);
    out_slot->guard_px = id_to_u8_default(scene->guard_px, 0);
    return true;
}

static uint32_t clip_rect_area_on_framebuffer(VgClipRect rect, const VgFrameBuffer *fb) {
    if (!fb || vg_clip_rect_is_empty(rect) || fb->width <= 0 || fb->height <= 0) {
        return 0u;
    }
    int x0 = (rect.x < 0) ? 0 : rect.x;
    int y0 = (rect.y < 0) ? 0 : rect.y;
    int x1 = (int)rect.x + (int)rect.w;
    int y1 = (int)rect.y + (int)rect.h;
    if (x1 > fb->width) {
        x1 = fb->width;
    }
    if (y1 > fb->height) {
        y1 = fb->height;
    }
    if (x1 <= x0 || y1 <= y0) {
        return 0u;
    }
    return (uint32_t)((uint32_t)(x1 - x0) * (uint32_t)(y1 - y0));
}

bool vg_render_frame_slot_record_at_ms(ID frame_scene_record,
                                       VgRenderSlotState *state,
                                       VgFrameBuffer *fb,
                                       uint32_t snapshot_id,
                                       uint32_t now_ms,
                                       bool force_render,
                                       uint32_t *out_dirty_pixels) {
    VgRenderFrameSlotResult result = {0};
    bool rendered = vg_render_frame_slot_record_result_at_ms(frame_scene_record,
                                                             state,
                                                             fb,
                                                             snapshot_id,
                                                             now_ms,
                                                             force_render,
                                                             &result);
    if (out_dirty_pixels) {
        *out_dirty_pixels = result.dirty_pixels;
    }
    return rendered;
}

bool vg_render_frame_slot_record_result_at_ms(ID frame_scene_record,
                                              VgRenderSlotState *state,
                                              VgFrameBuffer *fb,
                                              uint32_t snapshot_id,
                                              uint32_t now_ms,
                                              bool force_render,
                                              VgRenderFrameSlotResult *out_result) {
    VgRenderSlot slot;
    if (out_result) {
        memset(out_result, 0, sizeof(*out_result));
    }
    if (!state || !fb || !vg_decode_frame_slot_record(frame_scene_record, &slot)) {
        return false;
    }

    bool props_changed = !state->initialized ||
                         state->last_visible != slot.visible ||
                         state->last_opaque != slot.opaque ||
                         state->last_clear_color != slot.clear_color ||
                         state->last_guard_px != slot.guard_px ||
                         !vg_clip_rect_equal(state->last_clip_rect, slot.clip_rect);
    bool snapshot_changed = !state->initialized || state->snapshot_id != snapshot_id;
    if (!props_changed && !snapshot_changed && !force_render) {
        return false;
    }

    VgClipRect dirty = vg_clip_rect_expand(slot.clip_rect, slot.guard_px);
    if (state->initialized) {
        VgClipRect prev = vg_clip_rect_expand(state->last_clip_rect, state->last_guard_px);
        dirty = vg_clip_rect_union(prev, dirty);
    }
    uint32_t dirty_pixels = clip_rect_area_on_framebuffer(dirty, fb);
    vg_framebuffer_clear_rect(fb, dirty, slot.clear_color);

    bool slot_has_animation = false;
    if (slot.visible) {
        (void)render_scene_record_clipped_at_ms_internal(frame_scene_record,
                                                         fb,
                                                         slot.clip_rect,
                                                         now_ms,
                                                         &slot_has_animation);
    }

    state->initialized = true;
    state->has_animation = slot_has_animation;
    state->snapshot_id = snapshot_id;
    state->last_clip_rect = slot.clip_rect;
    state->last_visible = slot.visible;
    state->last_opaque = slot.opaque;
    state->last_clear_color = slot.clear_color;
    state->last_guard_px = slot.guard_px;
    if (out_result) {
        out_result->rendered = true;
        out_result->has_animation = slot_has_animation;
        out_result->dirty_rect = dirty;
        out_result->dirty_pixels = dirty_pixels;
    }
    return true;
}

bool vg_render_frame_slot_record_if_changed_at_ms(ID frame_scene_record,
                                                  VgRenderSlotState *state,
                                                  VgFrameBuffer *fb,
                                                  uint32_t snapshot_id,
                                                  uint32_t now_ms,
                                                  uint32_t *out_dirty_pixels) {
    return vg_render_frame_slot_record_at_ms(frame_scene_record,
                                             state,
                                             fb,
                                             snapshot_id,
                                             now_ms,
                                             false,
                                             out_dirty_pixels);
}

bool vg_render_frame_slot_record_if_changed(ID frame_scene_record,
                                            VgRenderSlotState *state,
                                            VgFrameBuffer *fb,
                                            uint32_t snapshot_id,
                                            uint32_t *out_dirty_pixels) {
    return vg_render_frame_slot_record_if_changed_at_ms(frame_scene_record,
                                                         state,
                                                         fb,
                                                         snapshot_id,
                                                         platform_current_time_ms(),
                                                         out_dirty_pixels);
}
