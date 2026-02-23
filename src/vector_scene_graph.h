#ifndef TINY_CLJ_VECTOR_SCENE_GRAPH_H
#define TINY_CLJ_VECTOR_SCENE_GRAPH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    float tx;
    float ty;
    float sx;
    float sy;
    float rot_deg;
} VgTransform;

typedef struct {
    int16_t x;
    int16_t y;
} VgPoint;

typedef struct {
    uint16_t stroke_rgb565;
    uint8_t stroke_width;
    bool visible;
    bool has_bg_rgb565;
    uint16_t bg_rgb565;
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
    float scale;
    float rot_deg;
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
VgTransform vg_transform_compose(VgTransform parent, VgTransform local);
void vg_transform_apply(VgTransform t, float x, float y, float *out_x, float *out_y);

bool vg_framebuffer_init(VgFrameBuffer *fb, int width, int height, uint16_t *pixels, size_t pixel_count);
void vg_framebuffer_clear(VgFrameBuffer *fb, uint16_t color);
uint32_t vg_framebuffer_checksum(const VgFrameBuffer *fb);
bool vg_framebuffer_dump_ppm(const VgFrameBuffer *fb, const char *path);

void vg_render_scene(const VgNode *root, VgFrameBuffer *fb);
bool vg_scene_apply_patch(VgNode *root, const VgPatch *patch);

#endif
