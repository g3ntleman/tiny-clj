#include "vector_scene_graph.h"
#include "vtext_hershey_simplex_subset.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
    float m00;
    float m01;
    float m02;
    float m10;
    float m11;
    float m12;
} VgMatrix2D;

typedef struct {
    int32_t m00;
    int32_t m01;
    int32_t m02;
    int32_t m10;
    int32_t m11;
    int32_t m12;
} VgMatrix2DFixed;

static VgMatrix2D matrix_from_transform(VgTransform t) {
    float r = t.rot_deg * (float)M_PI / 180.0f;
    float c = cosf(r);
    float s = sinf(r);
    VgMatrix2D m;
    m.m00 = c * t.sx;
    m.m01 = -s * t.sy;
    m.m02 = t.tx;
    m.m10 = s * t.sx;
    m.m11 = c * t.sy;
    m.m12 = t.ty;
    return m;
}

static VgMatrix2D matrix_mul(VgMatrix2D a, VgMatrix2D b) {
    VgMatrix2D m;
    m.m00 = (a.m00 * b.m00) + (a.m01 * b.m10);
    m.m01 = (a.m00 * b.m01) + (a.m01 * b.m11);
    m.m02 = (a.m00 * b.m02) + (a.m01 * b.m12) + a.m02;
    m.m10 = (a.m10 * b.m00) + (a.m11 * b.m10);
    m.m11 = (a.m10 * b.m01) + (a.m11 * b.m11);
    m.m12 = (a.m10 * b.m02) + (a.m11 * b.m12) + a.m12;
    return m;
}

static VgTransform transform_from_matrix(VgMatrix2D m) {
    VgTransform t = vg_transform_identity();
    t.tx = m.m02;
    t.ty = m.m12;
    t.sx = sqrtf((m.m00 * m.m00) + (m.m10 * m.m10));
    t.sy = sqrtf((m.m01 * m.m01) + (m.m11 * m.m11));
    t.rot_deg = atan2f(m.m10, m.m00) * (180.0f / (float)M_PI);
    return t;
}

VgTransform vg_transform_identity(void) {
    VgTransform t;
    t.tx = 0.0f;
    t.ty = 0.0f;
    t.sx = 1.0f;
    t.sy = 1.0f;
    t.rot_deg = 0.0f;
    return t;
}

VgStyle vg_style_default(void) {
    VgStyle s;
    s.stroke_rgb565 = 0xffffu;
    s.stroke_width = 1;
    s.visible = true;
    s.has_bg_rgb565 = false;
    s.bg_rgb565 = 0x0000u;
    return s;
}

VgTransform vg_transform_compose(VgTransform parent, VgTransform local) {
    VgMatrix2D pm = matrix_from_transform(parent);
    VgMatrix2D lm = matrix_from_transform(local);
    return transform_from_matrix(matrix_mul(pm, lm));
}

void vg_transform_apply(VgTransform t, float x, float y, float *out_x, float *out_y) {
    VgMatrix2D m = matrix_from_transform(t);
    if (out_x) {
        *out_x = (m.m00 * x) + (m.m01 * y) + m.m02;
    }
    if (out_y) {
        *out_y = (m.m10 * x) + (m.m11 * y) + m.m12;
    }
}

bool vg_framebuffer_init(VgFrameBuffer *fb, int width, int height, uint16_t *pixels, size_t pixel_count) {
    if (!fb || !pixels || width <= 0 || height <= 0) {
        return false;
    }
    if ((size_t)width * (size_t)height > pixel_count) {
        return false;
    }
    fb->width = width;
    fb->height = height;
    fb->pixels = pixels;
    fb->pixel_count = pixel_count;
    return true;
}

void vg_framebuffer_clear(VgFrameBuffer *fb, uint16_t color) {
    if (!fb || !fb->pixels) {
        return;
    }
    size_t count = (size_t)fb->width * (size_t)fb->height;
    for (size_t i = 0; i < count; i++) {
        fb->pixels[i] = color;
    }
}

uint32_t vg_framebuffer_checksum(const VgFrameBuffer *fb) {
    if (!fb || !fb->pixels) {
        return 0u;
    }
    uint32_t h = 2166136261u;
    size_t count = (size_t)fb->width * (size_t)fb->height;
    for (size_t i = 0; i < count; i++) {
        uint16_t p = fb->pixels[i];
        h ^= (uint8_t)(p & 0xffu);
        h *= 16777619u;
        h ^= (uint8_t)((p >> 8) & 0xffu);
        h *= 16777619u;
    }
    return h;
}

static uint8_t rgb565_to_r8(uint16_t c) {
    return (uint8_t)((((c >> 11) & 0x1f) * 255) / 31);
}

static uint8_t rgb565_to_g8(uint16_t c) {
    return (uint8_t)((((c >> 5) & 0x3f) * 255) / 63);
}

static uint8_t rgb565_to_b8(uint16_t c) {
    return (uint8_t)(((c & 0x1f) * 255) / 31);
}

bool vg_framebuffer_dump_ppm(const VgFrameBuffer *fb, const char *path) {
    if (!fb || !fb->pixels || !path) {
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    if (fprintf(f, "P6\n%d %d\n255\n", fb->width, fb->height) < 0) {
        fclose(f);
        return false;
    }
    for (int y = 0; y < fb->height; y++) {
        for (int x = 0; x < fb->width; x++) {
            uint16_t c = fb->pixels[(size_t)y * (size_t)fb->width + (size_t)x];
            uint8_t rgb[3] = {rgb565_to_r8(c), rgb565_to_g8(c), rgb565_to_b8(c)};
            if (fwrite(rgb, 1, sizeof(rgb), f) != sizeof(rgb)) {
                fclose(f);
                return false;
            }
        }
    }
    fclose(f);
    return true;
}

static int iroundf(float v) {
    return (int)lroundf(v);
}

#define VG_FP_SHIFT 16
#define VG_FP_ONE (1 << VG_FP_SHIFT)

static int32_t fp_from_float(float v) {
    return (int32_t)lroundf(v * (float)VG_FP_ONE);
}

static int fp_to_int_round(int32_t v) {
    if (v >= 0) {
        return (int)((v + (VG_FP_ONE / 2)) >> VG_FP_SHIFT);
    }
    return (int)((v - (VG_FP_ONE / 2)) >> VG_FP_SHIFT);
}

static int32_t fp_mul_q16(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> VG_FP_SHIFT);
}

static VgMatrix2DFixed matrix_fixed_from_transform(VgTransform t) {
    VgMatrix2D m = matrix_from_transform(t);
    VgMatrix2DFixed mf;
    mf.m00 = fp_from_float(m.m00);
    mf.m01 = fp_from_float(m.m01);
    mf.m02 = fp_from_float(m.m02);
    mf.m10 = fp_from_float(m.m10);
    mf.m11 = fp_from_float(m.m11);
    mf.m12 = fp_from_float(m.m12);
    return mf;
}

static void apply_xy_half_fixed(const VgMatrix2DFixed *m, int x_half, int y_half, int *out_x, int *out_y) {
    int32_t x_fp = ((int32_t)x_half) << (VG_FP_SHIFT - 1);
    int32_t y_fp = ((int32_t)y_half) << (VG_FP_SHIFT - 1);
    int32_t ox = fp_mul_q16(m->m00, x_fp) + fp_mul_q16(m->m01, y_fp) + m->m02;
    int32_t oy = fp_mul_q16(m->m10, x_fp) + fp_mul_q16(m->m11, y_fp) + m->m12;
    *out_x = fp_to_int_round(ox);
    *out_y = fp_to_int_round(oy);
}

static void put_pixel(VgFrameBuffer *fb, int x, int y, uint16_t color) {
    if (!fb || !fb->pixels) {
        return;
    }
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) {
        return;
    }
    fb->pixels[(size_t)y * (size_t)fb->width + (size_t)x] = color;
}

static uint16_t blend_rgb565(uint16_t bg, uint16_t fg, uint8_t alpha) {
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

static void put_pixel_aa_bg(VgFrameBuffer *fb, int x, int y, uint16_t fg, uint16_t bg, uint8_t alpha) {
    if (!fb || !fb->pixels) return;
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) return;
    fb->pixels[(size_t)y * (size_t)fb->width + (size_t)x] = blend_rgb565(bg, fg, alpha);
}

static void draw_fill_rect(VgFrameBuffer *fb, int x, int y, int w, int h, uint16_t color) {
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
            put_pixel(fb, xx, yy, color);
        }
    }
}

static void draw_line_basic(VgFrameBuffer *fb, int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (true) {
        put_pixel(fb, x0, y0, color);
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

static void draw_line_supercover(VgFrameBuffer *fb, int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0);
    int sx = (x1 >= x0) ? 1 : -1;
    int dy = abs(y1 - y0);
    int sy = (y1 >= y0) ? 1 : -1;

    int x = x0;
    int y = y0;
    put_pixel(fb, x, y, color);

    if (dx >= dy) {
        int err = dx / 2;
        for (int i = 0; i < dx; i++) {
            x += sx;
            err -= dy;
            if (err <= 0) {
                // Supercover: ensure corner transitions stay connected.
                put_pixel(fb, x, y, color);
                y += sy;
                err += dx;
            }
            put_pixel(fb, x, y, color);
        }
    } else {
        int err = dy / 2;
        for (int i = 0; i < dy; i++) {
            y += sy;
            err -= dx;
            if (err <= 0) {
                // Supercover: ensure corner transitions stay connected.
                put_pixel(fb, x, y, color);
                x += sx;
                err += dy;
            }
            put_pixel(fb, x, y, color);
        }
    }
}

static void draw_line_aa_1px_bg(VgFrameBuffer *fb,
                                int x0, int y0, int x1, int y1,
                                uint16_t fg, uint16_t bg) {
    // Always draw a crisp 1px core first, then add only anti-aliased fringe.
    draw_line_basic(fb, x0, y0, x1, y1, fg);

    float fx0 = (float)x0;
    float fy0 = (float)y0;
    float fx1 = (float)x1;
    float fy1 = (float)y1;
    float dx = fx1 - fx0;
    float dy = fy1 - fy0;
    float seg_len_sq = (dx * dx) + (dy * dy);
    if (seg_len_sq < 0.0001f) {
        put_pixel_aa_bg(fb, x0, y0, fg, bg, 255);
        return;
    }

    int min_x = (int)floorf(fminf(fx0, fx1) - 1.5f);
    int max_x = (int)ceilf(fmaxf(fx0, fx1) + 1.5f);
    int min_y = (int)floorf(fminf(fy0, fy1) - 1.5f);
    int max_y = (int)ceilf(fmaxf(fy0, fy1) + 1.5f);

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= fb->width) max_x = fb->width - 1;
    if (max_y >= fb->height) max_y = fb->height - 1;

    for (int py = min_y; py <= max_y; py++) {
        for (int px = min_x; px <= max_x; px++) {
            float cx = (float)px + 0.5f;
            float cy = (float)py + 0.5f;
            float tx = cx - fx0;
            float ty = cy - fy0;
            float t = (tx * dx + ty * dy) / seg_len_sq;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float qx = fx0 + t * dx;
            float qy = fy0 + t * dy;
            float ddx = cx - qx;
            float ddy = cy - qy;
            float dist = sqrtf((ddx * ddx) + (ddy * ddy));

            // 1px-AA with hard core + soft fringe:
            // - keep a crisp bright center
            // - only blend in the outer ring to reduce "gray line" appearance
            const float core_radius = 0.72f;
            const float fringe_end = 0.95f;
            float cov = 0.0f;
            if (dist <= core_radius) {
                cov = 1.0f;
            } else if (dist < fringe_end) {
                float t = (fringe_end - dist) / (fringe_end - core_radius);
                cov = t * t; // quadratic falloff: softer and darker fringe
            }
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            if (cov >= 0.999f) {
                // Core already drawn in solid color.
                continue;
            }
            uint8_t alpha = (uint8_t)(cov * 110.0f + 0.5f);
            if (alpha < 64) {
                // Keep only strong fringe contributions: sharper core, less gray haze.
                continue;
            }
            put_pixel_aa_bg(fb, px, py, fg, bg, alpha);
        }
    }
}

static void draw_line_thick(VgFrameBuffer *fb,
                            int x0, int y0, int x1, int y1,
                            uint16_t color, int width,
                            bool has_bg, uint16_t bg_color) {
    if (width <= 1) {
        if (has_bg) {
            draw_line_aa_1px_bg(fb, x0, y0, x1, y1, color, bg_color);
            return;
        }
        draw_line_basic(fb, x0, y0, x1, y1, color);
        return;
    }
    if (x0 == x1) {
        int y_min = (y0 < y1) ? y0 : y1;
        int y_max = (y0 > y1) ? y0 : y1;
        int half = width / 2;
        draw_fill_rect(fb, x0 - half, y_min, width, (y_max - y_min) + 1, color);
        return;
    }
    if (y0 == y1) {
        int x_min = (x0 < x1) ? x0 : x1;
        int x_max = (x0 > x1) ? x0 : x1;
        int half = width / 2;
        draw_fill_rect(fb, x_min, y0 - half, (x_max - x_min) + 1, width, color);
        return;
    }

    float fx0 = (float)x0;
    float fy0 = (float)y0;
    float fx1 = (float)x1;
    float fy1 = (float)y1;
    float dx = fx1 - fx0;
    float dy = fy1 - fy0;
    float seg_len_sq = (dx * dx) + (dy * dy);
    if (seg_len_sq < 0.0001f) {
        put_pixel(fb, x0, y0, color);
        return;
    }

    float radius = (float)width * 0.5f;
    float radius_sq = radius * radius;

    int min_x = (int)floorf(fminf(fx0, fx1) - radius - 1.0f);
    int max_x = (int)ceilf(fmaxf(fx0, fx1) + radius + 1.0f);
    int min_y = (int)floorf(fminf(fy0, fy1) - radius - 1.0f);
    int max_y = (int)ceilf(fmaxf(fy0, fy1) + radius + 1.0f);

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= fb->width) max_x = fb->width - 1;
    if (max_y >= fb->height) max_y = fb->height - 1;

    for (int py = min_y; py <= max_y; py++) {
        for (int px = min_x; px <= max_x; px++) {
            float cx = (float)px + 0.5f;
            float cy = (float)py + 0.5f;
            float tx = cx - fx0;
            float ty = cy - fy0;
            float t = (tx * dx + ty * dy) / seg_len_sq;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float qx = fx0 + t * dx;
            float qy = fy0 + t * dy;
            float ddx = cx - qx;
            float ddy = cy - qy;
            float dist_sq = (ddx * ddx) + (ddy * ddy);
            if (dist_sq <= radius_sq) {
                put_pixel(fb, px, py, color);
            }
        }
    }
}

static void apply_xy(VgTransform t, int16_t x, int16_t y, int *out_x, int *out_y) {
    float xf = 0.0f;
    float yf = 0.0f;
    vg_transform_apply(t, (float)x, (float)y, &xf, &yf);
    *out_x = iroundf(xf);
    *out_y = iroundf(yf);
}

static void draw_line_node(VgFrameBuffer *fb, const VgLineData *l, VgTransform t, VgStyle style) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    apply_xy(t, l->x1, l->y1, &x1, &y1);
    apply_xy(t, l->x2, l->y2, &x2, &y2);
    draw_line_thick(fb, x1, y1, x2, y2, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
}

static void draw_polyline_node(VgFrameBuffer *fb, const VgPolylineData *p, VgTransform t, VgStyle style) {
    if (!p->points || p->point_count < 2) {
        return;
    }
    for (size_t i = 1; i < p->point_count; i++) {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        apply_xy(t, p->points[i - 1].x, p->points[i - 1].y, &x1, &y1);
        apply_xy(t, p->points[i].x, p->points[i].y, &x2, &y2);
        draw_line_thick(fb, x1, y1, x2, y2, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    }
    if (p->closed && p->point_count > 2) {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        apply_xy(t, p->points[p->point_count - 1].x, p->points[p->point_count - 1].y, &x1, &y1);
        apply_xy(t, p->points[0].x, p->points[0].y, &x2, &y2);
        draw_line_thick(fb, x1, y1, x2, y2, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    }
}

static void draw_rect_node(VgFrameBuffer *fb, const VgRectData *r, VgTransform t, VgStyle style) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, x4 = 0, y4 = 0;
    apply_xy(t, r->x, r->y, &x1, &y1);
    apply_xy(t, (int16_t)(r->x + r->w), r->y, &x2, &y2);
    apply_xy(t, (int16_t)(r->x + r->w), (int16_t)(r->y + r->h), &x3, &y3);
    apply_xy(t, r->x, (int16_t)(r->y + r->h), &x4, &y4);
    draw_line_thick(fb, x1, y1, x2, y2, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    draw_line_thick(fb, x2, y2, x3, y3, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    draw_line_thick(fb, x3, y3, x4, y4, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    draw_line_thick(fb, x4, y4, x1, y1, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
}

static void draw_tri_node(VgFrameBuffer *fb, const VgTriData *tr, VgTransform t, VgStyle style) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0;
    apply_xy(t, tr->x1, tr->y1, &x1, &y1);
    apply_xy(t, tr->x2, tr->y2, &x2, &y2);
    apply_xy(t, tr->x3, tr->y3, &x3, &y3);
    draw_line_thick(fb, x1, y1, x2, y2, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    draw_line_thick(fb, x2, y2, x3, y3, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    draw_line_thick(fb, x3, y3, x1, y1, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
}

static void draw_text_node(VgFrameBuffer *fb, const VgTextData *txt, VgTransform parent_t, VgStyle style) {
    if (!txt->text || txt->text[0] == '\0') {
        return;
    }
    VgTransform local = vg_transform_identity();
    local.tx = (float)txt->x;
    local.ty = (float)txt->y;
    local.sx = (txt->scale > 0.0f) ? txt->scale : 1.0f;
    local.sy = (txt->scale > 0.0f) ? txt->scale : 1.0f;
    local.rot_deg = txt->rot_deg;
    VgTransform t = vg_transform_compose(parent_t, local);
    VgMatrix2DFixed t_fixed = matrix_fixed_from_transform(t);

    // Text-specific line draw wrapper:
    // - for 1px no-AA use supercover to avoid simple Bresenham dropouts
    // - at larger non-integer scales (e.g. 1.4), draw diagonal strokes with a
    //   minimal extra thickness to prevent intermittent missing pixels.
    #define DRAW_TEXT_SEGMENT(xa, ya, xb, yb, st) do { \
        if ((st).stroke_width <= 1 && !(st).has_bg_rgb565) { \
            int _dx = abs((xb) - (xa)); \
            int _dy = abs((yb) - (ya)); \
            if (txt->scale >= 1.25f && _dx > 0 && _dy > 0) { \
                draw_line_thick(fb, (xa), (ya), (xb), (yb), (st).stroke_rgb565, 2, false, (st).bg_rgb565); \
            } else { \
                draw_line_supercover(fb, (xa), (ya), (xb), (yb), (st).stroke_rgb565); \
            } \
        } else { \
            draw_line_thick(fb, (xa), (ya), (xb), (yb), (st).stroke_rgb565, (int)(st).stroke_width, (st).has_bg_rgb565, (st).bg_rgb565); \
        } \
    } while (0)

    int pen_x = 0;
    size_t len = strlen(txt->text);
    for (size_t i = 0; i < len; i++) {
        unsigned char uc = (unsigned char)txt->text[i];
        char c = (char)toupper((int)uc);
        // Digits and uppercase letters are always rendered with the HV glyph set.
        bool is_hv_mono_alnum = ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'));
        const int hv_mono_lsb = 1;
        const int hv_mono_advance = 10;
        int x0 = pen_x + (is_hv_mono_alnum ? hv_mono_lsb : 0);
        int adv = 8;
        bool used_hershey = false;
        VgStyle glyph_style = style;
        if (is_hv_mono_alnum && txt->scale >= 1.25f && glyph_style.stroke_width <= 1) {
            // At larger scales the 1px AA fringe can look fuzzy/noisy for
            // orthogonal line fonts. Keep these glyphs crisp.
            glyph_style.has_bg_rgb565 = false;
        }

#define GL(x1, y1, x2, y2) do { \
            int gx1 = 0, gy1 = 0, gx2 = 0, gy2 = 0; \
            apply_xy_half_fixed(&t_fixed, ((x0 + (x1)) * 2), ((y1) * 2), &gx1, &gy1); \
            apply_xy_half_fixed(&t_fixed, ((x0 + (x2)) * 2), ((y2) * 2), &gx2, &gy2); \
            DRAW_TEXT_SEGMENT(gx1, gy1, gx2, gy2, glyph_style); \
        } while (0)
#define GLH(x1h, y1h, x2h, y2h) do { \
            int gx1 = 0, gy1 = 0, gx2 = 0, gy2 = 0; \
            apply_xy_half_fixed(&t_fixed, ((x0 * 2) + (x1h)), (y1h), &gx1, &gy1); \
            apply_xy_half_fixed(&t_fixed, ((x0 * 2) + (x2h)), (y2h), &gx2, &gy2); \
            DRAW_TEXT_SEGMENT(gx1, gy1, gx2, gy2, glyph_style); \
        } while (0)

        // For A-Z/0-9 we intentionally prefer the arcadefont
        // monospace single-stroke glyph set. Hershey is used
        // as fallback for other symbols.
        if (!is_hv_mono_alnum) {
            for (int gi = 0; gi < VG_HERSHEY_SUBSET_COUNT; gi++) {
                if (vg_hershey_subset_chars[gi] != c) {
                    continue;
                }
                const VgHersheyGlyph *g = &vg_hershey_subset_glyphs[gi];
                // Normalize Hershey units to the fallback glyph scale.
                const int unit_div = 2;
                adv = ((int)g->advance + (unit_div - 1)) / unit_div;
                if (adv < 3) adv = 3;
                for (int si = 0; si < (int)g->seg_count; si++) {
                    const VgHersheySeg *s = &vg_hershey_subset_segs[g->seg_offset + (uint16_t)si];
                    int x1 = s->x1 / unit_div;
                    int y1 = s->y1 / unit_div;
                    int x2 = s->x2 / unit_div;
                    int y2 = s->y2 / unit_div;
                    GL(x1, y1, x2, y2);
                }
                used_hershey = true;
                break;
            }
        }

        if (used_hershey) {
            pen_x += adv;
            if (pen_x < 0) pen_x = 0;
            continue;
        }

        switch (c) {
            case ' ': adv = 5; break;
            case '.': GL(2, 9, 2, 9); adv = 4; break;
            case ':': GL(2, 3, 2, 3); GL(2, 7, 2, 7); adv = 4; break;
            case '-': GL(1, 5, 4, 5); break;
            case '_': GL(0, 10, 5, 10); break;
            case '/': GL(0, 10, 5, 0); break;
            case '\\': GL(0, 0, 5, 10); break;

            case 'A':
                GL(0, 8, 0, 2);
                GL(0, 2, 4, 0);
                GL(4, 0, 8, 2);
                GL(8, 2, 8, 8);
                GL(0, 5, 8, 5);
                adv = 10;
                break;
            case 'B':
                GL(0, 8, 0, 0);
                GL(0, 0, 5, 0);
                GL(5, 0, 7, 2);
                GL(7, 2, 5, 4);
                GL(5, 4, 0, 4);
                GL(6, 4, 8, 6);
                GL(8, 6, 6, 8);
                GL(6, 8, 0, 8);
                adv = 10;
                break;
            case 'C':
                GL(8, 0, 0, 0);
                GL(0, 0, 0, 8);
                GL(0, 8, 8, 8);
                adv = 10;
                break;
            case 'D':
                GL(0, 8, 0, 0);
                GL(0, 0, 5, 0);
                GL(5, 0, 8, 3);
                GL(8, 3, 8, 5);
                GL(8, 5, 5, 8);
                GL(5, 8, 0, 8);
                adv = 10;
                break;
            case 'E':
                GL(8, 0, 0, 0);
                GL(0, 0, 0, 8);
                GL(0, 8, 8, 8);
                GL(0, 4, 6, 4);
                adv = 10;
                break;
            case 'F':
                GL(8, 0, 0, 0);
                GL(0, 0, 0, 8);
                GL(0, 4, 6, 4);
                adv = 10;
                break;
            case 'G':
                GL(8, 0, 0, 0);
                GL(0, 0, 0, 8);
                GL(0, 8, 8, 8);
                GL(8, 8, 8, 5);
                GL(8, 5, 4, 5);
                adv = 10;
                break;
            case 'H':
                GL(0, 8, 0, 0);
                GL(8, 8, 8, 0);
                GL(0, 4, 8, 4);
                adv = 10;
                break;
            case 'I':
                GL(0, 8, 8, 8);
                GL(0, 0, 8, 0);
                GL(4, 8, 4, 0);
                adv = 10;
                break;
            case 'J':
                GL(8, 0, 8, 8);
                GL(8, 8, 4, 8);
                GL(4, 8, 0, 5);
                adv = 10;
                break;
            case 'K':
                GL(0, 8, 0, 0);
                GL(8, 0, 0, 4);
                GL(0, 4, 8, 8);
                adv = 10;
                break;
            case 'L':
                GL(0, 0, 0, 8);
                GL(0, 8, 8, 8);
                adv = 10;
                break;
            case 'M':
                GL(0, 8, 0, 0);
                GL(0, 0, 4, 3);
                GL(4, 3, 8, 0);
                GL(8, 0, 8, 8);
                adv = 10;
                break;
            case 'N':
                GL(0, 8, 0, 0);
                GL(0, 0, 8, 8);
                GL(8, 8, 8, 0);
                adv = 10;
                break;
            case 'O':
                GL(0, 8, 8, 8);
                GL(8, 8, 8, 0);
                GL(8, 0, 0, 0);
                GL(0, 0, 0, 8);
                adv = 10;
                break;
            case 'P':
                GL(0, 8, 0, 0);
                GL(0, 0, 8, 0);
                GL(8, 0, 8, 4);
                GL(8, 4, 0, 4);
                adv = 10;
                break;
            case 'Q':
                GL(0, 8, 0, 0);
                GL(0, 0, 8, 0);
                GL(8, 0, 8, 5);
                GL(8, 5, 4, 8);
                GL(4, 8, 0, 8);
                GL(4, 5, 8, 8);
                adv = 10;
                break;
            case 'R':
                GL(0, 8, 0, 0);
                GL(0, 0, 8, 0);
                GL(8, 0, 8, 4);
                GL(8, 4, 0, 4);
                GL(0, 4, 8, 8);
                adv = 10;
                break;
            case 'S':
                GL(0, 8, 8, 8);
                GL(8, 8, 8, 4);
                GL(8, 4, 0, 4);
                GL(0, 4, 0, 0);
                GL(0, 0, 8, 0);
                adv = 10;
                break;
            case 'T':
                GL(0, 0, 8, 0);
                GL(4, 8, 4, 0);
                adv = 10;
                break;
            case 'U':
                GL(0, 0, 0, 8);
                GL(0, 8, 8, 8);
                GL(8, 8, 8, 0);
                adv = 10;
                break;
            case 'V':
                GL(0, 0, 4, 8);
                GL(4, 8, 8, 0);
                adv = 10;
                break;
            case 'W':
                GL(0, 0, 0, 8);
                GL(0, 8, 4, 5);
                GL(4, 5, 8, 8);
                GL(8, 8, 8, 0);
                adv = 10;
                break;
            case 'X':
                GL(0, 8, 8, 0);
                GL(0, 0, 8, 8);
                adv = 10;
                break;
            case 'Y':
                GL(0, 0, 4, 3);
                GL(4, 3, 8, 0);
                GL(4, 3, 4, 8);
                adv = 10;
                break;
            case 'Z':
                GL(0, 0, 8, 0);
                GL(8, 0, 0, 8);
                GL(0, 8, 8, 8);
                adv = 10;
                break;
            case '0':
                GL(0, 8, 8, 8);
                GL(8, 8, 8, 0);
                GL(8, 0, 0, 0);
                GL(0, 0, 0, 8);
                adv = 10;
                break;
            case '1':
                GL(0, 8, 8, 8);
                GL(4, 8, 4, 0);
                GL(4, 0, 2, 2);
                adv = 10;
                break;
            case '2':
                GL(0, 0, 8, 0);
                GL(8, 0, 8, 4);
                GL(8, 4, 0, 4);
                GL(0, 4, 0, 8);
                GL(0, 8, 8, 8);
                adv = 10;
                break;
            case '3':
                GL(0, 0, 8, 0);
                GL(8, 0, 8, 8);
                GL(8, 8, 0, 8);
                GL(0, 4, 8, 4);
                adv = 10;
                break;
            case '4':
                GL(0, 0, 0, 4);
                GL(0, 4, 8, 4);
                GL(8, 0, 8, 8);
                adv = 10;
                break;
            case '5':
                GL(0, 8, 8, 8);
                GL(8, 8, 8, 4);
                GL(8, 4, 0, 4);
                GL(0, 4, 0, 0);
                GL(0, 0, 8, 0);
                adv = 10;
                break;
            case '6':
                GL(0, 0, 0, 8);
                GL(0, 8, 8, 8);
                GL(8, 8, 8, 4);
                GL(8, 4, 0, 4);
                adv = 10;
                break;
            case '7':
                GL(0, 0, 8, 0);
                GL(8, 0, 8, 8);
                adv = 10;
                break;
            case '8':
                GL(0, 8, 0, 0);
                GL(0, 0, 8, 0);
                GL(8, 0, 8, 8);
                GL(8, 8, 0, 8);
                GL(0, 4, 8, 4);
                adv = 10;
                break;
            case '9':
                GL(8, 8, 8, 0);
                GL(8, 0, 0, 0);
                GL(0, 0, 0, 4);
                GL(0, 4, 8, 4);
                adv = 10;
                break;

            default:
                GL(0, 0, 5, 0); GL(5, 0, 5, 10); GL(5, 10, 0, 10); GL(0, 10, 0, 0);
                break;
        }

        if (is_hv_mono_alnum) {
            adv = hv_mono_advance;
        }
        pen_x += adv;
        if (pen_x < 0) pen_x = 0;
#undef GL
#undef GLH
#undef DRAW_TEXT_SEGMENT
    }
}

static void render_node(const VgNode *node, VgTransform parent_t, VgFrameBuffer *fb) {
    if (!node || !fb) {
        return;
    }
    VgStyle style = node->style;
    if (style.stroke_width == 0) {
        style.stroke_width = 1;
    }
    if (!style.visible) {
        return;
    }
    VgTransform node_t = parent_t;
    if (node->has_transform) {
        node_t = vg_transform_compose(parent_t, node->transform);
    }

    switch (node->type) {
        case VG_NODE_GROUP:
            for (size_t i = 0; i < node->data.group.child_count; i++) {
                render_node(node->data.group.children[i], node_t, fb);
            }
            break;
        case VG_NODE_LINE:
            draw_line_node(fb, &node->data.line, node_t, style);
            break;
        case VG_NODE_POLYLINE:
            draw_polyline_node(fb, &node->data.polyline, node_t, style);
            break;
        case VG_NODE_RECT:
            draw_rect_node(fb, &node->data.rect, node_t, style);
            break;
        case VG_NODE_TRI:
            draw_tri_node(fb, &node->data.tri, node_t, style);
            break;
        case VG_NODE_VTEXT:
            draw_text_node(fb, &node->data.text, node_t, style);
            break;
        default:
            break;
    }
}

void vg_render_scene(const VgNode *root, VgFrameBuffer *fb) {
    if (!root || !fb) {
        return;
    }
    render_node(root, vg_transform_identity(), fb);
}

static VgNode *find_node_by_id(VgNode *node, uint32_t id) {
    if (!node) {
        return NULL;
    }
    if (node->id == id) {
        return node;
    }
    if (node->type == VG_NODE_GROUP) {
        for (size_t i = 0; i < node->data.group.child_count; i++) {
            VgNode *found = find_node_by_id(node->data.group.children[i], id);
            if (found) {
                return found;
            }
        }
    }
    return NULL;
}

bool vg_scene_apply_patch(VgNode *root, const VgPatch *patch) {
    if (!root || !patch) {
        return false;
    }
    VgNode *target = find_node_by_id(root, patch->id);
    if (!target) {
        return false;
    }
    switch (patch->type) {
        case VG_PATCH_TRANSFORM:
            target->has_transform = true;
            target->transform = patch->value.transform;
            return true;
        case VG_PATCH_TEXT:
            if (target->type != VG_NODE_VTEXT) {
                return false;
            }
            target->data.text.text = patch->value.text;
            return true;
        case VG_PATCH_VISIBILITY:
            target->style.visible = patch->value.visible;
            return true;
        case VG_PATCH_STYLE:
            target->style = patch->value.style;
            if (target->style.stroke_width == 0) {
                target->style.stroke_width = 1;
            }
            return true;
        default:
            return false;
    }
}
