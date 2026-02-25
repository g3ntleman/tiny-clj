#include "gfx.h"

#include <stdlib.h>

static bool gfx_clip_reject(const GfxClip *clip, int x, int y) {
    if (!clip || !clip->enabled) {
        return false;
    }
    return x < clip->x0 || x >= clip->x1 || y < clip->y0 || y >= clip->y1;
}

static void gfx_put_pixel(VgFrameBuffer *fb, int x, int y, uint16_t color, const GfxClip *clip) {
    if (!fb || !fb->pixels) {
        return;
    }
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) {
        return;
    }
    if (gfx_clip_reject(clip, x, y)) {
        return;
    }
    fb->pixels[(size_t)y * (size_t)fb->width + (size_t)x] = color;
}

static uint16_t gfx_blend_rgb565(uint16_t bg, uint16_t fg, uint8_t alpha) {
    if (alpha == 0) return bg;
    if (alpha == 255) return fg;
    uint32_t inv = (uint32_t)(255u - alpha);

    uint32_t br = (bg >> 11) & 0x1fu;
    uint32_t bgc = (bg >> 5) & 0x3fu;
    uint32_t bb = bg & 0x1fu;

    uint32_t fr = (fg >> 11) & 0x1fu;
    uint32_t fgc = (fg >> 5) & 0x3fu;
    uint32_t fb = fg & 0x1fu;

    uint32_t rr = (br * inv + fr * (uint32_t)alpha + 127u) / 255u;
    uint32_t rg = (bgc * inv + fgc * (uint32_t)alpha + 127u) / 255u;
    uint32_t rb = (bb * inv + fb * (uint32_t)alpha + 127u) / 255u;

    if (rr > 0x1fu) rr = 0x1fu;
    if (rg > 0x3fu) rg = 0x3fu;
    if (rb > 0x1fu) rb = 0x1fu;
    return (uint16_t)((rr << 11) | (rg << 5) | rb);
}

static void gfx_put_pixel_aa_bg(VgFrameBuffer *fb,
                                int x,
                                int y,
                                uint16_t fg,
                                uint16_t bg,
                                uint8_t alpha,
                                const GfxClip *clip) {
    if (!fb || !fb->pixels) return;
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) return;
    if (gfx_clip_reject(clip, x, y)) {
        return;
    }
    fb->pixels[(size_t)y * (size_t)fb->width + (size_t)x] = gfx_blend_rgb565(bg, fg, alpha);
}

void gfx_draw_fill_rect(VgFrameBuffer *fb, int x, int y, int w, int h, uint16_t color, const GfxClip *clip) {
    if (w <= 0 || h <= 0) {
        return;
    }
    int x0 = x;
    int y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    if (x1 < 0 || y1 < 0 || x0 >= fb->width || y0 >= fb->height) {
        return;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= fb->width) x1 = fb->width - 1;
    if (y1 >= fb->height) y1 = fb->height - 1;
    for (int yy = y0; yy <= y1; yy++) {
        for (int xx = x0; xx <= x1; xx++) {
            gfx_put_pixel(fb, xx, yy, color, clip);
        }
    }
}

void gfx_fill_polygon_scanline(VgFrameBuffer *fb,
                               const int *vx,
                               const int *vy,
                               size_t count,
                               uint16_t color,
                               const GfxClip *clip) {
    if (!fb || !vx || !vy || count < 3 || count > GFX_FILL_MAX_VERTS) {
        return;
    }
    int min_y = vy[0];
    int max_y = vy[0];
    for (size_t i = 1; i < count; i++) {
        if (vy[i] < min_y) min_y = vy[i];
        if (vy[i] > max_y) max_y = vy[i];
    }
    if (max_y < 0 || min_y >= fb->height) {
        return;
    }
    if (min_y < 0) min_y = 0;
    if (max_y >= fb->height) max_y = fb->height - 1;

    int xints[GFX_FILL_MAX_VERTS];
    for (int y = min_y; y <= max_y; y++) {
        size_t nints = 0;
        for (size_t i = 0; i < count; i++) {
            size_t j = (i + 1u) % count;
            int y1 = vy[i];
            int y2 = vy[j];
            int x1 = vx[i];
            int x2 = vx[j];
            if (y1 == y2) {
                continue;
            }
            int ymin = (y1 < y2) ? y1 : y2;
            int ymax = (y1 > y2) ? y1 : y2;
            if (y < ymin || y >= ymax) {
                continue;
            }
            int64_t dy = (int64_t)(y2 - y1);
            int64_t dx = (int64_t)(x2 - x1);
            int64_t num = (int64_t)(y - y1) * dx;
            int px = x1 + (int)(num / dy);
            if (nints < GFX_FILL_MAX_VERTS) {
                xints[nints++] = px;
            }
        }
        for (size_t i = 1; i < nints; i++) {
            int key = xints[i];
            size_t k = i;
            while (k > 0 && xints[k - 1] > key) {
                xints[k] = xints[k - 1];
                k--;
            }
            xints[k] = key;
        }
        for (size_t i = 0; i + 1 < nints; i += 2) {
            int x0 = xints[i];
            int x1 = xints[i + 1];
            if (x1 < x0) {
                int t = x0;
                x0 = x1;
                x1 = t;
            }
            if (x1 < 0 || x0 >= fb->width) {
                continue;
            }
            if (x0 < 0) x0 = 0;
            if (x1 >= fb->width) x1 = fb->width - 1;
            for (int xx = x0; xx <= x1; xx++) {
                gfx_put_pixel(fb, xx, y, color, clip);
            }
        }
    }
}

void gfx_draw_line_basic(VgFrameBuffer *fb, int x0, int y0, int x1, int y1, uint16_t color, const GfxClip *clip) {
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (true) {
        gfx_put_pixel(fb, x0, y0, color, clip);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_draw_line_supercover(VgFrameBuffer *fb, int x0, int y0, int x1, int y1, uint16_t color, const GfxClip *clip) {
    int dx = abs(x1 - x0);
    int sx = (x1 >= x0) ? 1 : -1;
    int dy = abs(y1 - y0);
    int sy = (y1 >= y0) ? 1 : -1;

    // The supercover branch logic below assumes both axes may advance.
    // For purely horizontal/vertical segments it can add a stray orthogonal
    // pixel, which is very visible on tiny punctuation boxes (':' '.').
    if (dx == 0 || dy == 0) {
        gfx_draw_line_basic(fb, x0, y0, x1, y1, color, clip);
        return;
    }

    int x = x0;
    int y = y0;
    gfx_put_pixel(fb, x, y, color, clip);

    if (dx >= dy) {
        int err = dx / 2;
        for (int i = 0; i < dx; i++) {
            x += sx;
            err -= dy;
            if (err <= 0) {
                // Supercover: ensure corner transitions stay connected.
                gfx_put_pixel(fb, x, y, color, clip);
                y += sy;
                err += dx;
            }
            gfx_put_pixel(fb, x, y, color, clip);
        }
    } else {
        int err = dy / 2;
        for (int i = 0; i < dy; i++) {
            y += sy;
            err -= dx;
            if (err <= 0) {
                // Supercover: ensure corner transitions stay connected.
                gfx_put_pixel(fb, x, y, color, clip);
                x += sx;
                err += dy;
            }
            gfx_put_pixel(fb, x, y, color, clip);
        }
    }
}

static void gfx_draw_line_aa_1px_bg(VgFrameBuffer *fb,
                                    int x0,
                                    int y0,
                                    int x1,
                                    int y1,
                                    uint16_t fg,
                                    uint16_t bg,
                                    const GfxClip *clip) {
    // Always draw a crisp 1px core first, then add only anti-aliased fringe.
    gfx_draw_line_basic(fb, x0, y0, x1, y1, fg, clip);
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    bool x_major = (dx >= -dy);
    uint8_t alpha = 72; // subtle fringe to keep crisp center

    while (true) {
        if (x_major) {
            gfx_put_pixel_aa_bg(fb, x0, y0 - 1, fg, bg, alpha, clip);
            gfx_put_pixel_aa_bg(fb, x0, y0 + 1, fg, bg, alpha, clip);
        } else {
            gfx_put_pixel_aa_bg(fb, x0 - 1, y0, fg, bg, alpha, clip);
            gfx_put_pixel_aa_bg(fb, x0 + 1, y0, fg, bg, alpha, clip);
        }

        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_draw_line_thick(VgFrameBuffer *fb,
                         int x0, int y0, int x1, int y1,
                         uint16_t color, int width,
                         bool has_bg, uint16_t bg_color,
                         const GfxClip *clip) {
    if (width <= 1) {
        if (has_bg) {
            gfx_draw_line_aa_1px_bg(fb, x0, y0, x1, y1, color, bg_color, clip);
            return;
        }
        gfx_draw_line_basic(fb, x0, y0, x1, y1, color, clip);
        return;
    }
    if (x0 == x1) {
        int y_min = (y0 < y1) ? y0 : y1;
        int y_max = (y0 > y1) ? y0 : y1;
        int half = width / 2;
        gfx_draw_fill_rect(fb, x0 - half, y_min, width, (y_max - y_min) + 1, color, clip);
        return;
    }
    if (y0 == y1) {
        int x_min = (x0 < x1) ? x0 : x1;
        int x_max = (x0 > x1) ? x0 : x1;
        int half = width / 2;
        gfx_draw_fill_rect(fb, x_min, y0 - half, (x_max - x_min) + 1, width, color, clip);
        return;
    }
    // Integer-only fallback for general thick lines: stamp a square brush
    // along the Bresenham centerline. This keeps the raster hot path float-free.
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int half = width / 2;
    while (true) {
        gfx_draw_fill_rect(fb, x0 - half, y0 - half, width, width, color, clip);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
