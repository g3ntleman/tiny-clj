#ifndef TINY_CLJ_RENDER_BACKEND_H
#define TINY_CLJ_RENDER_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "vector_scene_graph.h"

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} VgBackendRect;

typedef struct {
    bool (*begin_frame)(void *ctx, uint32_t frame_id);
    bool (*submit_rect)(void *ctx,
                        VgBackendRect rect,
                        const uint16_t *rgb565_pixels,
                        uint16_t stride_px);
    bool (*end_frame)(void *ctx, uint32_t frame_id);
} VgBackendOps;

typedef struct {
    const VgBackendOps *ops;
    void *ctx;
} VgBackend;

bool vg_backend_begin_frame(const VgBackend *backend, uint32_t frame_id);
bool vg_backend_submit_clip_rect(const VgBackend *backend,
                                 const VgFrameBuffer *fb,
                                 VgClipRect rect);
bool vg_backend_end_frame(const VgBackend *backend, uint32_t frame_id);

#endif
