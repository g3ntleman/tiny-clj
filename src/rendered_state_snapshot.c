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
