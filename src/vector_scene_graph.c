#include "vector_scene_graph.h"
#include "gfx.h"
#include "value.h"

#include <ctype.h>
#include <limits.h>
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

static GfxClip g_active_clip = {false, 0, 0, 0, 0};

static bool clip_rect_is_empty(VgClipRect r) {
    return r.w <= 0 || r.h <= 0;
}

static bool clip_rect_equal(VgClipRect a, VgClipRect b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static VgClipRect clip_rect_expand(VgClipRect r, uint8_t guard_px) {
    if (clip_rect_is_empty(r) || guard_px == 0) {
        return r;
    }
    int gx = (int)guard_px;
    int x0 = (int)r.x - gx;
    int y0 = (int)r.y - gx;
    int x1 = (int)r.x + (int)r.w + gx;
    int y1 = (int)r.y + (int)r.h + gx;
    if (x0 < INT16_MIN) x0 = INT16_MIN;
    if (y0 < INT16_MIN) y0 = INT16_MIN;
    if (x1 > INT16_MAX) x1 = INT16_MAX;
    if (y1 > INT16_MAX) y1 = INT16_MAX;
    VgClipRect out;
    out.x = (int16_t)x0;
    out.y = (int16_t)y0;
    out.w = (int16_t)(x1 - x0);
    out.h = (int16_t)(y1 - y0);
    return out;
}

static VgClipRect clip_rect_union(VgClipRect a, VgClipRect b) {
    if (clip_rect_is_empty(a)) return b;
    if (clip_rect_is_empty(b)) return a;
    int ax1 = (int)a.x + (int)a.w;
    int ay1 = (int)a.y + (int)a.h;
    int bx1 = (int)b.x + (int)b.w;
    int by1 = (int)b.y + (int)b.h;
    int x0 = ((int)a.x < (int)b.x) ? (int)a.x : (int)b.x;
    int y0 = ((int)a.y < (int)b.y) ? (int)a.y : (int)b.y;
    int x1 = (ax1 > bx1) ? ax1 : bx1;
    int y1 = (ay1 > by1) ? ay1 : by1;
    VgClipRect out;
    out.x = (int16_t)x0;
    out.y = (int16_t)y0;
    out.w = (int16_t)(x1 - x0);
    out.h = (int16_t)(y1 - y0);
    return out;
}

static bool clip_rect_intersect_fb(VgClipRect in, const VgFrameBuffer *fb, VgClipRect *out) {
    if (!fb || !out || clip_rect_is_empty(in)) {
        return false;
    }
    int x0 = (int)in.x;
    int y0 = (int)in.y;
    int x1 = (int)in.x + (int)in.w;
    int y1 = (int)in.y + (int)in.h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb->width) x1 = fb->width;
    if (y1 > fb->height) y1 = fb->height;
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }
    out->x = (int16_t)x0;
    out->y = (int16_t)y0;
    out->w = (int16_t)(x1 - x0);
    out->h = (int16_t)(y1 - y0);
    return true;
}
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
    s.has_fill = false;
    s.fill_rgb565 = 0x0000u;
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

#define VG_FP_SHIFT CLJ_FIXED_FRAC_BITS
#define VG_FP_ONE CLJ_FIXED_SCALE

static int32_t fp_from_float(float v) {
    return (int32_t)lroundf(v * (float)VG_FP_ONE);
}

static int fp_to_int_round(int32_t v) {
    if (v >= 0) {
        return (int)((v + (VG_FP_ONE / 2)) >> VG_FP_SHIFT);
    }
    return (int)((v - (VG_FP_ONE / 2)) >> VG_FP_SHIFT);
}

static int32_t fp_mul_fixed(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> VG_FP_SHIFT);
}

VgTransformFixed vg_transform_fixed_identity(void) {
    VgTransformFixed t;
    t.m00 = VG_FP_ONE;
    t.m01 = 0;
    t.m02 = 0;
    t.m10 = 0;
    t.m11 = VG_FP_ONE;
    t.m12 = 0;
    return t;
}

VgTransformFixed vg_transform_fixed_compose(VgTransformFixed parent, VgTransformFixed local) {
    VgTransformFixed m;
    m.m00 = fp_mul_fixed(parent.m00, local.m00) + fp_mul_fixed(parent.m01, local.m10);
    m.m01 = fp_mul_fixed(parent.m00, local.m01) + fp_mul_fixed(parent.m01, local.m11);
    m.m02 = fp_mul_fixed(parent.m00, local.m02) + fp_mul_fixed(parent.m01, local.m12) + parent.m02;
    m.m10 = fp_mul_fixed(parent.m10, local.m00) + fp_mul_fixed(parent.m11, local.m10);
    m.m11 = fp_mul_fixed(parent.m10, local.m01) + fp_mul_fixed(parent.m11, local.m11);
    m.m12 = fp_mul_fixed(parent.m10, local.m02) + fp_mul_fixed(parent.m11, local.m12) + parent.m12;
    return m;
}

VgTransformFixed vg_transform_fixed_from_transform(VgTransform t) {
    if (t.rot_deg == 0.0f) {
        VgTransformFixed mf = vg_transform_fixed_identity();
        mf.m00 = fp_from_float(t.sx);
        mf.m11 = fp_from_float(t.sy);
        mf.m02 = fp_from_float(t.tx);
        mf.m12 = fp_from_float(t.ty);
        return mf;
    }
    VgMatrix2D m = matrix_from_transform(t);
    VgTransformFixed mf;
    mf.m00 = fp_from_float(m.m00);
    mf.m01 = fp_from_float(m.m01);
    mf.m02 = fp_from_float(m.m02);
    mf.m10 = fp_from_float(m.m10);
    mf.m11 = fp_from_float(m.m11);
    mf.m12 = fp_from_float(m.m12);
    return mf;
}

static void apply_xy_half_fixed(const VgTransformFixed *m, int x_half, int y_half, int *out_x, int *out_y) {
    int32_t x_fp = ((int32_t)x_half) << (VG_FP_SHIFT - 1);
    int32_t y_fp = ((int32_t)y_half) << (VG_FP_SHIFT - 1);
    int32_t ox = fp_mul_fixed(m->m00, x_fp) + fp_mul_fixed(m->m01, y_fp) + m->m02;
    int32_t oy = fp_mul_fixed(m->m10, x_fp) + fp_mul_fixed(m->m11, y_fp) + m->m12;
    *out_x = fp_to_int_round(ox);
    *out_y = fp_to_int_round(oy);
}

void vg_transform_fixed_apply_px(VgTransformFixed t, int16_t x, int16_t y, int *out_x, int *out_y) {
    int32_t x_fp = ((int32_t)x) << VG_FP_SHIFT;
    int32_t y_fp = ((int32_t)y) << VG_FP_SHIFT;
    int32_t ox = fp_mul_fixed(t.m00, x_fp) + fp_mul_fixed(t.m01, y_fp) + t.m02;
    int32_t oy = fp_mul_fixed(t.m10, x_fp) + fp_mul_fixed(t.m11, y_fp) + t.m12;
    if (out_x) {
        *out_x = fp_to_int_round(ox);
    }
    if (out_y) {
        *out_y = fp_to_int_round(oy);
    }
}

static void framebuffer_clear_rect(VgFrameBuffer *fb, VgClipRect rect, uint16_t color) {
    if (!fb || !fb->pixels || clip_rect_is_empty(rect)) {
        return;
    }
    VgClipRect clipped;
    if (!clip_rect_intersect_fb(rect, fb, &clipped)) {
        return;
    }
    int x0 = clipped.x;
    int y0 = clipped.y;
    int x1 = clipped.x + clipped.w;
    int y1 = clipped.y + clipped.h;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            fb->pixels[(size_t)y * (size_t)fb->width + (size_t)x] = color;
        }
    }
}

static void draw_fill_rect(VgFrameBuffer *fb, int x, int y, int w, int h, uint16_t color) {
    gfx_draw_fill_rect(fb, x, y, w, h, color, &g_active_clip);
}

static void fill_polygon_scanline(VgFrameBuffer *fb,
                                  const int *vx,
                                  const int *vy,
                                  size_t count,
                                  uint16_t color) {
    gfx_fill_polygon_scanline(fb, vx, vy, count, color, &g_active_clip);
}

static void draw_line_supercover(VgFrameBuffer *fb, int x0, int y0, int x1, int y1, uint16_t color) {
    gfx_draw_line_supercover(fb, x0, y0, x1, y1, color, &g_active_clip);
}

static void draw_line_thick(VgFrameBuffer *fb,
                            int x0, int y0, int x1, int y1,
                            uint16_t color, int width,
                            bool has_bg, uint16_t bg_color) {
    gfx_draw_line_thick(fb, x0, y0, x1, y1, color, width, has_bg, bg_color, &g_active_clip);
}

static void apply_xy_fixed_px(const VgTransformFixed *m, int16_t x, int16_t y, int *out_x, int *out_y) {
    vg_transform_fixed_apply_px(*m, x, y, out_x, out_y);
}

static void draw_stroke_polyline_xy(VgFrameBuffer *fb,
                                    const int *vx,
                                    const int *vy,
                                    size_t count,
                                    bool closed,
                                    VgStyle style) {
    if (!fb || !vx || !vy || count < 2) {
        return;
    }
    for (size_t i = 1; i < count; i++) {
        draw_line_thick(fb,
                        vx[i - 1], vy[i - 1],
                        vx[i], vy[i],
                        style.stroke_rgb565,
                        (int)style.stroke_width,
                        style.has_bg_rgb565,
                        style.bg_rgb565);
    }
    if (closed && count > 2) {
        draw_line_thick(fb,
                        vx[count - 1], vy[count - 1],
                        vx[0], vy[0],
                        style.stroke_rgb565,
                        (int)style.stroke_width,
                        style.has_bg_rgb565,
                        style.bg_rgb565);
    }
}

static bool transform_polyline_points_fixed(const VgPolylineData *p,
                                            VgTransformFixed tf,
                                            int *vx,
                                            int *vy) {
    if (!p || !vx || !vy || !p->points || p->point_count == 0 || p->point_count > GFX_FILL_MAX_VERTS) {
        return false;
    }
    for (size_t i = 0; i < p->point_count; i++) {
        apply_xy_fixed_px(&tf, p->points[i].x, p->points[i].y, &vx[i], &vy[i]);
    }
    return true;
}

static void draw_line_node(VgFrameBuffer *fb, const VgLineData *l, VgTransformFixed tf, VgStyle style) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    apply_xy_fixed_px(&tf, l->x1, l->y1, &x1, &y1);
    apply_xy_fixed_px(&tf, l->x2, l->y2, &x2, &y2);
    draw_line_thick(fb, x1, y1, x2, y2, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
}

static void draw_polyline_node(VgFrameBuffer *fb, const VgPolylineData *p, VgTransformFixed tf, VgStyle style) {
    if (!p->points || p->point_count < 2) {
        return;
    }
    if (p->point_count <= GFX_FILL_MAX_VERTS) {
        int vx[GFX_FILL_MAX_VERTS];
        int vy[GFX_FILL_MAX_VERTS];
        if (transform_polyline_points_fixed(p, tf, vx, vy)) {
            if (p->closed && style.has_fill && p->point_count >= 3) {
                fill_polygon_scanline(fb, vx, vy, p->point_count, style.fill_rgb565);
            }
            draw_stroke_polyline_xy(fb, vx, vy, p->point_count, p->closed, style);
            return;
        }
    }
    for (size_t i = 1; i < p->point_count; i++) {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        apply_xy_fixed_px(&tf, p->points[i - 1].x, p->points[i - 1].y, &x1, &y1);
        apply_xy_fixed_px(&tf, p->points[i].x, p->points[i].y, &x2, &y2);
        draw_line_thick(fb, x1, y1, x2, y2, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    }
    if (p->closed && p->point_count > 2) {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        apply_xy_fixed_px(&tf, p->points[p->point_count - 1].x, p->points[p->point_count - 1].y, &x1, &y1);
        apply_xy_fixed_px(&tf, p->points[0].x, p->points[0].y, &x2, &y2);
        draw_line_thick(fb, x1, y1, x2, y2, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    }
}

static void draw_rect_node(VgFrameBuffer *fb, const VgRectData *r, VgTransformFixed tf, VgStyle style) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, x4 = 0, y4 = 0;
    apply_xy_fixed_px(&tf, r->x, r->y, &x1, &y1);
    apply_xy_fixed_px(&tf, (int16_t)(r->x + r->w), r->y, &x2, &y2);
    apply_xy_fixed_px(&tf, (int16_t)(r->x + r->w), (int16_t)(r->y + r->h), &x3, &y3);
    apply_xy_fixed_px(&tf, r->x, (int16_t)(r->y + r->h), &x4, &y4);
    if (style.has_fill) {
        int vx[4] = {x1, x2, x3, x4};
        int vy[4] = {y1, y2, y3, y4};
        fill_polygon_scanline(fb, vx, vy, 4, style.fill_rgb565);
    }
    draw_line_thick(fb, x1, y1, x2, y2, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    draw_line_thick(fb, x2, y2, x3, y3, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    draw_line_thick(fb, x3, y3, x4, y4, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
    draw_line_thick(fb, x4, y4, x1, y1, style.stroke_rgb565, (int)style.stroke_width, style.has_bg_rgb565, style.bg_rgb565);
}

static void draw_tri_node(VgFrameBuffer *fb, const VgTriData *tr, VgTransformFixed tf, VgStyle style) {
    int vx[3] = {0, 0, 0};
    int vy[3] = {0, 0, 0};
    apply_xy_fixed_px(&tf, tr->x1, tr->y1, &vx[0], &vy[0]);
    apply_xy_fixed_px(&tf, tr->x2, tr->y2, &vx[1], &vy[1]);
    apply_xy_fixed_px(&tf, tr->x3, tr->y3, &vx[2], &vy[2]);
    if (style.has_fill) {
        fill_polygon_scanline(fb, vx, vy, 3, style.fill_rgb565);
    }
    draw_stroke_polyline_xy(fb, vx, vy, 3, true, style);
}

static void draw_text_node(VgFrameBuffer *fb, const VgTextData *txt, VgTransformFixed parent_t, VgStyle style) {
    if (!txt->text || txt->text[0] == '\0') {
        return;
    }
    float scale_f = (txt->scale > 0.0f) ? txt->scale : 1.0f;
    int32_t scale_fp = fp_from_float(scale_f);
    bool text_scale_large = (scale_fp >= ((VG_FP_ONE * 5) / 4));

    VgTransformFixed local = vg_transform_fixed_identity();
    local.m02 = ((int32_t)txt->x) << VG_FP_SHIFT;
    local.m12 = ((int32_t)txt->y) << VG_FP_SHIFT;
    local.m00 = scale_fp;
    local.m11 = scale_fp;
    if (txt->rot_deg != 0.0f) {
        VgTransform local_f = vg_transform_identity();
        local_f.tx = (float)txt->x;
        local_f.ty = (float)txt->y;
        local_f.sx = scale_f;
        local_f.sy = scale_f;
        local_f.rot_deg = txt->rot_deg;
        local = vg_transform_fixed_from_transform(local_f);
    }
    VgTransformFixed t_fixed = vg_transform_fixed_compose(parent_t, local);

    // Text-specific line draw wrapper:
    // - for 1px no-AA use supercover to avoid simple Bresenham dropouts
    // - at larger non-integer scales (e.g. 1.4), draw diagonal strokes with a
    //   minimal extra thickness to prevent intermittent missing pixels.
    #define DRAW_TEXT_SEGMENT(xa, ya, xb, yb, st) do { \
        if ((st).stroke_width <= 1 && !(st).has_bg_rgb565) { \
            int _dx = abs((xb) - (xa)); \
            int _dy = abs((yb) - (ya)); \
            if (text_scale_large && !is_arcade_ascii_glyph && _dx > 0 && _dy > 0) { \
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
        bool is_arcade_ascii_symbol =
            (c == ' ') || (c == '.') || (c == ',') || (c == ':') || (c == ';') ||
            (c == '!') || (c == '?') || (c == '%') ||
            (c == '(') || (c == ')') || (c == '-') || (c == '_') ||
            (c == '/') || (c == '\\');
        bool is_arcade_ascii_glyph = is_hv_mono_alnum || is_arcade_ascii_symbol;
        VgStyle glyph_style = style;
        if (is_arcade_ascii_glyph && glyph_style.stroke_width == 1) {
            // The arcade glyph set is built from crisp grid segments; the
            // generic 1px AA fringe produces visible halos (e.g. "FPS: 59.9")
            // and frays tiny punctuation blobs. Keep arcade glyphs crisp.
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
#define GLCELLBOX(x, y) do { \
            if (glyph_style.stroke_width == 1 && t_fixed.m01 == 0 && t_fixed.m10 == 0) { \
                int _gx0 = 0, _gy0 = 0, _gx1 = 0, _gy1 = 0; \
                apply_xy_half_fixed(&t_fixed, ((x0 + (x)) * 2), ((y) * 2), &_gx0, &_gy0); \
                apply_xy_half_fixed(&t_fixed, ((x0 + ((x) + 1)) * 2), (((y) + 1) * 2), &_gx1, &_gy1); \
                int _xmin = (_gx0 < _gx1) ? _gx0 : _gx1; \
                int _xmax = (_gx0 > _gx1) ? _gx0 : _gx1; \
                int _ymin = (_gy0 < _gy1) ? _gy0 : _gy1; \
                int _ymax = (_gy0 > _gy1) ? _gy0 : _gy1; \
                draw_fill_rect(fb, _xmin, _ymin, (_xmax - _xmin) + 1, (_ymax - _ymin) + 1, glyph_style.stroke_rgb565); \
            } else { \
                GL((x), (y), (x), ((y) + 1)); \
                GL((x), ((y) + 1), ((x) + 1), ((y) + 1)); \
                GL(((x) + 1), ((y) + 1), ((x) + 1), (y)); \
                GL(((x) + 1), (y), (x), (y)); \
            } \
        } while (0)

        switch (c) {
            case ' ': adv = 5; break;
            case '.':
                /* Arcade 1x1 square at bottom of cell. */
                GLCELLBOX(2, 7);
                adv = 4;
                break;
            case ',':
                /* Arcade small square + tail. */
                GLCELLBOX(2, 7);
                GL(3, 8, 2, 9);
                adv = 4;
                break;
            case ':':
                /* Vertically aligned compact squares (arcade-style punctuation). */
                GLCELLBOX(2, 2);
                GLCELLBOX(2, 5);
                adv = 4;
                break;
            case ';':
                /* Vertically aligned compact squares + tail. */
                GLCELLBOX(2, 2);
                GLCELLBOX(2, 5);
                GL(1, 6, 0, 8);
                adv = 4;
                break;
            case '!':
                /* Arcade-style exclamation: center stem + bottom square. */
                GL(2, 0, 2, 5);
                GLCELLBOX(2, 7);
                adv = 4;
                break;
            case '?':
                /* Arcade-style question mark with detached square dot. */
                GL(1, 1, 2, 1);
                GL(2, 1, 3, 2);
                GL(3, 2, 3, 3);
                GL(3, 3, 2, 4);
                GL(2, 4, 1, 4);
                GL(2, 4, 2, 6);
                GL(1, 1, 0, 2);
                GL(0, 2, 0, 3);
                GL(0, 3, 1, 4);
                GLCELLBOX(2, 7);
                adv = 6;
                break;
            case '%':
                GL(0, 8, 5, 0);
                GLCELLBOX(0, 1);
                GLCELLBOX(4, 6);
                adv = 7;
                break;
            case '(':
                GLH(6, 0, 4, 2);
                GLH(4, 2, 2, 6);
                GLH(2, 6, 2, 12);
                GLH(2, 12, 4, 16);
                GLH(4, 16, 6, 18);
                adv = 4;
                break;
            case ')':
                GLH(2, 0, 4, 2);
                GLH(4, 2, 6, 6);
                GLH(6, 6, 6, 12);
                GLH(6, 12, 4, 16);
                GLH(4, 16, 2, 18);
                adv = 4;
                break;
            case '-': GL(2, 5, 5, 5); break;
            case '_': GL(0, 10, 5, 10); break;
            case '/': GL(1, 9, 6, 0); break;
            case '\\': GL(1, 0, 6, 9); break;

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
                /* Arcade: 88 80 40 03 (y-flipped into renderer coordinates). */
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
#undef GLCELLBOX
#undef DRAW_TEXT_SEGMENT
    }
}

static void render_node(const VgNode *node, VgTransformFixed parent_t, VgFrameBuffer *fb) {
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
    VgTransformFixed node_t = parent_t;
    if (node->has_transform) {
        VgTransformFixed local_t = vg_transform_fixed_from_transform(node->transform);
        node_t = vg_transform_fixed_compose(parent_t, local_t);
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
    GfxClip prev_clip = g_active_clip;
    g_active_clip.enabled = false;
    render_node(root, vg_transform_fixed_identity(), fb);
    g_active_clip = prev_clip;
}

void vg_render_scene_clipped(const VgNode *root, VgFrameBuffer *fb, VgClipRect clip_rect) {
    if (!root || !fb) {
        return;
    }
    VgClipRect clipped;
    if (!clip_rect_intersect_fb(clip_rect, fb, &clipped)) {
        return;
    }
    GfxClip prev_clip = g_active_clip;
    g_active_clip.enabled = true;
    g_active_clip.x0 = clipped.x;
    g_active_clip.y0 = clipped.y;
    g_active_clip.x1 = clipped.x + clipped.w;
    g_active_clip.y1 = clipped.y + clipped.h;
    render_node(root, vg_transform_fixed_identity(), fb);
    g_active_clip = prev_clip;
}

bool vg_render_slot_if_changed(const VgRenderSlot *slot,
                               VgRenderSlotState *state,
                               VgFrameBuffer *fb,
                               uint32_t snapshot_id) {
    if (!slot || !state || !fb) {
        return false;
    }
    VgClipRect slot_rect = clip_rect_expand(slot->clip_rect, slot->guard_px);
    bool props_changed = !state->initialized ||
                         state->last_visible != slot->visible ||
                         state->last_opaque != slot->opaque ||
                         state->last_clear_rgb565 != slot->clear_rgb565 ||
                         state->last_guard_px != slot->guard_px ||
                         !clip_rect_equal(state->last_clip_rect, slot->clip_rect);
    bool snapshot_changed = !state->initialized || state->snapshot_id != snapshot_id;
    if (!props_changed && !snapshot_changed) {
        return false;
    }

    VgClipRect dirty_rect = slot_rect;
    if (state->initialized) {
        VgClipRect prev_rect = clip_rect_expand(state->last_clip_rect, state->last_guard_px);
        dirty_rect = clip_rect_union(prev_rect, slot_rect);
    }
    framebuffer_clear_rect(fb, dirty_rect, slot->clear_rgb565);

    if (slot->visible && slot->root) {
        vg_render_scene_clipped(slot->root, fb, slot_rect);
    }

    state->initialized = true;
    state->snapshot_id = snapshot_id;
    state->last_clip_rect = slot->clip_rect;
    state->last_visible = slot->visible;
    state->last_opaque = slot->opaque;
    state->last_clear_rgb565 = slot->clear_rgb565;
    state->last_guard_px = slot->guard_px;
    return true;
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
