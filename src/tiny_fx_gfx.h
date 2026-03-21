#ifndef TINY_CLJ_TINY_FX_GFX_H
#define TINY_CLJ_TINY_FX_GFX_H

#include <stdbool.h>

#include "object.h"
#include "record.h"
#include "namespace.h"

typedef struct {
    ID t_transform, t_style, t_group, t_line, t_polyline, t_rect, t_tri, t_vtext, t_timeline, t_frame_scene, t_scene;
    CljRecordDescriptor *d_transform, *d_style, *d_group, *d_line, *d_polyline, *d_rect, *d_tri, *d_vtext, *d_timeline, *d_frame_scene, *d_scene;
    uint32_t h_transform, h_style, h_group, h_line, h_polyline, h_rect, h_tri, h_vtext, h_timeline, h_frame_scene, h_scene;
    unsigned int n_transform, n_style, n_group, n_line, n_polyline, n_rect, n_tri, n_vtext, n_timeline, n_frame_scene, n_scene;
    int transform_tx, transform_ty, transform_sx, transform_sy, transform_rot;
    int style_stroke_color, style_stroke_width, style_visible, style_has_fill, style_fill_color, style_has_bg_color, style_bg_color;
    int group_id, group_t, group_style, group_visible, group_children;
    int line_id, line_t, line_style, line_visible, line_x1, line_y1, line_x2, line_y2;
    int poly_id, poly_t, poly_style, poly_visible, poly_pts, poly_closed;
    int rect_id, rect_t, rect_style, rect_visible, rect_x, rect_y, rect_w, rect_h;
    int tri_id, tri_t, tri_style, tri_visible, tri_x1, tri_y1, tri_x2, tri_y2, tri_x3, tri_y3;
    int text_id, text_t, text_style, text_visible, text_x, text_y, text_scale, text_rot, text_text;
    int timeline_keyframes, timeline_loop, timeline_end_event;
    int frame_root, frame_index, frame_clip_rect, frame_z, frame_visible, frame_opaque, frame_erase_color, frame_guard_px;
    int scene_root, scene_index, scene_clip_rect, scene_erase_color;
} VgRecordSchema;

typedef struct {
    ID k_root;
    ID k_index;
    ID k_id;
    ID k_t;
    ID k_style;
    ID k_visible;
    ID k_children;
    ID k_x1;
    ID k_y1;
    ID k_x2;
    ID k_y2;
    ID k_x3;
    ID k_y3;
    ID k_x;
    ID k_y;
    ID k_w;
    ID k_h;
    ID k_pts;
    ID k_closed;
    ID k_scale;
    ID k_rot;
    ID k_text;
    ID k_keyframes;
    ID k_loop;
    ID k_end_event;
    ID k_tx;
    ID k_ty;
    ID k_sx;
    ID k_sy;
    ID k_stroke_color;
    ID k_stroke_width;
    ID k_has_fill;
    ID k_fill_color;
    ID k_has_bg_color;
    ID k_bg_color;
    ID k_clip_rect;
    ID k_erase_color;
    ID k_z;
    ID k_opaque;
    ID k_guard_px;
} VgRecordKeys;

// Layout-compatible typed overlays for scene record payloads.
DEFRECORD(Transform, tx, ty, sx, sy, rot)
DEFRECORD(Style, stroke_color, stroke_width, visible, has_fill, fill_color, has_bg_color, bg_color)
DEFRECORD(Group, id, t, style, visible, children, prototype)
DEFRECORD(Line, id, t, style, visible, x1, y1, x2, y2, prototype)
DEFRECORD(Polyline, id, t, style, visible, pts, closed, prototype)
DEFRECORD(Rect, id, t, style, visible, x, y, w, h, prototype)
DEFRECORD(Tri, id, t, style, visible, x1, y1, x2, y2, x3, y3, prototype)
DEFRECORD(VText, id, t, style, visible, x, y, scale, rot, text, prototype)
DEFRECORD(Timeline, keyframes, loop, end_event)
DEFRECORD(FrameScene, root, index, clip_rect, z, visible, opaque, erase_color, guard_px, collision_rules)
DEFRECORD(Scene, root, index, clip_rect, erase_color, collision_rules)

bool tiny_fx_gfx_ensure_schema(EvalState *st);
const VgRecordSchema *tiny_fx_gfx_schema(void);
const VgRecordKeys *tiny_fx_gfx_record_keys(void);
// Returns a borrowed alias; caller must ensure the parent record outlives usage.
ID tiny_fx_gfx_get_field(ID record_obj, ID key, ID not_found);
ID tiny_fx_gfx_create_record_from_slots(ID type_symbol, unsigned int field_count, ID *slots);

#endif // TINY_CLJ_TINY_FX_GFX_H
