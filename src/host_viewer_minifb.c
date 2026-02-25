#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(TINYCLJ_WITH_MINIFB)
#include "vector_scene_graph.h"
#include "vector_scene_graph_records.h"
#include "value.h"
#include "runtime.h"
#include "record.h"
#include "builtins.h"
#include "event_loop.h"
#include "MiniFB.h"
#if defined(__APPLE__)
#include "host_viewer_macos_menu.h"
#endif

#define VIEW_W 320
#define VIEW_H 240
#define VIEW_DEFAULT_WINDOW_SCALE 2u
#define VIEWER_SLOT_COUNT 3
/* Toggle for easy A/B: 1=fixed simulation timestep, 0=legacy frame-coupled updates. */
#define HOST_VIEWER_FIXED_TIMESTEP_ENABLED 1
#define HOST_VIEWER_SIM_HZ 60.0
#define HOST_VIEWER_MAX_SIM_STEPS_PER_FRAME 4u

/** Letterbox viewport in window coordinates (avoids MiniFB's scale division which breaks on Retina). */
static void set_letterbox_viewport(struct mfb_window *window, unsigned win_w, unsigned win_h) {
    if (win_w == 0 || win_h == 0) return;
    float scale = (float)win_w / (float)VIEW_W;
    float scale_y = (float)win_h / (float)VIEW_H;
    if (scale_y < scale) scale = scale_y;
    unsigned draw_w = (unsigned)((float)VIEW_W * scale + 0.5f);
    unsigned draw_h = (unsigned)((float)VIEW_H * scale + 0.5f);
    unsigned offset_x = (win_w - draw_w) >> 1;
    unsigned offset_y = (win_h - draw_h) >> 1;
    (void)mfb_set_viewport(window, offset_x, offset_y, draw_w, draw_h);
}

static void on_window_resize(struct mfb_window *window, int width, int height) {
    set_letterbox_viewport(window, (unsigned)width, (unsigned)height);
}

static uint32_t rgb565_to_xrgb8888(uint16_t c) {
    uint32_t r = (uint32_t)((((c >> 11) & 0x1f) * 255) / 31);
    uint32_t g = (uint32_t)((((c >> 5) & 0x3f) * 255) / 63);
    uint32_t b = (uint32_t)(((c & 0x1f) * 255) / 31);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

typedef struct {
    int min_x;
    int min_y;
    int max_x;
    int max_y;
} VgIntAabb;

typedef struct {
    bool valid;
    VgTransform body_t;
    VgTransform nose_t;
    VgIntAabb world_aabb;
} ObstacleBBoxCache;

static void aabb_init(VgIntAabb *b, int x, int y) {
    if (!b) return;
    b->min_x = x;
    b->max_x = x;
    b->min_y = y;
    b->max_y = y;
}

static void aabb_expand(VgIntAabb *b, int x, int y) {
    if (!b) return;
    if (x < b->min_x) b->min_x = x;
    if (x > b->max_x) b->max_x = x;
    if (y < b->min_y) b->min_y = y;
    if (y > b->max_y) b->max_y = y;
}

static bool transforms_equal(VgTransform a, VgTransform b) {
    return a.tx == b.tx && a.ty == b.ty && a.sx == b.sx && a.sy == b.sy && a.rot_deg == b.rot_deg;
}

static bool compute_polyline_world_aabb_manual_transform(const VgNode *poly, VgIntAabb *out) {
    if (!poly || !out || poly->type != VG_NODE_POLYLINE || !poly->data.polyline.points || poly->data.polyline.point_count == 0) {
        return false;
    }
    VgTransformFixed t = vg_transform_fixed_from_transform(poly->transform);
    int wx = 0, wy = 0;
    vg_transform_fixed_apply_px(t, poly->data.polyline.points[0].x, poly->data.polyline.points[0].y, &wx, &wy);
    aabb_init(out, wx, wy);
    for (size_t i = 1; i < poly->data.polyline.point_count; i++) {
        vg_transform_fixed_apply_px(t, poly->data.polyline.points[i].x, poly->data.polyline.points[i].y, &wx, &wy);
        aabb_expand(out, wx, wy);
    }
    return true;
}

static bool compute_tri_world_aabb_manual_transform(const VgNode *tri, VgIntAabb *out) {
    if (!tri || !out || tri->type != VG_NODE_TRI) {
        return false;
    }
    VgTransformFixed t = vg_transform_fixed_from_transform(tri->transform);
    int wx = 0, wy = 0;
    vg_transform_fixed_apply_px(t, (int16_t)tri->data.tri.x1, (int16_t)tri->data.tri.y1, &wx, &wy);
    aabb_init(out, wx, wy);
    vg_transform_fixed_apply_px(t, (int16_t)tri->data.tri.x2, (int16_t)tri->data.tri.y2, &wx, &wy);
    aabb_expand(out, wx, wy);
    vg_transform_fixed_apply_px(t, (int16_t)tri->data.tri.x3, (int16_t)tri->data.tri.y3, &wx, &wy);
    aabb_expand(out, wx, wy);
    return true;
}

static bool compute_obstacle_world_aabb_cached_manual_transform(const VgNode *body,
                                                                const VgNode *nose,
                                                                ObstacleBBoxCache *cache,
                                                                VgIntAabb *out) {
    if (!body || !nose || !cache || !out) {
        return false;
    }
    if (cache->valid && transforms_equal(cache->body_t, body->transform) && transforms_equal(cache->nose_t, nose->transform)) {
        *out = cache->world_aabb;
        return true;
    }
    VgIntAabb body_box = {0};
    VgIntAabb nose_box = {0};
    if (!compute_polyline_world_aabb_manual_transform(body, &body_box) ||
        !compute_tri_world_aabb_manual_transform(nose, &nose_box)) {
        return false;
    }
    VgIntAabb merged = body_box;
    aabb_expand(&merged, nose_box.min_x, nose_box.min_y);
    aabb_expand(&merged, nose_box.max_x, nose_box.max_y);
    cache->valid = true;
    cache->body_t = body->transform;
    cache->nose_t = nose->transform;
    cache->world_aabb = merged;
    *out = merged;
    return true;
}

typedef struct {
    ID t_transform, t_style, t_group, t_line, t_polyline, t_rect, t_tri, t_vtext, t_frame_scene;
    unsigned int n_transform, n_style, n_group, n_line, n_polyline, n_rect, n_tri, n_vtext, n_frame_scene;
    int transform_tx, transform_ty, transform_sx, transform_sy, transform_rot;
    int style_stroke_rgb565, style_stroke_width, style_visible, style_has_fill, style_fill_rgb565, style_has_bg_rgb565, style_bg_rgb565;
    int group_id, group_t, group_style, group_visible, group_children;
    int line_id, line_t, line_style, line_visible, line_x1, line_y1, line_x2, line_y2;
    int poly_id, poly_t, poly_style, poly_visible, poly_pts, poly_closed;
    int rect_id, rect_t, rect_style, rect_visible, rect_x, rect_y, rect_w, rect_h;
    int tri_id, tri_t, tri_style, tri_visible, tri_x1, tri_y1, tri_x2, tri_y2, tri_x3, tri_y3;
    int text_id, text_t, text_style, text_visible, text_x, text_y, text_scale, text_rot, text_text;
    int frame_root, frame_clip_rect, frame_z, frame_visible, frame_opaque, frame_erase_rgb565, frame_guard_px;
} VgRecordSchema;

static VgRecordSchema g_record_schema = {0};

static int descriptor_index_of(CljRecordDescriptor *desc, ID key) {
    if (!desc || !desc->field_keys || !key) {
        return -1;
    }
    unsigned int n = vector_count(desc->field_keys);
    for (unsigned int i = 0; i < n; i++) {
        ID candidate = vector_nth(desc->field_keys, i);
        if (candidate == key) {
            return (int)i;
        }
    }
    return -1;
}

static bool ensure_vector_scene_record_schema(EvalState *st) {
    static bool initialized = false;
    if (initialized) {
        return true;
    }
    if (!st) {
        return false;
    }
    if (!require_namespace_by_name(st, "tiny-gfx.scene")) {
        return false;
    }

    ID k_tx = intern_symbol_global(":tx");
    ID k_ty = intern_symbol_global(":ty");
    ID k_sx = intern_symbol_global(":sx");
    ID k_sy = intern_symbol_global(":sy");
    ID k_rot = intern_symbol_global(":rot");
    ID k_stroke_rgb565 = intern_symbol_global(":stroke_rgb565");
    ID k_stroke_width = intern_symbol_global(":stroke_width");
    ID k_has_fill = intern_symbol_global(":has_fill");
    ID k_fill_rgb565 = intern_symbol_global(":fill_rgb565");
    ID k_visible = intern_symbol_global(":visible");
    ID k_has_bg_rgb565 = intern_symbol_global(":has_bg_rgb565");
    ID k_bg_rgb565 = intern_symbol_global(":bg_rgb565");
    ID k_id = intern_symbol_global(":id");
    ID k_t = intern_symbol_global(":t");
    ID k_style = intern_symbol_global(":style");
    ID k_children = intern_symbol_global(":children");
    ID k_x1 = intern_symbol_global(":x1");
    ID k_y1 = intern_symbol_global(":y1");
    ID k_x2 = intern_symbol_global(":x2");
    ID k_y2 = intern_symbol_global(":y2");
    ID k_x3 = intern_symbol_global(":x3");
    ID k_y3 = intern_symbol_global(":y3");
    ID k_pts = intern_symbol_global(":pts");
    ID k_closed = intern_symbol_global(":closed");
    ID k_x = intern_symbol_global(":x");
    ID k_y = intern_symbol_global(":y");
    ID k_w = intern_symbol_global(":w");
    ID k_h = intern_symbol_global(":h");
    ID k_scale = intern_symbol_global(":scale");
    ID k_text = intern_symbol_global(":text");
    ID k_root = intern_symbol_global(":root");
    ID k_clip_rect = intern_symbol_global(":clip-rect");
    ID k_z = intern_symbol_global(":z");
    ID k_opaque = intern_symbol_global(":opaque");
    ID k_erase_rgb565 = intern_symbol_global(":erase-rgb565");
    ID k_guard_px = intern_symbol_global(":guard-px");

    g_record_schema.t_transform = intern_symbol_global("Transform");
    g_record_schema.t_style = intern_symbol_global("Style");
    g_record_schema.t_group = intern_symbol_global("Group");
    g_record_schema.t_line = intern_symbol_global("Line");
    g_record_schema.t_polyline = intern_symbol_global("Polyline");
    g_record_schema.t_rect = intern_symbol_global("Rect");
    g_record_schema.t_tri = intern_symbol_global("Tri");
    g_record_schema.t_vtext = intern_symbol_global("VText");
    g_record_schema.t_frame_scene = intern_symbol_global("FrameScene");

    CljRecordDescriptor *d_transform = record_descriptor_lookup(g_record_schema.t_transform);
    CljRecordDescriptor *d_style = record_descriptor_lookup(g_record_schema.t_style);
    CljRecordDescriptor *d_group = record_descriptor_lookup(g_record_schema.t_group);
    CljRecordDescriptor *d_line = record_descriptor_lookup(g_record_schema.t_line);
    CljRecordDescriptor *d_poly = record_descriptor_lookup(g_record_schema.t_polyline);
    CljRecordDescriptor *d_rect = record_descriptor_lookup(g_record_schema.t_rect);
    CljRecordDescriptor *d_tri = record_descriptor_lookup(g_record_schema.t_tri);
    CljRecordDescriptor *d_text = record_descriptor_lookup(g_record_schema.t_vtext);
    CljRecordDescriptor *d_frame = record_descriptor_lookup(g_record_schema.t_frame_scene);
    if (!d_transform || !d_style || !d_group || !d_line || !d_poly || !d_rect || !d_tri || !d_text || !d_frame) {
        return false;
    }

    g_record_schema.n_transform = vector_count(d_transform->field_keys);
    g_record_schema.n_style = vector_count(d_style->field_keys);
    g_record_schema.n_group = vector_count(d_group->field_keys);
    g_record_schema.n_line = vector_count(d_line->field_keys);
    g_record_schema.n_polyline = vector_count(d_poly->field_keys);
    g_record_schema.n_rect = vector_count(d_rect->field_keys);
    g_record_schema.n_tri = vector_count(d_tri->field_keys);
    g_record_schema.n_vtext = vector_count(d_text->field_keys);
    g_record_schema.n_frame_scene = vector_count(d_frame->field_keys);

    g_record_schema.transform_tx = descriptor_index_of(d_transform, k_tx);
    g_record_schema.transform_ty = descriptor_index_of(d_transform, k_ty);
    g_record_schema.transform_sx = descriptor_index_of(d_transform, k_sx);
    g_record_schema.transform_sy = descriptor_index_of(d_transform, k_sy);
    g_record_schema.transform_rot = descriptor_index_of(d_transform, k_rot);
    g_record_schema.style_stroke_rgb565 = descriptor_index_of(d_style, k_stroke_rgb565);
    g_record_schema.style_stroke_width = descriptor_index_of(d_style, k_stroke_width);
    g_record_schema.style_visible = descriptor_index_of(d_style, k_visible);
    g_record_schema.style_has_fill = descriptor_index_of(d_style, k_has_fill);
    g_record_schema.style_fill_rgb565 = descriptor_index_of(d_style, k_fill_rgb565);
    g_record_schema.style_has_bg_rgb565 = descriptor_index_of(d_style, k_has_bg_rgb565);
    g_record_schema.style_bg_rgb565 = descriptor_index_of(d_style, k_bg_rgb565);
    g_record_schema.group_id = descriptor_index_of(d_group, k_id);
    g_record_schema.group_t = descriptor_index_of(d_group, k_t);
    g_record_schema.group_style = descriptor_index_of(d_group, k_style);
    g_record_schema.group_visible = descriptor_index_of(d_group, k_visible);
    g_record_schema.group_children = descriptor_index_of(d_group, k_children);
    g_record_schema.line_id = descriptor_index_of(d_line, k_id);
    g_record_schema.line_t = descriptor_index_of(d_line, k_t);
    g_record_schema.line_style = descriptor_index_of(d_line, k_style);
    g_record_schema.line_visible = descriptor_index_of(d_line, k_visible);
    g_record_schema.line_x1 = descriptor_index_of(d_line, k_x1);
    g_record_schema.line_y1 = descriptor_index_of(d_line, k_y1);
    g_record_schema.line_x2 = descriptor_index_of(d_line, k_x2);
    g_record_schema.line_y2 = descriptor_index_of(d_line, k_y2);
    g_record_schema.poly_id = descriptor_index_of(d_poly, k_id);
    g_record_schema.poly_t = descriptor_index_of(d_poly, k_t);
    g_record_schema.poly_style = descriptor_index_of(d_poly, k_style);
    g_record_schema.poly_visible = descriptor_index_of(d_poly, k_visible);
    g_record_schema.poly_pts = descriptor_index_of(d_poly, k_pts);
    g_record_schema.poly_closed = descriptor_index_of(d_poly, k_closed);
    g_record_schema.rect_id = descriptor_index_of(d_rect, k_id);
    g_record_schema.rect_t = descriptor_index_of(d_rect, k_t);
    g_record_schema.rect_style = descriptor_index_of(d_rect, k_style);
    g_record_schema.rect_visible = descriptor_index_of(d_rect, k_visible);
    g_record_schema.rect_x = descriptor_index_of(d_rect, k_x);
    g_record_schema.rect_y = descriptor_index_of(d_rect, k_y);
    g_record_schema.rect_w = descriptor_index_of(d_rect, k_w);
    g_record_schema.rect_h = descriptor_index_of(d_rect, k_h);
    g_record_schema.tri_id = descriptor_index_of(d_tri, k_id);
    g_record_schema.tri_t = descriptor_index_of(d_tri, k_t);
    g_record_schema.tri_style = descriptor_index_of(d_tri, k_style);
    g_record_schema.tri_visible = descriptor_index_of(d_tri, k_visible);
    g_record_schema.tri_x1 = descriptor_index_of(d_tri, k_x1);
    g_record_schema.tri_y1 = descriptor_index_of(d_tri, k_y1);
    g_record_schema.tri_x2 = descriptor_index_of(d_tri, k_x2);
    g_record_schema.tri_y2 = descriptor_index_of(d_tri, k_y2);
    g_record_schema.tri_x3 = descriptor_index_of(d_tri, k_x3);
    g_record_schema.tri_y3 = descriptor_index_of(d_tri, k_y3);
    g_record_schema.text_id = descriptor_index_of(d_text, k_id);
    g_record_schema.text_t = descriptor_index_of(d_text, k_t);
    g_record_schema.text_style = descriptor_index_of(d_text, k_style);
    g_record_schema.text_visible = descriptor_index_of(d_text, k_visible);
    g_record_schema.text_x = descriptor_index_of(d_text, k_x);
    g_record_schema.text_y = descriptor_index_of(d_text, k_y);
    g_record_schema.text_scale = descriptor_index_of(d_text, k_scale);
    g_record_schema.text_rot = descriptor_index_of(d_text, k_rot);
    g_record_schema.text_text = descriptor_index_of(d_text, k_text);
    g_record_schema.frame_root = descriptor_index_of(d_frame, k_root);
    g_record_schema.frame_clip_rect = descriptor_index_of(d_frame, k_clip_rect);
    g_record_schema.frame_z = descriptor_index_of(d_frame, k_z);
    g_record_schema.frame_visible = descriptor_index_of(d_frame, k_visible);
    g_record_schema.frame_opaque = descriptor_index_of(d_frame, k_opaque);
    g_record_schema.frame_erase_rgb565 = descriptor_index_of(d_frame, k_erase_rgb565);
    g_record_schema.frame_guard_px = descriptor_index_of(d_frame, k_guard_px);

    if (g_record_schema.transform_tx < 0 || g_record_schema.transform_ty < 0 || g_record_schema.transform_sx < 0 ||
        g_record_schema.transform_sy < 0 || g_record_schema.transform_rot < 0 || g_record_schema.style_stroke_rgb565 < 0 ||
        g_record_schema.style_stroke_width < 0 || g_record_schema.style_visible < 0 || g_record_schema.style_has_fill < 0 ||
        g_record_schema.style_fill_rgb565 < 0 || g_record_schema.style_has_bg_rgb565 < 0 ||
        g_record_schema.style_bg_rgb565 < 0 || g_record_schema.group_id < 0 || g_record_schema.group_t < 0 ||
        g_record_schema.group_style < 0 || g_record_schema.group_visible < 0 || g_record_schema.group_children < 0 ||
        g_record_schema.line_id < 0 || g_record_schema.line_t < 0 || g_record_schema.line_style < 0 ||
        g_record_schema.line_visible < 0 || g_record_schema.line_x1 < 0 || g_record_schema.line_y1 < 0 ||
        g_record_schema.line_x2 < 0 || g_record_schema.line_y2 < 0 || g_record_schema.poly_id < 0 ||
        g_record_schema.poly_t < 0 || g_record_schema.poly_style < 0 || g_record_schema.poly_visible < 0 ||
        g_record_schema.poly_pts < 0 || g_record_schema.poly_closed < 0 || g_record_schema.rect_id < 0 ||
        g_record_schema.rect_t < 0 || g_record_schema.rect_style < 0 || g_record_schema.rect_visible < 0 ||
        g_record_schema.rect_x < 0 || g_record_schema.rect_y < 0 || g_record_schema.rect_w < 0 ||
        g_record_schema.rect_h < 0 || g_record_schema.tri_id < 0 || g_record_schema.tri_t < 0 ||
        g_record_schema.tri_style < 0 || g_record_schema.tri_visible < 0 || g_record_schema.tri_x1 < 0 ||
        g_record_schema.tri_y1 < 0 || g_record_schema.tri_x2 < 0 || g_record_schema.tri_y2 < 0 ||
        g_record_schema.tri_x3 < 0 || g_record_schema.tri_y3 < 0 || g_record_schema.text_id < 0 ||
        g_record_schema.text_t < 0 || g_record_schema.text_style < 0 || g_record_schema.text_visible < 0 ||
        g_record_schema.text_x < 0 || g_record_schema.text_y < 0 || g_record_schema.text_scale < 0 ||
        g_record_schema.text_rot < 0 || g_record_schema.text_text < 0 || g_record_schema.frame_root < 0 ||
        g_record_schema.frame_clip_rect < 0 || g_record_schema.frame_z < 0 || g_record_schema.frame_visible < 0 ||
        g_record_schema.frame_opaque < 0 || g_record_schema.frame_erase_rgb565 < 0 || g_record_schema.frame_guard_px < 0) {
        return false;
    }

    initialized = true;
    return true;
}

static ID create_record_from_slots(ID type_symbol, unsigned int field_count, ID *slots) {
    CljPersistentVector *v = make_vector(field_count, STRONG);
    if (!v) {
        return NULL;
    }
    for (unsigned int i = 0; i < field_count; i++) {
        vector_conj_inplace(&v, slots[i]);
    }
    ID rec = AUTORELEASE(record_create(type_symbol, v));
    RELEASE(v);
    return rec;
}

static ID make_transform_record(const VgNode *node) {
    if (!node || !node->has_transform) {
        return NULL;
    }
    ID *slots = STACK_ALLOC(ID, g_record_schema.n_transform);
    for (unsigned int i = 0; i < g_record_schema.n_transform; i++) slots[i] = NULL;
    slots[g_record_schema.transform_tx] = fixed(node->transform.tx);
    slots[g_record_schema.transform_ty] = fixed(node->transform.ty);
    slots[g_record_schema.transform_sx] = fixed(node->transform.sx);
    slots[g_record_schema.transform_sy] = fixed(node->transform.sy);
    slots[g_record_schema.transform_rot] = fixed(node->transform.rot_deg);
    return create_record_from_slots(g_record_schema.t_transform, g_record_schema.n_transform, slots);
}

static ID make_style_record(VgStyle style) {
    ID *slots = STACK_ALLOC(ID, g_record_schema.n_style);
    for (unsigned int i = 0; i < g_record_schema.n_style; i++) slots[i] = NULL;
    slots[g_record_schema.style_stroke_rgb565] = fixnum((int)style.stroke_rgb565);
    slots[g_record_schema.style_stroke_width] = fixnum((int)style.stroke_width);
    slots[g_record_schema.style_visible] = style.visible ? clj_true : clj_false;
    slots[g_record_schema.style_has_fill] = style.has_fill ? clj_true : clj_false;
    slots[g_record_schema.style_fill_rgb565] = fixnum((int)style.fill_rgb565);
    slots[g_record_schema.style_has_bg_rgb565] = style.has_bg_rgb565 ? clj_true : clj_false;
    slots[g_record_schema.style_bg_rgb565] = fixnum((int)style.bg_rgb565);
    return create_record_from_slots(g_record_schema.t_style, g_record_schema.n_style, slots);
}

static ID make_node_record(const VgNode *node);

static ID make_group_children_vector(const VgNode *node) {
    CljPersistentVector *children_vec = make_vector((unsigned int)node->data.group.child_count, STRONG);
    if (!children_vec) return NULL;
    for (size_t i = 0; i < node->data.group.child_count; i++) {
        ID child = make_node_record(node->data.group.children[i]);
        if (!child) {
            RELEASE(children_vec);
            return NULL;
        }
        vector_conj_inplace(&children_vec, child);
    }
    return AUTORELEASE(children_vec);
}

static ID make_polyline_points_vector(const VgNode *node) {
    CljPersistentVector *pts = make_vector((unsigned int)node->data.polyline.point_count, STRONG);
    if (!pts) return NULL;
    for (size_t i = 0; i < node->data.polyline.point_count; i++) {
        CljPersistentVector *xy = make_vector(2, STRONG);
        if (!xy) {
            RELEASE(pts);
            return NULL;
        }
        vector_conj_inplace(&xy, fixnum((int)node->data.polyline.points[i].x));
        vector_conj_inplace(&xy, fixnum((int)node->data.polyline.points[i].y));
        vector_conj_inplace(&pts, xy);
        RELEASE(xy);
    }
    return AUTORELEASE(pts);
}

static ID make_node_record(const VgNode *node) {
    if (!node) return NULL;
    ID t_rec = make_transform_record(node);
    ID s_rec = make_style_record(node->style);
    ID visible = node->style.visible ? clj_true : clj_false;

    switch (node->type) {
        case VG_NODE_GROUP: {
            ID children = make_group_children_vector(node);
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_group);
            for (unsigned int i = 0; i < g_record_schema.n_group; i++) slots[i] = NULL;
            slots[g_record_schema.group_id] = fixnum((int)node->id);
            slots[g_record_schema.group_t] = t_rec;
            slots[g_record_schema.group_style] = s_rec;
            slots[g_record_schema.group_visible] = visible;
            slots[g_record_schema.group_children] = children;
            return create_record_from_slots(g_record_schema.t_group, g_record_schema.n_group, slots);
        }
        case VG_NODE_LINE: {
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_line);
            for (unsigned int i = 0; i < g_record_schema.n_line; i++) slots[i] = NULL;
            slots[g_record_schema.line_id] = fixnum((int)node->id);
            slots[g_record_schema.line_t] = t_rec;
            slots[g_record_schema.line_style] = s_rec;
            slots[g_record_schema.line_visible] = visible;
            slots[g_record_schema.line_x1] = fixnum((int)node->data.line.x1);
            slots[g_record_schema.line_y1] = fixnum((int)node->data.line.y1);
            slots[g_record_schema.line_x2] = fixnum((int)node->data.line.x2);
            slots[g_record_schema.line_y2] = fixnum((int)node->data.line.y2);
            return create_record_from_slots(g_record_schema.t_line, g_record_schema.n_line, slots);
        }
        case VG_NODE_POLYLINE: {
            ID pts = make_polyline_points_vector(node);
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_polyline);
            for (unsigned int i = 0; i < g_record_schema.n_polyline; i++) slots[i] = NULL;
            slots[g_record_schema.poly_id] = fixnum((int)node->id);
            slots[g_record_schema.poly_t] = t_rec;
            slots[g_record_schema.poly_style] = s_rec;
            slots[g_record_schema.poly_visible] = visible;
            slots[g_record_schema.poly_pts] = pts;
            slots[g_record_schema.poly_closed] = node->data.polyline.closed ? clj_true : clj_false;
            return create_record_from_slots(g_record_schema.t_polyline, g_record_schema.n_polyline, slots);
        }
        case VG_NODE_RECT: {
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_rect);
            for (unsigned int i = 0; i < g_record_schema.n_rect; i++) slots[i] = NULL;
            slots[g_record_schema.rect_id] = fixnum((int)node->id);
            slots[g_record_schema.rect_t] = t_rec;
            slots[g_record_schema.rect_style] = s_rec;
            slots[g_record_schema.rect_visible] = visible;
            slots[g_record_schema.rect_x] = fixnum((int)node->data.rect.x);
            slots[g_record_schema.rect_y] = fixnum((int)node->data.rect.y);
            slots[g_record_schema.rect_w] = fixnum((int)node->data.rect.w);
            slots[g_record_schema.rect_h] = fixnum((int)node->data.rect.h);
            return create_record_from_slots(g_record_schema.t_rect, g_record_schema.n_rect, slots);
        }
        case VG_NODE_TRI: {
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_tri);
            for (unsigned int i = 0; i < g_record_schema.n_tri; i++) slots[i] = NULL;
            slots[g_record_schema.tri_id] = fixnum((int)node->id);
            slots[g_record_schema.tri_t] = t_rec;
            slots[g_record_schema.tri_style] = s_rec;
            slots[g_record_schema.tri_visible] = visible;
            slots[g_record_schema.tri_x1] = fixnum((int)node->data.tri.x1);
            slots[g_record_schema.tri_y1] = fixnum((int)node->data.tri.y1);
            slots[g_record_schema.tri_x2] = fixnum((int)node->data.tri.x2);
            slots[g_record_schema.tri_y2] = fixnum((int)node->data.tri.y2);
            slots[g_record_schema.tri_x3] = fixnum((int)node->data.tri.x3);
            slots[g_record_schema.tri_y3] = fixnum((int)node->data.tri.y3);
            return create_record_from_slots(g_record_schema.t_tri, g_record_schema.n_tri, slots);
        }
        case VG_NODE_VTEXT: {
            ID text = AUTORELEASE(make_string(node->data.text.text ? node->data.text.text : ""));
            ID *slots = STACK_ALLOC(ID, g_record_schema.n_vtext);
            for (unsigned int i = 0; i < g_record_schema.n_vtext; i++) slots[i] = NULL;
            slots[g_record_schema.text_id] = fixnum((int)node->id);
            slots[g_record_schema.text_t] = t_rec;
            slots[g_record_schema.text_style] = s_rec;
            slots[g_record_schema.text_visible] = visible;
            slots[g_record_schema.text_x] = fixnum((int)node->data.text.x);
            slots[g_record_schema.text_y] = fixnum((int)node->data.text.y);
            slots[g_record_schema.text_scale] = fixed(node->data.text.scale);
            slots[g_record_schema.text_rot] = fixed(node->data.text.rot_deg);
            slots[g_record_schema.text_text] = text;
            return create_record_from_slots(g_record_schema.t_vtext, g_record_schema.n_vtext, slots);
        }
        default:
            return NULL;
    }
}

static ID make_frame_scene_record(const VgNode *root,
                                   VgClipRect clip_rect,
                                   int z,
                                   bool visible,
                                   bool opaque,
                                   uint16_t erase_rgb565,
                                   uint8_t guard_px) {
    ID root_rec = make_node_record(root);
    if (!root_rec) return NULL;

    CljPersistentVector *clip_vec = make_vector(4, STRONG);
    if (!clip_vec) return NULL;
    vector_conj_inplace(&clip_vec, fixnum(clip_rect.x));
    vector_conj_inplace(&clip_vec, fixnum(clip_rect.y));
    vector_conj_inplace(&clip_vec, fixnum(clip_rect.w));
    vector_conj_inplace(&clip_vec, fixnum(clip_rect.h));

    ID *slots = STACK_ALLOC(ID, g_record_schema.n_frame_scene);
    for (unsigned int i = 0; i < g_record_schema.n_frame_scene; i++) slots[i] = NULL;
    slots[g_record_schema.frame_root] = root_rec;
    slots[g_record_schema.frame_clip_rect] = clip_vec;
    slots[g_record_schema.frame_z] = fixnum(z);
    slots[g_record_schema.frame_visible] = visible ? clj_true : clj_false;
    slots[g_record_schema.frame_opaque] = opaque ? clj_true : clj_false;
    slots[g_record_schema.frame_erase_rgb565] = fixnum((int)erase_rgb565);
    slots[g_record_schema.frame_guard_px] = fixnum((int)guard_px);
    ID scene = create_record_from_slots(g_record_schema.t_frame_scene, g_record_schema.n_frame_scene, slots);
    RELEASE(clip_vec);
    return scene;
}

static ID g_published_slots[VIEWER_SLOT_COUNT] = {0};
static uint32_t g_published_slot_generation[VIEWER_SLOT_COUNT] = {0};

static void publish_frame_scene_slot(size_t slot_index,
                                     const VgNode *root,
                                     VgClipRect clip_rect,
                                     int z,
                                     bool visible,
                                     bool opaque,
                                     uint16_t erase_rgb565,
                                     uint8_t guard_px) {
    if (slot_index >= VIEWER_SLOT_COUNT) {
        return;
    }
    WITH_AUTORELEASE_POOL({
        ID scene = make_frame_scene_record(root, clip_rect, z, visible, opaque, erase_rgb565, guard_px);
        if (scene) {
            RETAIN(scene);
            ASSIGN(g_published_slots[slot_index], scene);
            g_published_slot_generation[slot_index]++;
            RELEASE(scene);
        }
    });
}
#endif

int main(void) {
#if !defined(TINYCLJ_WITH_MINIFB)
    fprintf(stderr, "MiniFB support is disabled for this build.\n");
    return 1;
#else
    uint16_t fb_pixels[VIEW_W * VIEW_H];
    uint32_t window_pixels[VIEW_W * VIEW_H];

    VgFrameBuffer fb;
    if (!vg_framebuffer_init(&fb, VIEW_W, VIEW_H, fb_pixels, VIEW_W * VIEW_H)) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    runtime_init(&g_runtime);
    event_loop_init();
    EvalState *viewer_eval_state = evalstate_new(true);
    if (!viewer_eval_state) {
        fprintf(stderr, "Failed to initialize eval state\n");
        return 1;
    }
    evalstate_set_ns(viewer_eval_state, "user");
    if (!ensure_vector_scene_record_schema(viewer_eval_state)) {
        fprintf(stderr, "Failed to initialize vector scene record schema via tiny-gfx.scene\n");
        return 1;
    }

#if defined(__APPLE__)
    macos_viewer_install_menu();
#endif
    const unsigned default_win_w = VIEW_W * VIEW_DEFAULT_WINDOW_SCALE;
    const unsigned default_win_h = VIEW_H * VIEW_DEFAULT_WINDOW_SCALE;
    struct mfb_window *window = mfb_open_ex("tiny-clj vector host viewer (MiniFB)", default_win_w, default_win_h, WF_RESIZABLE);
    if (!window) {
        fprintf(stderr, "Failed to open MiniFB window\n");
        return 1;
    }
#if defined(__APPLE__)
    macos_viewer_register_window_callbacks();
    macos_viewer_restore_window_position();
#endif
    mfb_show_cursor(window, true);
    mfb_set_resize_callback(window, on_window_resize);
    set_letterbox_viewport(window, default_win_w, default_win_h);
    struct mfb_timer *timer = mfb_timer_create();
    if (!timer) {
        fprintf(stderr, "Failed to create MiniFB timer\n");
        mfb_close(window);
        return 1;
    }
    double fps_window_start_s = mfb_timer_now(timer);
    double sim_prev_time_s = fps_window_start_s;
    double sim_accumulator_s = 0.0;
    unsigned fps_frame_count = 0;
    char fps_label[32];
    (void)snprintf(fps_label, sizeof(fps_label), "FPS: --.-");

    VgStyle deco_style = vg_style_default();
    deco_style.stroke_rgb565 = 0x07ffu;
    deco_style.stroke_width = 2;
    deco_style.has_bg_rgb565 = false;

    VgStyle score_style = vg_style_default();
    score_style.stroke_rgb565 = 0xffffu;
    score_style.stroke_width = 1;
    score_style.has_bg_rgb565 = false;

    VgStyle game_line_style = vg_style_default();
    game_line_style.stroke_rgb565 = 0x07e0u;
    game_line_style.stroke_width = 2;
    game_line_style.has_bg_rgb565 = false;

    VgStyle game_player_style = vg_style_default();
    game_player_style.stroke_rgb565 = 0xf81fu;
    game_player_style.stroke_width = 3;
    game_player_style.has_bg_rgb565 = false;

    VgStyle game_obstacle_style = vg_style_default();
    game_obstacle_style.stroke_rgb565 = 0xffe0u;
    game_obstacle_style.stroke_width = 2;
    game_obstacle_style.has_fill = true;
    game_obstacle_style.fill_rgb565 = 0xffe0u;
    game_obstacle_style.has_bg_rgb565 = false;
    VgStyle game_obstacle_nose_style = vg_style_default();
    game_obstacle_nose_style.stroke_rgb565 = 0xf800u;
    game_obstacle_nose_style.stroke_width = 2;
    game_obstacle_nose_style.has_fill = true;
    game_obstacle_nose_style.fill_rgb565 = 0xf800u;
    game_obstacle_nose_style.has_bg_rgb565 = false;

    VgClipRect score_clip = {.x = 0, .y = 0, .w = VIEW_W, .h = 32};
    VgClipRect game_clip = {.x = 0, .y = 40, .w = VIEW_W, .h = 136};
    VgClipRect deco_clip = {.x = 0, .y = 184, .w = VIEW_W, .h = 56};

    VgPoint mountain_pts[] = {
        {0, 228}, {26, 206}, {58, 226}, {92, 196}, {126, 225},
        {162, 202}, {198, 230}, {238, 192}, {276, 226}, {319, 204}
    };
    VgNode mountains = {
        .id = 1001,
        .type = VG_NODE_POLYLINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = deco_style,
        .data.polyline = {.points = mountain_pts, .point_count = sizeof(mountain_pts) / sizeof(mountain_pts[0]), .closed = false}
    };
    VgNode deco_horizon = {
        .id = 1002,
        .type = VG_NODE_LINE,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = deco_style,
        .data.line = {.x1 = 0, .y1 = 236, .x2 = 319, .y2 = 236}
    };
    VgNode *deco_children[] = {&mountains, &deco_horizon};
    VgNode deco_root = {
        .id = 1000,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = deco_children, .child_count = sizeof(deco_children) / sizeof(deco_children[0])}
    };

    char score_line[64];
    (void)snprintf(score_line, sizeof(score_line), "SCORE 0000    LIFES 3");
    VgNode score_text = {
        .id = 2001,
        .type = VG_NODE_VTEXT,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = score_style,
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = score_line}
    };
    score_text.transform.tx = 6.0f;
    score_text.transform.ty = 10.0f;
    VgNode *score_children[] = {&score_text};
    VgNode score_root = {
        .id = 2000,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = score_children, .child_count = 1}
    };

    VgPoint terrain_pts[] = {
        {0, 156}, {60, 156}, {100, 144}, {150, 156}, {220, 156}, {260, 146}, {319, 156},
        {380, 156}, {420, 146}, {480, 156}, {560, 156}, {620, 144}
    };
    VgNode game_terrain = {
        .id = 3001,
        .type = VG_NODE_POLYLINE,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = game_line_style,
        .data.polyline = {.points = terrain_pts, .point_count = sizeof(terrain_pts) / sizeof(terrain_pts[0]), .closed = false}
    };
    VgNode game_player = {
        .id = 3002,
        .type = VG_NODE_TRI,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = game_player_style,
        .data.tri = {.x1 = 56, .y1 = 146, .x2 = 72, .y2 = 118, .x3 = 88, .y3 = 146}
    };
    /* Missile silhouette: original right-facing geometry, oriented upward via transform rotation. */
    VgPoint missile_body_pts[] = {
        {-12, 3}, {8, 3}, {8, -3}, {-12, -3},
        {-16, -7}, {-20, -7}, {-20, -2}, {-13, -2},
        {-13, -1}, {-20, -1}, {-20, 1}, {-13, 1},
        {-13, 2}, {-20, 2}, {-20, 7}, {-16, 7}
    };
    VgNode game_obstacle_body = {
        .id = 3003,
        .type = VG_NODE_POLYLINE,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = game_obstacle_style,
        .data.polyline = {.points = missile_body_pts, .point_count = sizeof(missile_body_pts) / sizeof(missile_body_pts[0]), .closed = true}
    };
    VgNode game_obstacle_nose = {
        .id = 3005,
        .type = VG_NODE_TRI,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = game_obstacle_nose_style,
        .data.tri = {.x1 = 20, .y1 = 0, .x2 = 10, .y2 = -3, .x3 = 10, .y3 = 3}
    };
    VgNode game_caption = {
        .id = 3004,
        .type = VG_NODE_VTEXT,
        .has_transform = true,
        .transform = vg_transform_identity(),
        .style = score_style,
        .data.text = {.x = 0, .y = 0, .scale = 1.0f, .rot_deg = 0.0f, .text = "GAME SCENE"}
    };
    game_caption.transform.tx = 96.0f;
    game_caption.transform.ty = 52.0f;
    VgNode *game_children[] = {&game_terrain, &game_player, &game_obstacle_body, &game_obstacle_nose, &game_caption};
    VgNode game_root = {
        .id = 3000,
        .type = VG_NODE_GROUP,
        .has_transform = false,
        .transform = vg_transform_identity(),
        .style = vg_style_default(),
        .data.group = {.children = game_children, .child_count = sizeof(game_children) / sizeof(game_children[0])}
    };

    VgRenderSlotState slot_states[VIEWER_SLOT_COUNT] = {0};
    bool player_small = false;
    bool collision_latched = false;
    int collision_cooldown_frames = 0;
    ObstacleBBoxCache obstacle_bbox_cache = {0};
    uint32_t frame_tick = 0;
    static const int player_bob_lut[] = {0, -1, -3, -5, -4, -2, 0, 1, 0, -1, -2, -1};
    vg_framebuffer_clear(&fb, 0x0000u);
    publish_frame_scene_slot(0, &deco_root, deco_clip, 0, true, true, 0x0000u, 1);
    publish_frame_scene_slot(1, &score_root, score_clip, 1, true, true, 0x0000u, 1);
    publish_frame_scene_slot(2, &game_root, game_clip, 2, true, true, 0x0000u, 1);

    while (true) {
        float time_s = (float)mfb_timer_now(timer);
        fps_frame_count++;
        double fps_elapsed_s = (double)time_s - fps_window_start_s;
        bool score_changed = false;
#if defined(__APPLE__)
        if (fps_elapsed_s >= 1.0) {
            double fps = (double)fps_frame_count / fps_elapsed_s;
            int score = (int)(time_s * 120.0f);
            (void)snprintf(fps_label, sizeof(fps_label), "FPS: %.1f", fps);
            (void)snprintf(score_line, sizeof(score_line), "SCORE %04d    LIFES 3", score % 10000);
            char title[96];
            (void)snprintf(title, sizeof(title), "tiny-clj vector host viewer (MiniFB) - %.1f FPS", fps);
            macos_viewer_set_window_title(title);
            fps_window_start_s = (double)time_s;
            fps_frame_count = 0;
            score_changed = true;
        }
#endif
        if (!mfb_wait_sync(window)) {
            break;
        }
        const uint8_t *keys = mfb_get_key_buffer(window);
        if (keys) {
            bool esc = keys[KB_KEY_ESCAPE] != 0;
            bool cmd_q = (keys[KB_KEY_Q] != 0) &&
                         ((keys[KB_KEY_LEFT_SUPER] != 0) || (keys[KB_KEY_RIGHT_SUPER] != 0));
            if (esc || cmd_q) {
                break;
            }
        }

#if HOST_VIEWER_FIXED_TIMESTEP_ENABLED
        double sim_now_s = (double)mfb_timer_now(timer);
        double frame_dt_s = sim_now_s - sim_prev_time_s;
        sim_prev_time_s = sim_now_s;
        if (frame_dt_s < 0.0) frame_dt_s = 0.0;
        if (frame_dt_s > 0.25) frame_dt_s = 0.25;
        const double sim_dt_s = 1.0 / HOST_VIEWER_SIM_HZ;
        sim_accumulator_s += frame_dt_s;
        unsigned sim_steps = 0;
        while (sim_accumulator_s >= sim_dt_s && sim_steps < HOST_VIEWER_MAX_SIM_STEPS_PER_FRAME) {
            frame_tick++;
            if (collision_cooldown_frames > 0) {
                collision_cooldown_frames--;
            }
            sim_accumulator_s -= sim_dt_s;
            sim_steps++;
        }
        if (sim_steps == HOST_VIEWER_MAX_SIM_STEPS_PER_FRAME && sim_accumulator_s > sim_dt_s) {
            /* Drop backlog to avoid visible spiral-of-death stutter. */
            sim_accumulator_s = sim_dt_s;
        }
#else
        frame_tick++;
        if (collision_cooldown_frames > 0) {
            collision_cooldown_frames--;
        }
#endif
        int terrain_scroll_px = (int)((frame_tick * 2u) % 320u);
        int player_bob_y = player_bob_lut[frame_tick % (sizeof(player_bob_lut) / sizeof(player_bob_lut[0]))];
        int obstacle_x = 319 - (int)((frame_tick * 3u) % 360u);

        game_terrain.transform = vg_transform_identity();
        game_terrain.transform.tx = (float)(-terrain_scroll_px);
        game_player.transform = vg_transform_identity();
        game_player.transform.ty = (float)player_bob_y;
        game_obstacle_body.transform = vg_transform_identity();
        game_obstacle_body.transform.tx = (float)(obstacle_x + 20);
        game_obstacle_body.transform.ty = 126.0f;
        game_obstacle_body.transform.rot_deg = -90.0f;
        game_obstacle_nose.transform = vg_transform_identity();
        game_obstacle_nose.transform.tx = (float)(obstacle_x + 20);
        game_obstacle_nose.transform.ty = 126.0f;
        game_obstacle_nose.transform.rot_deg = -90.0f;

        // Derived automatically from geometry + manual transform call.
        // Collision remains on manual hitbox for now.
        VgIntAabb obstacle_bbox_auto = {0};
        bool obstacle_bbox_auto_ok = compute_obstacle_world_aabb_cached_manual_transform(
            &game_obstacle_body, &game_obstacle_nose, &obstacle_bbox_cache, &obstacle_bbox_auto);
        (void)obstacle_bbox_auto_ok;
        (void)obstacle_bbox_auto;

        if (player_small) {
            game_player.data.tri.x1 = 60;
            game_player.data.tri.y1 = 146;
            game_player.data.tri.x2 = 72;
            game_player.data.tri.y2 = 126;
            game_player.data.tri.x3 = 84;
            game_player.data.tri.y3 = 146;
        } else {
            game_player.data.tri.x1 = 56;
            game_player.data.tri.y1 = 146;
            game_player.data.tri.x2 = 72;
            game_player.data.tri.y2 = 118;
            game_player.data.tri.x3 = 88;
            game_player.data.tri.y3 = 146;
        }

        {
            // Collision uses a stable hitbox to avoid size-toggle feedback jitter.
            int player_min_x = 58;
            int player_max_x = 86;
            int player_min_y = 124 + player_bob_y;
            int player_max_y = 146 + player_bob_y;
            int obstacle_min_x_i = 13 + obstacle_x;
            int obstacle_max_x_i = 27 + obstacle_x;
            int obstacle_min_y_i = 106;
            int obstacle_max_y_i = 146;
            bool colliding = (player_max_x >= obstacle_min_x_i) && (player_min_x <= obstacle_max_x_i) &&
                             (player_max_y >= obstacle_min_y_i) && (player_min_y <= obstacle_max_y_i);

            if (colliding && !collision_latched && collision_cooldown_frames == 0) {
                player_small = !player_small;
                collision_latched = true;
                collision_cooldown_frames = 18;
            } else if (!colliding) {
                collision_latched = false;
            }
        }

        if (score_changed) {
            publish_frame_scene_slot(1, &score_root, score_clip, 1, true, true, 0x0000u, 1);
        }
        publish_frame_scene_slot(2, &game_root, game_clip, 2, true, true, 0x0000u, 1);

        for (size_t i = 0; i < VIEWER_SLOT_COUNT; i++) {
            if (g_published_slots[i]) {
                (void)vg_render_frame_slot_record_if_changed(g_published_slots[i], &slot_states[i], &fb, g_published_slot_generation[i]);
            }
        }

        for (size_t i = 0; i < (size_t)VIEW_W * (size_t)VIEW_H; i++) {
            window_pixels[i] = rgb565_to_xrgb8888(fb_pixels[i]);
        }

        mfb_update_state state = mfb_update_ex(window, window_pixels, VIEW_W, VIEW_H);
        if (state != STATE_OK) {
            break;
        }
    }

    mfb_timer_destroy(timer);
#if defined(__APPLE__)
    macos_viewer_save_window_position();
#endif
    mfb_close(window);
    for (size_t i = 0; i < VIEWER_SLOT_COUNT; i++) {
        RELEASE(g_published_slots[i]);
        g_published_slots[i] = NULL;
    }
    runtime_reset(&g_runtime);
    return 0;
#endif
}
