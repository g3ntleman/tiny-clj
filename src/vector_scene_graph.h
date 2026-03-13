#ifndef TINY_CLJ_VECTOR_SCENE_GRAPH_H
#define TINY_CLJ_VECTOR_SCENE_GRAPH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Fixed-point 1.0× for scale fields (Q19.13, matches CLJ_FIXED_SCALE). */
#define VG_SCALE_ONE (1 << 13)

typedef struct {
    int16_t tx;
    int16_t ty;
    int32_t sx;       /* Q19.13 fixed-point scale (VG_SCALE_ONE = 1.0×) */
    int32_t sy;       /* Q19.13 fixed-point scale (VG_SCALE_ONE = 1.0×) */
    int16_t rot_deg;
} VgTransform;

typedef struct {
    int32_t m00;
    int32_t m01;
    int32_t m02;
    int32_t m10;
    int32_t m11;
    int32_t m12;
} VgTransformFixed;

typedef enum {
    VG_ANIM_EASE_LINEAR = 0,
    VG_ANIM_EASE_IN_QUAD = 1,
    VG_ANIM_EASE_OUT_QUAD = 2,
    VG_ANIM_EASE_IN_OUT_QUAD = 3,
    VG_ANIM_EASE_OUT_CUBIC = 4
} VgAnimEase;

typedef struct {
    bool initialized;
    uint32_t response_ms;
    VgAnimEase ease;
    int32_t current_tx_q13;
    int32_t current_ty_q13;
    int32_t current_sx_q13;
    int32_t current_sy_q13;
    int32_t current_rot_q13;
    int32_t target_tx_q13;
    int32_t target_ty_q13;
    int32_t target_sx_q13;
    int32_t target_sy_q13;
    int32_t target_rot_q13;
} VgAnimTransformState;

typedef struct {
    int16_t x;
    int16_t y;
} VgPoint;

typedef struct {
    uint16_t stroke_color;
    uint8_t stroke_width;
    bool visible;
    bool has_fill;
    uint16_t fill_color;
    bool has_bg_color;
    uint16_t bg_color;
} VgStyle;

typedef enum {
    VG_NODE_GROUP = 1,
    VG_NODE_LINE = 2,
    VG_NODE_POLYLINE = 3,
    VG_NODE_RECT = 4,
    VG_NODE_TRI = 5,
    VG_NODE_VTEXT = 6
} VgNodeType;

struct VgNode;

typedef struct {
    struct VgNode **children;
    size_t child_count;
} VgGroupData;

typedef struct {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
} VgLineData;

typedef struct {
    const VgPoint *points;
    size_t point_count;
    bool closed;
} VgPolylineData;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} VgRectData;

typedef struct {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
    int16_t x3;
    int16_t y3;
} VgTriData;

typedef struct {
    int16_t x;
    int16_t y;
    int32_t scale;     /* Q19.13 fixed-point scale (VG_SCALE_ONE = 1.0×) */
    int16_t rot_deg;
    const char *text;
} VgTextData;

typedef struct VgNode {
    uint32_t id;
    VgNodeType type;
    bool has_transform;
    VgTransform transform;
    VgStyle style;
    union {
        VgGroupData group;
        VgLineData line;
        VgPolylineData polyline;
        VgRectData rect;
        VgTriData tri;
        VgTextData text;
    } data;
} VgNode;

typedef struct {
    int width;
    int height;
    uint16_t *pixels;
    size_t pixel_count;
} VgFrameBuffer;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} VgClipRect;

static inline bool vg_clip_rect_is_empty(VgClipRect r) {
    return r.w <= 0 || r.h <= 0;
}

static inline bool vg_clip_rect_equal(VgClipRect a, VgClipRect b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static inline VgClipRect vg_clip_rect_union(VgClipRect a, VgClipRect b) {
    if (vg_clip_rect_is_empty(a)) return b;
    if (vg_clip_rect_is_empty(b)) return a;
    int ax1 = (int)a.x + (int)a.w, ay1 = (int)a.y + (int)a.h;
    int bx1 = (int)b.x + (int)b.w, by1 = (int)b.y + (int)b.h;
    int x0 = ((int)a.x < (int)b.x) ? (int)a.x : (int)b.x;
    int y0 = ((int)a.y < (int)b.y) ? (int)a.y : (int)b.y;
    VgClipRect out = {(int16_t)x0, (int16_t)y0,
                      (int16_t)((ax1 > bx1 ? ax1 : bx1) - x0),
                      (int16_t)((ay1 > by1 ? ay1 : by1) - y0)};
    return out;
}

static inline VgClipRect vg_clip_rect_expand(VgClipRect r, uint8_t guard_px) {
    if (guard_px == 0 || vg_clip_rect_is_empty(r)) return r;
    int g = (int)guard_px;
    VgClipRect out = {(int16_t)((int)r.x - g), (int16_t)((int)r.y - g),
                      (int16_t)((int)r.w + 2 * g), (int16_t)((int)r.h + 2 * g)};
    return out;
}

static inline bool vg_clip_rect_intersect(VgClipRect a, VgClipRect b, VgClipRect *out) {
    int ax1 = (int)a.x + (int)a.w, ay1 = (int)a.y + (int)a.h;
    int bx1 = (int)b.x + (int)b.w, by1 = (int)b.y + (int)b.h;
    int x0 = ((int)a.x > (int)b.x) ? (int)a.x : (int)b.x;
    int y0 = ((int)a.y > (int)b.y) ? (int)a.y : (int)b.y;
    int x1 = (ax1 < bx1) ? ax1 : bx1;
    int y1 = (ay1 < by1) ? ay1 : by1;
    if (x1 <= x0 || y1 <= y0) return false;
    if (out) { out->x = (int16_t)x0; out->y = (int16_t)y0; out->w = (int16_t)(x1 - x0); out->h = (int16_t)(y1 - y0); }
    return true;
}

typedef struct {
    const VgNode *root;
    VgClipRect clip_rect;
    int16_t z;
    bool visible;
    bool opaque;
    uint16_t clear_color;
    uint8_t guard_px;
} VgRenderSlot;

typedef struct {
    bool initialized;
    bool has_animation;
    uint32_t snapshot_id;
    VgClipRect last_clip_rect;
    bool last_visible;
    bool last_opaque;
    uint16_t last_clear_color;
    uint8_t last_guard_px;
} VgRenderSlotState;

typedef enum {
    VG_PATCH_TRANSFORM = 1,
    VG_PATCH_TEXT = 2,
    VG_PATCH_VISIBILITY = 3,
    VG_PATCH_STYLE = 4
} VgPatchType;

typedef struct {
    uint32_t id;
    VgPatchType type;
    union {
        VgTransform transform;
        const char *text;
        bool visible;
        VgStyle style;
    } value;
} VgPatch;

VgTransform vg_transform_identity(void);
VgStyle vg_style_default(void);
VgTransformFixed vg_transform_fixed_identity(void);
VgTransformFixed vg_transform_fixed_from_transform(VgTransform t);
VgTransformFixed vg_transform_fixed_compose(VgTransformFixed parent, VgTransformFixed local);
void vg_transform_fixed_apply_px(VgTransformFixed t, int16_t x, int16_t y, int *out_x, int *out_y);

/* Fixed-point animation helpers for render-thread interpolation (Q19.13 / CLJ_FIXED_FRAC_BITS). */
int32_t vg_anim_progress_q13(uint32_t elapsed_ms, uint32_t duration_ms);
int32_t vg_anim_ease_q13(VgAnimEase ease, int32_t t_q13);
int32_t vg_anim_lerp_q13(int32_t from_q13, int32_t to_q13, int32_t t_q13);
void vg_anim_transform_state_reset(VgAnimTransformState *state,
                                   VgTransform initial,
                                   uint32_t response_ms,
                                   VgAnimEase ease);
void vg_anim_transform_state_set_target(VgAnimTransformState *state, VgTransform target);
VgTransform vg_anim_transform_state_step(VgAnimTransformState *state, uint32_t dt_ms);
VgTransform vg_anim_transform_state_current(const VgAnimTransformState *state);

bool vg_framebuffer_init(VgFrameBuffer *fb, int width, int height, uint16_t *pixels, size_t pixel_count);
void vg_framebuffer_clear(VgFrameBuffer *fb, uint16_t color);
void vg_framebuffer_clear_rect(VgFrameBuffer *fb, VgClipRect rect, uint16_t color);
uint32_t vg_framebuffer_checksum(const VgFrameBuffer *fb);

void vg_render_scene(const VgNode *root, VgFrameBuffer *fb);
void vg_render_scene_clipped(const VgNode *root, VgFrameBuffer *fb, VgClipRect clip_rect);
void vg_render_node_fixed(const VgNode *node, VgTransformFixed world_t, VgFrameBuffer *fb);
void vg_render_node_fixed_clipped(const VgNode *node, VgTransformFixed world_t, VgFrameBuffer *fb, VgClipRect clip_rect);
bool vg_render_slot_if_changed(const VgRenderSlot *slot,
                               VgRenderSlotState *state,
                               VgFrameBuffer *fb,
                               uint32_t snapshot_id);
bool vg_scene_apply_patch(VgNode *root, const VgPatch *patch);

#endif
