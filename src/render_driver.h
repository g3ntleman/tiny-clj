#ifndef TINY_CLJ_RENDER_DRIVER_H
#define TINY_CLJ_RENDER_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "panel.h"
#include "scene.h"
#include "thread.h"
#include "vector_scene_graph.h"

typedef struct {
    VgPanel *panel;
    uint16_t *stripe_pixels;
    int16_t display_width;
    int16_t display_height;
    int16_t stripe_rows;
    uint8_t slot_count;
    const char *thread_name;
    size_t thread_stack_bytes;
    int thread_priority;

    VgRenderSlotState *slot_states;
    uint32_t *seen_generations;
    uint32_t animated_slots_mask;
    SubjectiveCThread *thread;
    volatile bool running;
    size_t stack_high_water_mark_bytes;

    VgSlotChangeTracker *tracker;
    const ID *scene_snapshots;
    uint16_t background_color;

    bool owns_slot_state_buffers;
} VgRenderDriver;

bool vg_render_driver_bind_runtime(VgRenderDriver *driver,
                                   VgSlotChangeTracker *tracker,
                                   const ID *scene_snapshots,
                                   uint16_t background_color);

bool vg_render_driver_stripe_push(VgRenderDriver *driver,
                                  const VgNode *scene_root,
                                  VgClipRect dirty,
                                  uint32_t now_ms,
                                  uint16_t bg_color);

int vg_render_driver_step(VgRenderDriver *driver,
                          VgSlotChangeTracker *tracker,
                          const ID *scene_snapshots);

bool vg_render_driver_start(VgRenderDriver *driver);
bool vg_render_driver_stop(VgRenderDriver *driver);

#endif /* TINY_CLJ_RENDER_DRIVER_H */
