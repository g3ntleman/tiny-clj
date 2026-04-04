#include "rendered_state_snapshot.h"

#include <stdatomic.h>
#include <string.h>

#include "memory.h"  /* CLJ_HOST_CALLOC / REALLOC / FREE */

typedef struct {
    bool active;
    uint8_t slot_index;
} RenderedCaptureContext;

static RenderedCaptureContext g_capture_ctx = {0};

/* --- Timeline Overlay (dynamic, heap-allocated) --- */

#define OVERLAY_INITIAL_ENTITY_CAPACITY 8u
#define OVERLAY_INITIAL_TIMELINE_CAPACITY 16u

typedef struct {
    uintptr_t entity_id_bits;
    VgTransformFixed world_t;
    bool has_world_aabb;
    VgAabb world_aabb;
    uint32_t content_signature;
} OverlayEntityRow;

typedef struct {
    uintptr_t entity_id_bits;
    uint8_t field;
    uint8_t _pad;
    VgRenderedTimelineSample sample;
} OverlayTimelineRow;

typedef struct {
    uint32_t snapshot_generation;
    uint32_t frame_time_ms;
    uint16_t entity_count;
    uint16_t entity_capacity;
    OverlayEntityRow *entities;
    uint16_t timeline_count;
    uint16_t timeline_capacity;
    OverlayTimelineRow *timelines;
} OverlayBuffer;

typedef struct {
    OverlayBuffer buffers[2];
    atomic_uint active_buffer_index;
} OverlaySlot;

static OverlaySlot *g_overlay_slots = NULL;
static uint8_t g_overlay_slot_count = 0;

static OverlayBuffer *overlay_write_buf(void) {
    if (!g_overlay_slots || !g_capture_ctx.active ||
        g_capture_ctx.slot_index >= g_overlay_slot_count) {
        return NULL;
    }
    OverlaySlot *slot = &g_overlay_slots[g_capture_ctx.slot_index];
    unsigned int active = atomic_load_explicit(&slot->active_buffer_index, memory_order_acquire);
    uint8_t write_idx = (active == 0u) ? 1u : 0u;
    return &slot->buffers[write_idx];
}

static int overlay_find_entity_row(const OverlayBuffer *buf, uintptr_t entity_id_bits) {
    for (uint16_t i = 0; i < buf->entity_count; i++) {
        if (buf->entities[i].entity_id_bits == entity_id_bits) {
            return (int)i;
        }
    }
    return -1;
}

static int overlay_find_timeline_row(const OverlayBuffer *buf,
                                     uintptr_t entity_id_bits,
                                     uint8_t field) {
    for (uint16_t i = 0; i < buf->timeline_count; i++) {
        if (buf->timelines[i].entity_id_bits == entity_id_bits &&
            buf->timelines[i].field == field) {
            return (int)i;
        }
    }
    return -1;
}

static bool overlay_ensure_entity_capacity(OverlayBuffer *buf) {
    if (buf->entity_count < buf->entity_capacity) {
        return true;
    }
    uint16_t new_cap = buf->entity_capacity
        ? (uint16_t)(buf->entity_capacity * 2u) : OVERLAY_INITIAL_ENTITY_CAPACITY;
    OverlayEntityRow *p = (OverlayEntityRow *)CLJ_HOST_REALLOC(
        buf->entities, (size_t)new_cap * sizeof(OverlayEntityRow));
    if (!p) { return false; }
    buf->entities = p;
    buf->entity_capacity = new_cap;
    return true;
}

static bool overlay_ensure_timeline_capacity(OverlayBuffer *buf) {
    if (buf->timeline_count < buf->timeline_capacity) {
        return true;
    }
    uint16_t new_cap = buf->timeline_capacity
        ? (uint16_t)(buf->timeline_capacity * 2u) : OVERLAY_INITIAL_TIMELINE_CAPACITY;
    OverlayTimelineRow *p = (OverlayTimelineRow *)CLJ_HOST_REALLOC(
        buf->timelines, (size_t)new_cap * sizeof(OverlayTimelineRow));
    if (!p) { return false; }
    buf->timelines = p;
    buf->timeline_capacity = new_cap;
    return true;
}

/* Ensure an entity row exists in the overlay; create if missing. */
static OverlayEntityRow *overlay_ensure_entity(OverlayBuffer *buf, uintptr_t entity_id_bits) {
    int idx = overlay_find_entity_row(buf, entity_id_bits);
    if (idx >= 0) { return &buf->entities[idx]; }
    if (!overlay_ensure_entity_capacity(buf)) { return NULL; }
    uint16_t i = buf->entity_count++;
    memset(&buf->entities[i], 0, sizeof(buf->entities[i]));
    buf->entities[i].entity_id_bits = entity_id_bits;
    buf->entities[i].world_t = vg_transform_fixed_identity();
    return &buf->entities[i];
}

static bool aabb_equal(VgAabb a, VgAabb b) {
    return a.min_x == b.min_x && a.min_y == b.min_y && a.max_x == b.max_x && a.max_y == b.max_y;
}

static VgClipRect clip_rect_from_aabb(VgAabb box, uint8_t padding_px) {
    int pad = (int)padding_px;
    int x0 = box.min_x - pad;
    int y0 = box.min_y - pad;
    int x1 = box.max_x + pad + 1;
    int y1 = box.max_y + pad + 1;
    VgClipRect rect = {
        .x = (int16_t)x0,
        .y = (int16_t)y0,
        .w = (int16_t)(x1 - x0),
        .h = (int16_t)(y1 - y0),
    };
    return rect;
}

static bool append_dirty_aabb_rect(VgClipRect *io_dirty_rect,
                                   bool *io_have_dirty_rect,
                                   VgAabb box,
                                   uint8_t padding_px,
                                   VgClipRect clip_rect) {
    VgClipRect rect = clip_rect_from_aabb(box, padding_px);
    VgClipRect clipped = {0};
    if (!vg_clip_rect_intersect(rect, clip_rect, &clipped)) {
        return false;
    }
    if (*io_have_dirty_rect) {
        *io_dirty_rect = vg_clip_rect_union(*io_dirty_rect, clipped);
    } else {
        *io_dirty_rect = clipped;
        *io_have_dirty_rect = true;
    }
    return true;
}

static bool append_dirty_aabb_rect_leaf(VgClipRect *out_rects,
                                        size_t out_capacity,
                                        size_t *io_count,
                                        VgAabb box,
                                        uint8_t padding_px,
                                        VgClipRect clip_rect) {
    if (!out_rects || !io_count || *io_count >= out_capacity) {
        return false;
    }
    VgClipRect rect = clip_rect_from_aabb(box, padding_px);
    VgClipRect clipped = {0};
    if (!vg_clip_rect_intersect(rect, clip_rect, &clipped)) {
        return false;
    }
    out_rects[(*io_count)++] = clipped;
    return true;
}

static bool overlay_ensure_slot_count(uint8_t needed) {
    if (g_overlay_slots && needed <= g_overlay_slot_count) {
        return true;
    }
    OverlaySlot *p = (OverlaySlot *)CLJ_HOST_REALLOC(
        g_overlay_slots, (size_t)needed * sizeof(OverlaySlot));
    if (!p) { return false; }
    /* Zero-init only the new slots. */
    for (uint8_t s = g_overlay_slot_count; s < needed; s++) {
        memset(&p[s], 0, sizeof(OverlaySlot));
    }
    g_overlay_slots = p;
    g_overlay_slot_count = needed;
    return true;
}

void vg_rendered_state_capture_begin(uint8_t slot_index, uint32_t snapshot_generation, uint32_t frame_time_ms) {
    g_capture_ctx.active = false;
    g_capture_ctx.slot_index = 0u;
    if (slot_index >= g_overlay_slot_count) {
        (void)overlay_ensure_slot_count((uint8_t)(slot_index + 1u));
    }
    if (!g_overlay_slots || slot_index >= g_overlay_slot_count) {
        return;
    }
    g_capture_ctx.active = true;
    g_capture_ctx.slot_index = slot_index;

    OverlayBuffer *obuf = overlay_write_buf();
    if (obuf) {
        obuf->entity_count = 0;
        obuf->timeline_count = 0;
        obuf->snapshot_generation = snapshot_generation;
        obuf->frame_time_ms = frame_time_ms;
    }
}

void vg_rendered_state_capture_record_entity(uintptr_t entity_id_bits, VgTransformFixed world_t) {
    if (!entity_id_bits) {
        return;
    }
    OverlayBuffer *obuf = overlay_write_buf();
    if (obuf) {
        OverlayEntityRow *orow = overlay_ensure_entity(obuf, entity_id_bits);
        if (orow) { orow->world_t = world_t; }
    }
}

void vg_rendered_state_capture_record_entity_aabb(uintptr_t entity_id_bits, VgAabb world_aabb) {
    if (!entity_id_bits) {
        return;
    }
    OverlayBuffer *obuf = overlay_write_buf();
    if (obuf) {
        OverlayEntityRow *orow = overlay_ensure_entity(obuf, entity_id_bits);
        if (orow) { orow->has_world_aabb = true; orow->world_aabb = world_aabb; }
    }
}

void vg_rendered_state_capture_record_entity_content_signature(uintptr_t entity_id_bits, uint32_t content_signature) {
    if (!entity_id_bits) {
        return;
    }
    OverlayBuffer *obuf = overlay_write_buf();
    if (obuf) {
        OverlayEntityRow *orow = overlay_ensure_entity(obuf, entity_id_bits);
        if (orow) { orow->content_signature = content_signature; }
    }
}

void vg_rendered_state_capture_record_timeline(uintptr_t entity_id_bits,
                                               VgRenderedField field,
                                               VgRenderedTimelineSample sample) {
    if (!entity_id_bits || field == VG_RENDERED_FIELD_NONE) {
        return;
    }
    OverlayBuffer *obuf = overlay_write_buf();
    if (!obuf) {
        return;
    }
    int existing = overlay_find_timeline_row(obuf, entity_id_bits, (uint8_t)field);
    if (existing >= 0) {
        obuf->timelines[existing].sample = sample;
    } else if (overlay_ensure_timeline_capacity(obuf)) {
        uint16_t idx = obuf->timeline_count++;
        obuf->timelines[idx].entity_id_bits = entity_id_bits;
        obuf->timelines[idx].field = (uint8_t)field;
        obuf->timelines[idx]._pad = 0;
        obuf->timelines[idx].sample = sample;
    }
}

void vg_rendered_state_capture_commit(void) {
    if (!g_capture_ctx.active || !g_overlay_slots ||
        g_capture_ctx.slot_index >= g_overlay_slot_count) {
        return;
    }
    OverlaySlot *oslot = &g_overlay_slots[g_capture_ctx.slot_index];
    unsigned int oactive = atomic_load_explicit(&oslot->active_buffer_index, memory_order_acquire);
    uint8_t owrite = (oactive == 0u) ? 1u : 0u;
    atomic_store_explicit(&oslot->active_buffer_index, owrite, memory_order_release);
    g_capture_ctx.active = false;
}

void vg_rendered_state_capture_discard(void) {
    /* Reset overlay write buffer counts so stale data is not committed later. */
    OverlayBuffer *obuf = overlay_write_buf();
    if (obuf) {
        obuf->entity_count = 0;
        obuf->timeline_count = 0;
    }
    g_capture_ctx.active = false;
}

static void overlay_destroy(void) {
    if (g_overlay_slots) {
        for (uint8_t s = 0; s < g_overlay_slot_count; s++) {
            for (int b = 0; b < 2; b++) {
                CLJ_HOST_FREE(g_overlay_slots[s].buffers[b].entities);
                CLJ_HOST_FREE(g_overlay_slots[s].buffers[b].timelines);
            }
        }
        CLJ_HOST_FREE(g_overlay_slots);
        g_overlay_slots = NULL;
    }
    g_overlay_slot_count = 0;
}

bool vg_rendered_state_query_entity(uint8_t slot_index,
                                      uintptr_t entity_id_bits,
                                      VgRenderedEntityState *out_state) {
    if (!out_state || !entity_id_bits || !g_overlay_slots ||
        slot_index >= g_overlay_slot_count) {
        return false;
    }
    OverlaySlot *slot = &g_overlay_slots[slot_index];
    unsigned int active = atomic_load_explicit(&slot->active_buffer_index, memory_order_acquire);
    if (active > 1u) {
        return false;
    }
    const OverlayBuffer *buf = &slot->buffers[active];
    int idx = overlay_find_entity_row(buf, entity_id_bits);
    if (idx < 0) {
        return false;
    }
    out_state->snapshot_generation = buf->snapshot_generation;
    out_state->frame_time_ms = buf->frame_time_ms;
    out_state->world_t = buf->entities[idx].world_t;
    out_state->has_world_aabb = buf->entities[idx].has_world_aabb;
    out_state->world_aabb = buf->entities[idx].world_aabb;
    return true;
}

bool vg_rendered_state_query_timeline(uint8_t slot_index,
                                        uintptr_t entity_id_bits,
                                        VgRenderedField field,
                                        VgRenderedTimelineState *out_state) {
    if (!out_state || !entity_id_bits || !g_overlay_slots ||
        slot_index >= g_overlay_slot_count || field == VG_RENDERED_FIELD_NONE) {
        return false;
    }
    OverlaySlot *slot = &g_overlay_slots[slot_index];
    unsigned int active = atomic_load_explicit(&slot->active_buffer_index, memory_order_acquire);
    if (active > 1u) {
        return false;
    }
    const OverlayBuffer *buf = &slot->buffers[active];
    int idx = overlay_find_timeline_row(buf, entity_id_bits, (uint8_t)field);
    if (idx < 0) {
        return false;
    }
    out_state->snapshot_generation = buf->snapshot_generation;
    out_state->frame_time_ms = buf->frame_time_ms;
    out_state->sample = buf->timelines[idx].sample;
    return true;
}

static bool overlay_entity_changed(const OverlayEntityRow *curr,
                                   const OverlayBuffer *prev_buf) {
    int prev_idx = overlay_find_entity_row(prev_buf, curr->entity_id_bits);
    if (prev_idx < 0) { return true; }
    const OverlayEntityRow *prev = &prev_buf->entities[prev_idx];
    if (memcmp(&curr->world_t, &prev->world_t, sizeof(curr->world_t)) != 0) { return true; }
    if (curr->has_world_aabb != prev->has_world_aabb) { return true; }
    if (curr->content_signature != prev->content_signature) { return true; }
    if (curr->has_world_aabb && prev->has_world_aabb &&
        !aabb_equal(curr->world_aabb, prev->world_aabb)) { return true; }
    return false;
}

bool vg_rendered_state_capture_compute_dirty_rect(uint8_t slot_index,
                                                    VgClipRect clip_rect,
                                                    uint8_t padding_px,
                                                    VgClipRect *out_dirty_rect) {
    if (!out_dirty_rect || !g_overlay_slots || slot_index >= g_overlay_slot_count ||
        !g_capture_ctx.active || g_capture_ctx.slot_index != slot_index ||
        vg_clip_rect_is_empty(clip_rect)) {
        return false;
    }
    OverlaySlot *oslot = &g_overlay_slots[slot_index];
    unsigned int active = atomic_load_explicit(&oslot->active_buffer_index, memory_order_acquire);
    if (active > 1u) { return false; }
    const OverlayBuffer *prev = &oslot->buffers[active];
    uint8_t write_idx = (active == 0u) ? 1u : 0u;
    const OverlayBuffer *curr = &oslot->buffers[write_idx];

    bool have_dirty = false;
    VgClipRect dirty = {0};

    for (uint16_t i = 0; i < curr->entity_count; i++) {
        const OverlayEntityRow *cr = &curr->entities[i];
        if (!overlay_entity_changed(cr, prev)) { continue; }
        int pi = overlay_find_entity_row(prev, cr->entity_id_bits);
        bool contributed = false;
        if (pi >= 0 && prev->entities[pi].has_world_aabb) {
            contributed |= append_dirty_aabb_rect(&dirty, &have_dirty,
                                                   prev->entities[pi].world_aabb, padding_px, clip_rect);
        }
        if (cr->has_world_aabb) {
            contributed |= append_dirty_aabb_rect(&dirty, &have_dirty,
                                                   cr->world_aabb, padding_px, clip_rect);
        }
        if (!contributed) { *out_dirty_rect = clip_rect; return true; }
    }
    for (uint16_t i = 0; i < prev->entity_count; i++) {
        if (overlay_find_entity_row(curr, prev->entities[i].entity_id_bits) >= 0) { continue; }
        if (!prev->entities[i].has_world_aabb) { *out_dirty_rect = clip_rect; return true; }
        (void)append_dirty_aabb_rect(&dirty, &have_dirty,
                                     prev->entities[i].world_aabb, padding_px, clip_rect);
    }
    if (!have_dirty) { return false; }
    *out_dirty_rect = dirty;
    return true;
}

bool vg_rendered_state_capture_collect_dirty_rects(uint8_t slot_index,
                                                     VgClipRect clip_rect,
                                                     uint8_t padding_px,
                                                     VgClipRect *out_rects,
                                                     size_t out_capacity,
                                                     size_t *out_count) {
    if (out_count) { *out_count = 0u; }
    if (!out_rects || !out_count || out_capacity == 0u || !g_overlay_slots ||
        slot_index >= g_overlay_slot_count || !g_capture_ctx.active ||
        g_capture_ctx.slot_index != slot_index || vg_clip_rect_is_empty(clip_rect)) {
        return false;
    }
    OverlaySlot *oslot = &g_overlay_slots[slot_index];
    unsigned int active = atomic_load_explicit(&oslot->active_buffer_index, memory_order_acquire);
    if (active > 1u) { return false; }
    const OverlayBuffer *prev = &oslot->buffers[active];
    uint8_t write_idx = (active == 0u) ? 1u : 0u;
    const OverlayBuffer *curr = &oslot->buffers[write_idx];
    size_t count = 0u;

    for (uint16_t i = 0; i < curr->entity_count; i++) {
        const OverlayEntityRow *cr = &curr->entities[i];
        if (!overlay_entity_changed(cr, prev)) { continue; }
        int pi = overlay_find_entity_row(prev, cr->entity_id_bits);
        bool has_any = (pi >= 0 && prev->entities[pi].has_world_aabb) || cr->has_world_aabb;
        if (!has_any) { out_rects[0] = clip_rect; *out_count = 1u; return true; }
        if (pi >= 0 && prev->entities[pi].has_world_aabb) {
            if (!append_dirty_aabb_rect_leaf(out_rects, out_capacity, &count,
                                             prev->entities[pi].world_aabb, padding_px, clip_rect)) {
                return false;
            }
        }
        if (cr->has_world_aabb) {
            if (!append_dirty_aabb_rect_leaf(out_rects, out_capacity, &count,
                                             cr->world_aabb, padding_px, clip_rect)) {
                return false;
            }
        }
    }
    for (uint16_t i = 0; i < prev->entity_count; i++) {
        if (overlay_find_entity_row(curr, prev->entities[i].entity_id_bits) >= 0) { continue; }
        if (!prev->entities[i].has_world_aabb) {
            out_rects[0] = clip_rect; *out_count = 1u; return true;
        }
        if (!append_dirty_aabb_rect_leaf(out_rects, out_capacity, &count,
                                         prev->entities[i].world_aabb, padding_px, clip_rect)) {
            return false;
        }
    }
    *out_count = count;
    return count > 0u;
}

void vg_rendered_state_reset_all(void) {
    g_capture_ctx.active = false;
    g_capture_ctx.slot_index = 0u;
    overlay_destroy();
}
