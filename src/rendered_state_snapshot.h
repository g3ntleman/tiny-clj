#ifndef TINY_CLJ_RENDERED_STATE_SNAPSHOT_H
#define TINY_CLJ_RENDERED_STATE_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "vector_scene_graph.h"
#include "fx_collision.h"

#if defined(ESP32_BUILD)
#define VG_RENDERED_STATE_MAX_SLOTS 3u
#define VG_RENDERED_STATE_MAX_ENTITIES 64u
#define VG_RENDERED_STATE_MAX_TIMELINES 128u
#else
#define VG_RENDERED_STATE_MAX_SLOTS 8u
#define VG_RENDERED_STATE_MAX_ENTITIES 256u
#define VG_RENDERED_STATE_MAX_TIMELINES 512u
#endif

typedef enum {
    VG_RENDERED_FIELD_NONE = 0,
    VG_RENDERED_FIELD_T = 1,
    VG_RENDERED_FIELD_STYLE = 2,
    VG_RENDERED_FIELD_VISIBLE = 3,
    VG_RENDERED_FIELD_CHILDREN = 4,
    VG_RENDERED_FIELD_PTS = 5,
    VG_RENDERED_FIELD_CLOSED = 6,
    VG_RENDERED_FIELD_X = 7,
    VG_RENDERED_FIELD_Y = 8,
    VG_RENDERED_FIELD_W = 9,
    VG_RENDERED_FIELD_H = 10,
    VG_RENDERED_FIELD_X1 = 11,
    VG_RENDERED_FIELD_Y1 = 12,
    VG_RENDERED_FIELD_X2 = 13,
    VG_RENDERED_FIELD_Y2 = 14,
    VG_RENDERED_FIELD_X3 = 15,
    VG_RENDERED_FIELD_Y3 = 16,
    VG_RENDERED_FIELD_SCALE = 17,
    VG_RENDERED_FIELD_ROT = 18,
    VG_RENDERED_FIELD_TEXT = 19
} VgRenderedField;

typedef struct {
    uint16_t step_index;
    uint16_t keyframe_count;
    uint32_t phase_ms;
    uint32_t period_ms;
    bool loop;
    bool end_event;
    bool at_end;
} VgRenderedTimelineSample;

typedef struct {
    uint32_t snapshot_generation;
    uint32_t frame_time_ms;
    VgTransformFixed world_t;
    bool has_world_aabb;
    VgAabb world_aabb;
} VgRenderedEntityState;

typedef struct {
    uint32_t snapshot_generation;
    uint32_t frame_time_ms;
    VgRenderedTimelineSample sample;
} VgRenderedTimelineState;

/* Writer-side API (render thread) */
void vg_rendered_state_capture_begin(uint8_t slot_index, uint32_t snapshot_generation, uint32_t frame_time_ms);
void vg_rendered_state_capture_record_entity(uintptr_t entity_id_bits, VgTransformFixed world_t);
void vg_rendered_state_capture_record_entity_aabb(uintptr_t entity_id_bits, VgAabb world_aabb);
void vg_rendered_state_capture_record_entity_content_signature(uintptr_t entity_id_bits, uint32_t content_signature);
void vg_rendered_state_capture_record_timeline(uintptr_t entity_id_bits,
                                               VgRenderedField field,
                                               VgRenderedTimelineSample sample);
bool vg_rendered_state_capture_compute_dirty_rect(uint8_t slot_index,
                                                  VgClipRect clip_rect,
                                                  uint8_t padding_px,
                                                  VgClipRect *out_dirty_rect);
bool vg_rendered_state_capture_collect_dirty_rects(uint8_t slot_index,
                                                   VgClipRect clip_rect,
                                                   uint8_t padding_px,
                                                   VgClipRect *out_rects,
                                                   size_t out_capacity,
                                                   size_t *out_count);
void vg_rendered_state_capture_commit(void);
void vg_rendered_state_capture_discard(void);

/* Reader-side API (runtime builtins) */
bool vg_rendered_state_query_entity(uint8_t slot_index, uintptr_t entity_id_bits, VgRenderedEntityState *out_state);
bool vg_rendered_state_query_timeline(uint8_t slot_index,
                                      uintptr_t entity_id_bits,
                                      VgRenderedField field,
                                      VgRenderedTimelineState *out_state);

/* Test/reset utility */
void vg_rendered_state_reset_all(void);

#endif /* TINY_CLJ_RENDERED_STATE_SNAPSHOT_H */
