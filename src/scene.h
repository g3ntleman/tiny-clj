#ifndef TINY_CLJ_SCENE_H
#define TINY_CLJ_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "object.h"
#include "vector_scene_graph.h"

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#define VG_SLOT_CHANGE_TRACKER_USE_PTHREAD 1
#else
#define VG_SLOT_CHANGE_TRACKER_USE_PTHREAD 0
#endif

#define VG_SLOT_CHANGE_TRACKER_MAX_SLOTS 32u

typedef struct {
    uint8_t slot_count;
    uint8_t _pad[3];
    uint64_t sequence;
    uint32_t generations[VG_SLOT_CHANGE_TRACKER_MAX_SLOTS];
#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif
} VgSlotChangeTracker;

typedef struct {
    bool rendered;
    bool has_animation;
    VgClipRect dirty_rect;
    uint32_t dirty_pixels;
} VgRenderFrameSlotResult;

typedef struct {
    ID event_id;
    uint16_t step_index;
    uint16_t keyframe_count;
    uint32_t phase_ms;
    uint32_t period_ms;
    bool end_event;
    bool at_end;
} VgTimelineProgressSample;

typedef void (*VgTimelineProgressObserverFn)(ID entity_id,
                                             const VgTimelineProgressSample *sample);

bool vg_slot_change_tracker_init(VgSlotChangeTracker *tracker, uint8_t slot_count);
void vg_slot_change_tracker_destroy(VgSlotChangeTracker *tracker);
bool vg_slot_change_tracker_publish(VgSlotChangeTracker *tracker, uint8_t slot_index, uint32_t *out_generation);
uint32_t vg_slot_change_tracker_wait_for_changes(VgSlotChangeTracker *tracker,
                                                 const uint32_t *last_seen_generations,
                                                 uint32_t *out_generations,
                                                 uint32_t timeout_ms);

bool vg_render_scene_record(ID root_record, VgFrameBuffer *fb);
bool vg_render_scene_record_clipped(ID root_record, VgFrameBuffer *fb, VgClipRect clip_rect);
bool vg_render_scene_record_at_ms(ID root_record, VgFrameBuffer *fb, uint32_t now_ms);
bool vg_render_scene_record_clipped_at_ms(ID root_record, VgFrameBuffer *fb, VgClipRect clip_rect, uint32_t now_ms);
bool vg_decode_frame_slot_record(ID frame_scene_record, VgRenderSlot *out_slot);
bool vg_render_frame_slot_record_if_changed(ID frame_scene_record,
                                            VgRenderSlotState *state,
                                            VgFrameBuffer *fb,
                                            uint32_t snapshot_id,
                                            uint32_t *out_dirty_pixels);
bool vg_render_frame_slot_record_at_ms(ID frame_scene_record,
                                       VgRenderSlotState *state,
                                       VgFrameBuffer *fb,
                                       uint32_t snapshot_id,
                                       uint32_t now_ms,
                                       bool force_render,
                                       uint32_t *out_dirty_pixels);
bool vg_render_frame_slot_record_result_at_ms(ID frame_scene_record,
                                              VgRenderSlotState *state,
                                              VgFrameBuffer *fb,
                                              uint32_t snapshot_id,
                                              uint32_t now_ms,
                                              bool force_render,
                                              VgRenderFrameSlotResult *out_result);
bool vg_render_frame_slot_record_if_changed_at_ms(ID frame_scene_record,
                                                   VgRenderSlotState *state,
                                                   VgFrameBuffer *fb,
                                                   uint32_t snapshot_id,
                                                   uint32_t now_ms,
                                                   uint32_t *out_dirty_pixels);
void vg_set_timeline_progress_observer(VgTimelineProgressObserverFn observer);

/* Query entity world transform from an immutable scene record.
 * Walks the scene tree composing static transforms.  Returns false when
 * the entity is not found or any ancestor carries a timeline transform
 * (the caller should fall back to the render-thread overlay in that case). */
bool vg_scene_query_entity_world_t(ID scene_record,
                                   uintptr_t entity_id_bits,
                                   VgTransformFixed *out_world_t);

#endif
