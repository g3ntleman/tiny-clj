#ifndef TINY_CLJ_GFX_H
#define TINY_CLJ_GFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vector_scene_graph.h"

#define GFX_FILL_MAX_VERTS 256

typedef struct {
    bool enabled;
    int x0;
    int y0;
    int x1;
    int y1;
} GfxClip;

void gfx_draw_fill_rect(VgFrameBuffer *fb, int x, int y, int w, int h, uint16_t color, const GfxClip *clip);
void gfx_fill_polygon_scanline(VgFrameBuffer *fb,
                               const int *vx,
                               const int *vy,
                               size_t count,
                               uint16_t color,
                               const GfxClip *clip);
void gfx_draw_line_basic(VgFrameBuffer *fb, int x0, int y0, int x1, int y1, uint16_t color, const GfxClip *clip);
void gfx_draw_line_supercover(VgFrameBuffer *fb, int x0, int y0, int x1, int y1, uint16_t color, const GfxClip *clip);
void gfx_draw_line_thick(VgFrameBuffer *fb,
                         int x0, int y0, int x1, int y1,
                         uint16_t color, int width,
                         bool has_bg, uint16_t bg_color,
                         const GfxClip *clip);

#endif
