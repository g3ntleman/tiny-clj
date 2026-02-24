#ifndef TINY_CLJ_VECTOR_SCENE_GRAPH_RECORDS_H
#define TINY_CLJ_VECTOR_SCENE_GRAPH_RECORDS_H

#include <stdbool.h>

#include "vector_scene_graph.h"
#include "object.h"

bool vg_render_scene_record(ID root_record, VgFrameBuffer *fb);
bool vg_render_scene_record_clipped(ID root_record, VgFrameBuffer *fb, VgClipRect clip_rect);
bool vg_decode_frame_slot_record(ID frame_scene_record, VgRenderSlot *out_slot);
bool vg_render_frame_slot_record_if_changed(ID frame_scene_record,
                                            VgRenderSlotState *state,
                                            VgFrameBuffer *fb,
                                            uint32_t snapshot_id);

#endif
