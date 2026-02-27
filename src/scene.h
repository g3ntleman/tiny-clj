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

bool vg_slot_change_tracker_init(VgSlotChangeTracker *tracker, uint8_t slot_count);
void vg_slot_change_tracker_destroy(VgSlotChangeTracker *tracker);
bool vg_slot_change_tracker_publish(VgSlotChangeTracker *tracker, uint8_t slot_index, uint32_t *out_generation);
uint32_t vg_slot_change_tracker_wait_for_changes(VgSlotChangeTracker *tracker,
                                                 const uint32_t *last_seen_generations,
                                                 uint32_t *out_generations,
                                                 uint32_t timeout_ms);

bool vg_render_scene_record(ID root_record, VgFrameBuffer *fb);
bool vg_render_scene_record_clipped(ID root_record, VgFrameBuffer *fb, VgClipRect clip_rect);
bool vg_decode_frame_slot_record(ID frame_scene_record, VgRenderSlot *out_slot);
bool vg_render_frame_slot_record_if_changed(ID frame_scene_record,
                                            VgRenderSlotState *state,
                                            VgFrameBuffer *fb,
                                            uint32_t snapshot_id,
                                            uint32_t *out_dirty_pixels);

#endif
