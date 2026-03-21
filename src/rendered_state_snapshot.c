#include "rendered_state_snapshot.h"

#include <stdatomic.h>
#include <string.h>

typedef struct {
    uintptr_t entity_id_bits;
    VgTransformFixed world_t;
    bool has_world_aabb;
    VgAabb world_aabb;
} RenderedEntityRow;

typedef struct {
    uintptr_t entity_id_bits;
    uint8_t field;
    uint8_t _pad;
    VgRenderedTimelineSample sample;
} RenderedTimelineRow;

typedef struct {
    uint32_t snapshot_generation;
    uint32_t frame_time_ms;
    uint16_t entity_count;
    uint16_t timeline_count;
    uint16_t dropped_entities;
    uint16_t dropped_timelines;
    RenderedEntityRow entities[VG_RENDERED_STATE_MAX_ENTITIES];
    RenderedTimelineRow timelines[VG_RENDERED_STATE_MAX_TIMELINES];
} RenderedSlotSnapshot;

typedef struct {
    RenderedSlotSnapshot buffers[2];
    atomic_uint active_buffer_index;
} RenderedSlotStore;

static RenderedSlotStore g_rendered_slots[VG_RENDERED_STATE_MAX_SLOTS];

typedef struct {
    bool active;
    uint8_t slot_index;
    uint8_t write_buffer_index;
} RenderedCaptureContext;

static RenderedCaptureContext g_capture_ctx = {0};

static inline RenderedSlotSnapshot *capture_write_snapshot(void) {
    if (!g_capture_ctx.active || g_capture_ctx.slot_index >= VG_RENDERED_STATE_MAX_SLOTS) {
        return NULL;
    }
    RenderedSlotStore *store = &g_rendered_slots[g_capture_ctx.slot_index];
    return &store->buffers[g_capture_ctx.write_buffer_index];
}

static int find_entity_row(RenderedSlotSnapshot *snapshot, uintptr_t entity_id_bits) {
    if (!snapshot) {
        return -1;
    }
    for (uint16_t i = 0; i < snapshot->entity_count; i++) {
        if (snapshot->entities[i].entity_id_bits == entity_id_bits) {
            return (int)i;
        }
    }
    return -1;
}

static RenderedEntityRow *ensure_entity_row(RenderedSlotSnapshot *snapshot, uintptr_t entity_id_bits) {
    if (!snapshot || !entity_id_bits) {
        return NULL;
    }
    int existing = find_entity_row(snapshot, entity_id_bits);
    if (existing >= 0) {
        return &snapshot->entities[existing];
    }
    if (snapshot->entity_count >= VG_RENDERED_STATE_MAX_ENTITIES) {
        snapshot->dropped_entities++;
        return NULL;
    }
    uint16_t idx = snapshot->entity_count++;
    snapshot->entities[idx].entity_id_bits = entity_id_bits;
    return &snapshot->entities[idx];
}

static int find_timeline_row(RenderedSlotSnapshot *snapshot, uintptr_t entity_id_bits, uint8_t field) {
    if (!snapshot) {
        return -1;
    }
    for (uint16_t i = 0; i < snapshot->timeline_count; i++) {
        if (snapshot->timelines[i].entity_id_bits == entity_id_bits &&
            snapshot->timelines[i].field == field) {
            return (int)i;
        }
    }
    return -1;
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

void vg_rendered_state_capture_begin(uint8_t slot_index, uint32_t snapshot_generation, uint32_t frame_time_ms) {
    g_capture_ctx.active = false;
    g_capture_ctx.slot_index = 0u;
    g_capture_ctx.write_buffer_index = 0u;
    if (slot_index >= VG_RENDERED_STATE_MAX_SLOTS) {
        return;
    }
    RenderedSlotStore *store = &g_rendered_slots[slot_index];
    unsigned int active = atomic_load_explicit(&store->active_buffer_index, memory_order_acquire);
    uint8_t write = (uint8_t)((active == 0u) ? 1u : 0u);
    RenderedSlotSnapshot *snapshot = &store->buffers[write];
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->snapshot_generation = snapshot_generation;
    snapshot->frame_time_ms = frame_time_ms;
    g_capture_ctx.active = true;
    g_capture_ctx.slot_index = slot_index;
    g_capture_ctx.write_buffer_index = write;
}

void vg_rendered_state_capture_record_entity(uintptr_t entity_id_bits, VgTransformFixed world_t) {
    if (!entity_id_bits) {
        return;
    }
    RenderedSlotSnapshot *snapshot = capture_write_snapshot();
    if (!snapshot) {
        return;
    }
    RenderedEntityRow *row = ensure_entity_row(snapshot, entity_id_bits);
    if (!row) {
        return;
    }
    row->world_t = world_t;
}

void vg_rendered_state_capture_record_entity_aabb(uintptr_t entity_id_bits, VgAabb world_aabb) {
    if (!entity_id_bits) {
        return;
    }
    RenderedSlotSnapshot *snapshot = capture_write_snapshot();
    if (!snapshot) {
        return;
    }
    RenderedEntityRow *row = ensure_entity_row(snapshot, entity_id_bits);
    if (!row) {
        return;
    }
    row->has_world_aabb = true;
    row->world_aabb = world_aabb;
}

void vg_rendered_state_capture_record_timeline(uintptr_t entity_id_bits,
                                               VgRenderedField field,
                                               VgRenderedTimelineSample sample) {
    if (!entity_id_bits || field == VG_RENDERED_FIELD_NONE) {
        return;
    }
    RenderedSlotSnapshot *snapshot = capture_write_snapshot();
    if (!snapshot) {
        return;
    }
    int existing = find_timeline_row(snapshot, entity_id_bits, (uint8_t)field);
    if (existing >= 0) {
        snapshot->timelines[existing].sample = sample;
        return;
    }
    if (snapshot->timeline_count >= VG_RENDERED_STATE_MAX_TIMELINES) {
        snapshot->dropped_timelines++;
        return;
    }
    uint16_t idx = snapshot->timeline_count++;
    snapshot->timelines[idx].entity_id_bits = entity_id_bits;
    snapshot->timelines[idx].field = (uint8_t)field;
    snapshot->timelines[idx].sample = sample;
}

bool vg_rendered_state_capture_compute_dirty_rect(uint8_t slot_index,
                                                  VgClipRect clip_rect,
                                                  uint8_t padding_px,
                                                  VgClipRect *out_dirty_rect) {
    if (!out_dirty_rect || slot_index >= VG_RENDERED_STATE_MAX_SLOTS || !g_capture_ctx.active ||
        g_capture_ctx.slot_index != slot_index || vg_clip_rect_is_empty(clip_rect)) {
        return false;
    }

    RenderedSlotStore *store = &g_rendered_slots[slot_index];
    unsigned int active = atomic_load_explicit(&store->active_buffer_index, memory_order_acquire);
    if (active > 1u) {
        return false;
    }

    RenderedSlotSnapshot *prev = &store->buffers[active];
    RenderedSlotSnapshot *curr = &store->buffers[g_capture_ctx.write_buffer_index];
    bool have_dirty_rect = false;
    VgClipRect dirty_rect = {0};

    for (uint16_t i = 0; i < curr->entity_count; i++) {
        RenderedEntityRow *curr_row = &curr->entities[i];
        int prev_idx = find_entity_row(prev, curr_row->entity_id_bits);
        RenderedEntityRow *prev_row = (prev_idx >= 0) ? &prev->entities[prev_idx] : NULL;
        bool changed = !prev_row ||
                       memcmp(&curr_row->world_t, &prev_row->world_t, sizeof(curr_row->world_t)) != 0 ||
                       curr_row->has_world_aabb != prev_row->has_world_aabb ||
                       (curr_row->has_world_aabb && prev_row->has_world_aabb &&
                        !aabb_equal(curr_row->world_aabb, prev_row->world_aabb));
        if (!changed) {
            continue;
        }

        bool contributed = false;
        if (prev_row && prev_row->has_world_aabb) {
            contributed |= append_dirty_aabb_rect(&dirty_rect,
                                                  &have_dirty_rect,
                                                  prev_row->world_aabb,
                                                  padding_px,
                                                  clip_rect);
        }
        if (curr_row->has_world_aabb) {
            contributed |= append_dirty_aabb_rect(&dirty_rect,
                                                  &have_dirty_rect,
                                                  curr_row->world_aabb,
                                                  padding_px,
                                                  clip_rect);
        }
        if (!contributed) {
            return false;
        }
    }

    for (uint16_t i = 0; i < prev->entity_count; i++) {
        RenderedEntityRow *prev_row = &prev->entities[i];
        if (find_entity_row(curr, prev_row->entity_id_bits) >= 0) {
            continue;
        }
        if (!prev_row->has_world_aabb) {
            return false;
        }
        (void)append_dirty_aabb_rect(&dirty_rect,
                                     &have_dirty_rect,
                                     prev_row->world_aabb,
                                     padding_px,
                                     clip_rect);
    }

    if (!have_dirty_rect) {
        return false;
    }

    *out_dirty_rect = dirty_rect;
    return true;
}

bool vg_rendered_state_capture_collect_dirty_rects(uint8_t slot_index,
                                                   VgClipRect clip_rect,
                                                   uint8_t padding_px,
                                                   VgClipRect *out_rects,
                                                   size_t out_capacity,
                                                   size_t *out_count) {
    if (out_count) {
        *out_count = 0u;
    }
    if (!out_rects || !out_count || out_capacity == 0u || slot_index >= VG_RENDERED_STATE_MAX_SLOTS ||
        !g_capture_ctx.active || g_capture_ctx.slot_index != slot_index || vg_clip_rect_is_empty(clip_rect)) {
        return false;
    }

    RenderedSlotStore *store = &g_rendered_slots[slot_index];
    unsigned int active = atomic_load_explicit(&store->active_buffer_index, memory_order_acquire);
    if (active > 1u) {
        return false;
    }

    RenderedSlotSnapshot *prev = &store->buffers[active];
    RenderedSlotSnapshot *curr = &store->buffers[g_capture_ctx.write_buffer_index];
    size_t count = 0u;

    for (uint16_t i = 0; i < curr->entity_count; i++) {
        RenderedEntityRow *curr_row = &curr->entities[i];
        int prev_idx = find_entity_row(prev, curr_row->entity_id_bits);
        RenderedEntityRow *prev_row = (prev_idx >= 0) ? &prev->entities[prev_idx] : NULL;
        bool changed = !prev_row ||
                       memcmp(&curr_row->world_t, &prev_row->world_t, sizeof(curr_row->world_t)) != 0 ||
                       curr_row->has_world_aabb != prev_row->has_world_aabb ||
                       (curr_row->has_world_aabb && prev_row->has_world_aabb &&
                        !aabb_equal(curr_row->world_aabb, prev_row->world_aabb));
        if (!changed) {
            continue;
        }

        if (prev_row && prev_row->has_world_aabb) {
            if (!append_dirty_aabb_rect_leaf(out_rects,
                                             out_capacity,
                                             &count,
                                             prev_row->world_aabb,
                                             padding_px,
                                             clip_rect)) {
                return false;
            }
        }
        if (curr_row->has_world_aabb) {
            if (!append_dirty_aabb_rect_leaf(out_rects,
                                             out_capacity,
                                             &count,
                                             curr_row->world_aabb,
                                             padding_px,
                                             clip_rect)) {
                return false;
            }
        }
    }

    for (uint16_t i = 0; i < prev->entity_count; i++) {
        RenderedEntityRow *prev_row = &prev->entities[i];
        if (find_entity_row(curr, prev_row->entity_id_bits) >= 0) {
            continue;
        }
        if (!prev_row->has_world_aabb) {
            return false;
        }
        if (!append_dirty_aabb_rect_leaf(out_rects,
                                         out_capacity,
                                         &count,
                                         prev_row->world_aabb,
                                         padding_px,
                                         clip_rect)) {
            return false;
        }
    }

    *out_count = count;
    return count > 0u;
}

void vg_rendered_state_capture_commit(void) {
    if (!g_capture_ctx.active || g_capture_ctx.slot_index >= VG_RENDERED_STATE_MAX_SLOTS) {
        return;
    }
    RenderedSlotStore *store = &g_rendered_slots[g_capture_ctx.slot_index];
    atomic_store_explicit(&store->active_buffer_index, g_capture_ctx.write_buffer_index, memory_order_release);
    g_capture_ctx.active = false;
}

void vg_rendered_state_capture_discard(void) {
    g_capture_ctx.active = false;
}

bool vg_rendered_state_query_entity(uint8_t slot_index, uintptr_t entity_id_bits, VgRenderedEntityState *out_state) {
    if (!out_state || !entity_id_bits || slot_index >= VG_RENDERED_STATE_MAX_SLOTS) {
        return false;
    }
    RenderedSlotStore *store = &g_rendered_slots[slot_index];
    unsigned int active = atomic_load_explicit(&store->active_buffer_index, memory_order_acquire);
    if (active > 1u) {
        return false;
    }
    RenderedSlotSnapshot *snapshot = &store->buffers[active];
    int idx = find_entity_row(snapshot, entity_id_bits);
    if (idx < 0) {
        return false;
    }
    out_state->snapshot_generation = snapshot->snapshot_generation;
    out_state->frame_time_ms = snapshot->frame_time_ms;
    out_state->world_t = snapshot->entities[idx].world_t;
    out_state->has_world_aabb = snapshot->entities[idx].has_world_aabb;
    out_state->world_aabb = snapshot->entities[idx].world_aabb;
    return true;
}

bool vg_rendered_state_query_timeline(uint8_t slot_index,
                                      uintptr_t entity_id_bits,
                                      VgRenderedField field,
                                      VgRenderedTimelineState *out_state) {
    if (!out_state || !entity_id_bits || slot_index >= VG_RENDERED_STATE_MAX_SLOTS || field == VG_RENDERED_FIELD_NONE) {
        return false;
    }
    RenderedSlotStore *store = &g_rendered_slots[slot_index];
    unsigned int active = atomic_load_explicit(&store->active_buffer_index, memory_order_acquire);
    if (active > 1u) {
        return false;
    }
    RenderedSlotSnapshot *snapshot = &store->buffers[active];
    int idx = find_timeline_row(snapshot, entity_id_bits, (uint8_t)field);
    if (idx < 0) {
        return false;
    }
    out_state->snapshot_generation = snapshot->snapshot_generation;
    out_state->frame_time_ms = snapshot->frame_time_ms;
    out_state->sample = snapshot->timelines[idx].sample;
    return true;
}

void vg_rendered_state_reset_all(void) {
    g_capture_ctx.active = false;
    g_capture_ctx.slot_index = 0u;
    g_capture_ctx.write_buffer_index = 0u;
    memset(g_rendered_slots, 0, sizeof(g_rendered_slots));
    for (uint8_t i = 0; i < VG_RENDERED_STATE_MAX_SLOTS; i++) {
        atomic_store_explicit(&g_rendered_slots[i].active_buffer_index, 0u, memory_order_release);
    }
}
