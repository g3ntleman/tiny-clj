#include "scene.h"
#include "gfx.h"
#include "tiny_gfx.h"

#include <limits.h>
#include "callbacks.h"
#include "record.h"
#include "strings.h"
#include "symbol.h"
#include "value.h"
#include "vector.h"

static inline uint32_t record_type_hash(ID obj) {
    CljPersistentRecord *r = (CljPersistentRecord *)obj;
    return r->descriptor ? clj_hash(r->descriptor->type_symbol) : 0;
}

static inline void transform_point(VgTransformFixed t, int16_t x, int16_t y, int *ox, int *oy) {
    vg_transform_fixed_apply_px(t, x, y, ox, oy);
}

static inline bool aabb_outside_fb(int min_x, int min_y, int max_x, int max_y,
                                   int fb_w, int fb_h,
                                   bool use_clip, VgClipRect clip) {
    int lo_x = 0, lo_y = 0, hi_x = fb_w, hi_y = fb_h;
    if (use_clip) {
        lo_x = clip.x; lo_y = clip.y;
        hi_x = clip.x + clip.w; hi_y = clip.y + clip.h;
    }
    return max_x < lo_x || min_x >= hi_x || max_y < lo_y || min_y >= hi_y;
}

static inline bool node_culled_line(VgTransformFixed t, int16_t x1, int16_t y1,
                                    int16_t x2, int16_t y2, int sw,
                                    int fb_w, int fb_h, bool use_clip, VgClipRect clip) {
    int ax, ay, bx, by;
    transform_point(t, x1, y1, &ax, &ay);
    transform_point(t, x2, y2, &bx, &by);
    int mn_x = (ax < bx ? ax : bx) - sw;
    int mn_y = (ay < by ? ay : by) - sw;
    int mx_x = (ax > bx ? ax : bx) + sw;
    int mx_y = (ay > by ? ay : by) + sw;
    return aabb_outside_fb(mn_x, mn_y, mx_x, mx_y, fb_w, fb_h, use_clip, clip);
}

static inline bool node_culled_rect(VgTransformFixed t, int16_t x, int16_t y,
                                    int16_t w, int16_t h, int sw,
                                    int fb_w, int fb_h, bool use_clip, VgClipRect clip) {
    int c[8];
    transform_point(t, x,     y,     &c[0], &c[1]);
    transform_point(t, (int16_t)(x+w), y,     &c[2], &c[3]);
    transform_point(t, (int16_t)(x+w), (int16_t)(y+h), &c[4], &c[5]);
    transform_point(t, x,     (int16_t)(y+h), &c[6], &c[7]);
    int mn_x = c[0], mx_x = c[0], mn_y = c[1], mx_y = c[1];
    for (int i = 2; i < 8; i += 2) {
        if (c[i]   < mn_x) mn_x = c[i];
        if (c[i]   > mx_x) mx_x = c[i];
        if (c[i+1] < mn_y) mn_y = c[i+1];
        if (c[i+1] > mx_y) mx_y = c[i+1];
    }
    return aabb_outside_fb(mn_x - sw, mn_y - sw, mx_x + sw, mx_y + sw, fb_w, fb_h, use_clip, clip);
}

static inline bool node_culled_tri(VgTransformFixed t,
                                   int16_t x1, int16_t y1,
                                   int16_t x2, int16_t y2,
                                   int16_t x3, int16_t y3, int sw,
                                   int fb_w, int fb_h, bool use_clip, VgClipRect clip) {
    int ax, ay, bx, by, cx, cy;
    transform_point(t, x1, y1, &ax, &ay);
    transform_point(t, x2, y2, &bx, &by);
    transform_point(t, x3, y3, &cx, &cy);
    int mn_x = ax, mx_x = ax, mn_y = ay, mx_y = ay;
    if (bx < mn_x) mn_x = bx; if (bx > mx_x) mx_x = bx;
    if (by < mn_y) mn_y = by; if (by > mx_y) mx_y = by;
    if (cx < mn_x) mn_x = cx; if (cx > mx_x) mx_x = cx;
    if (cy < mn_y) mn_y = cy; if (cy > mx_y) mx_y = cy;
    return aabb_outside_fb(mn_x - sw, mn_y - sw, mx_x + sw, mx_y + sw, fb_w, fb_h, use_clip, clip);
}

static int32_t fixed_payload_raw(ID v) {
    return (int32_t)((intptr_t)v >> TAG_BITS);
}

static int32_t fixed_raw_to_int_trunc_zero(int32_t raw) {
    return raw / CLJ_FIXED_SCALE;
}

static bool id_to_bool_default(ID v, bool default_value) {
    if (!v) {
        return default_value;
    }
    return v != clj_false;
}

/** Decode a Clojure numeric value to raw Q19.13 fixed-point (same as CLJ_FIXED_SCALE). */
static int32_t id_to_fixed_raw_default(ID v, int32_t default_value) {
    if (!v) return default_value;
    if (is_fixnum(v)) {
        return (int32_t)as_fixnum(v) << CLJ_FIXED_FRAC_BITS;
    }
    if (is_fixed(v)) {
        return fixed_payload_raw(v);
    }
    return default_value;
}

static int16_t id_to_i16_default(ID v, int16_t default_value) {
    if (!v) {
        return default_value;
    }
    if (is_fixnum(v)) {
        return (int16_t)as_fixnum(v);
    }
    if (is_fixed(v)) {
        return (int16_t)fixed_raw_to_int_trunc_zero(fixed_payload_raw(v));
    }
    return default_value;
}

static uint16_t id_to_u16_default(ID v, uint16_t default_value) {
    if (!v) {
        return default_value;
    }
    if (is_fixnum(v)) {
        return (uint16_t)as_fixnum(v);
    }
    if (is_fixed(v)) {
        return (uint16_t)fixed_raw_to_int_trunc_zero(fixed_payload_raw(v));
    }
    return default_value;
}

static uint8_t id_to_u8_default(ID v, uint8_t default_value) {
    if (!v) {
        return default_value;
    }
    if (is_fixnum(v)) {
        return (uint8_t)as_fixnum(v);
    }
    if (is_fixed(v)) {
        return (uint8_t)fixed_raw_to_int_trunc_zero(fixed_payload_raw(v));
    }
    return default_value;
}

static const char *id_to_text_cstr(ID v) {
    if (!v) {
        return "";
    }
    CljType v_tag = TAG(v);
    if (v_tag == CLJ_STRING) {
        const char *s = string_data(v);
        return s ? s : "";
    }
    if (v_tag == CLJ_SYMBOL) {
        CljSymbol *sym = as_symbol(v);
        if (sym && sym->cname) {
            return sym->cname;
        }
    }
    return "";
}



static VgTransformFixed decode_transform_fixed(ID obj, const VgRecordSchema *s) {
    VgTransform t = vg_transform_identity();
    if (!obj || TAG(obj) != CLJ_RECORD) {
        return vg_transform_fixed_identity();
    }
    if (record_type_hash(obj) == s->h_transform) {
        Transform *tr = obj;
        t.tx = id_to_i16_default(tr->tx, 0);
        t.ty = id_to_i16_default(tr->ty, 0);
        t.sx = id_to_fixed_raw_default(tr->sx, VG_SCALE_ONE);
        t.sy = id_to_fixed_raw_default(tr->sy, VG_SCALE_ONE);
        t.rot_deg = id_to_i16_default(tr->rot, 0);
        return vg_transform_fixed_from_transform(t);
    }
    return vg_transform_fixed_identity();
}

static ID node_style_field(ID node_obj, uint32_t h, const VgRecordSchema *s) {
    if (h == s->h_group)    return ((Group *)node_obj)->style;
    if (h == s->h_line)     return ((Line *)node_obj)->style;
    if (h == s->h_polyline) return ((Polyline *)node_obj)->style;
    if (h == s->h_rect)     return ((Rect *)node_obj)->style;
    if (h == s->h_tri)      return ((Tri *)node_obj)->style;
    if (h == s->h_vtext)    return ((VText *)node_obj)->style;
    return NULL;
}

static ID node_visible_field(ID node_obj, uint32_t h, const VgRecordSchema *s) {
    if (h == s->h_group)    return ((Group *)node_obj)->visible;
    if (h == s->h_line)     return ((Line *)node_obj)->visible;
    if (h == s->h_polyline) return ((Polyline *)node_obj)->visible;
    if (h == s->h_rect)     return ((Rect *)node_obj)->visible;
    if (h == s->h_tri)      return ((Tri *)node_obj)->visible;
    if (h == s->h_vtext)    return ((VText *)node_obj)->visible;
    return NULL;
}

static VgStyle decode_style(ID node_obj, uint32_t node_h, const VgRecordSchema *sc) {
    VgStyle st = vg_style_default();
    if (!node_obj) {
        return st;
    }
    ID style_obj = node_style_field(node_obj, node_h, sc);
    if (style_obj && TAG(style_obj) == CLJ_RECORD && record_type_hash(style_obj) == sc->h_style) {
        Style *sr = style_obj;
        st.stroke_rgb565 = id_to_u16_default(sr->stroke_rgb565, st.stroke_rgb565);
        st.stroke_width = id_to_u8_default(sr->stroke_width, st.stroke_width);
        st.has_fill = id_to_bool_default(sr->has_fill, st.has_fill);
        st.fill_rgb565 = id_to_u16_default(sr->fill_rgb565, st.fill_rgb565);
        st.has_bg_rgb565 = id_to_bool_default(sr->has_bg_rgb565, st.has_bg_rgb565);
        st.bg_rgb565 = id_to_u16_default(sr->bg_rgb565, st.bg_rgb565);
        st.visible = id_to_bool_default(sr->visible, st.visible);
    }
    ID node_visible = node_visible_field(node_obj, node_h, sc);
    if (node_visible) {
        st.visible = id_to_bool_default(node_visible, st.visible);
    } else if (node_visible == clj_false) {
        st.visible = false;
    }
    if (st.stroke_width == 0) {
        st.stroke_width = 1;
    }
    return st;
}

static bool render_record_node(ID node_obj,
                               VgTransformFixed parent_t,
                               VgFrameBuffer *fb,
                               bool use_clip,
                               VgClipRect clip_rect);

static bool decode_rect(ID obj, VgClipRect *out_rect, const VgRecordSchema *sc) {
    if (!obj || !out_rect) {
        return false;
    }
    CljType obj_tag = TAG(obj);
    if (obj_tag == CLJ_VECTOR_PERSISTENT || obj_tag == CLJ_VECTOR_TRANSIENT) {
        CljPersistentVector *v = as_vector(obj);
        if (vector_count(v) < 4) {
            return false;
        }
        out_rect->x = id_to_i16_default(vector_nth(v, 0), 0);
        out_rect->y = id_to_i16_default(vector_nth(v, 1), 0);
        out_rect->w = id_to_i16_default(vector_nth(v, 2), 0);
        out_rect->h = id_to_i16_default(vector_nth(v, 3), 0);
        return !vg_clip_rect_is_empty(*out_rect);
    }
    if (obj_tag == CLJ_RECORD && record_type_hash(obj) == sc->h_rect) {
        Rect *r = obj;
        out_rect->x = id_to_i16_default(r->x, 0);
        out_rect->y = id_to_i16_default(r->y, 0);
        out_rect->w = id_to_i16_default(r->w, 0);
        out_rect->h = id_to_i16_default(r->h, 0);
        return !vg_clip_rect_is_empty(*out_rect);
    }
    return false;
}

static bool render_one_temp_node(const VgNode *node, VgTransformFixed world_t, VgFrameBuffer *fb, bool use_clip, VgClipRect clip_rect) {
    if (!node || !fb) {
        return false;
    }
    if (use_clip) {
        vg_render_node_fixed_clipped(node, world_t, fb, clip_rect);
    } else {
        vg_render_node_fixed(node, world_t, fb);
    }
    return true;
}

static bool render_polyline_record(ID node_obj,
                                   VgTransformFixed world_t,
                                   VgStyle style,
                                   VgFrameBuffer *fb,
                                   bool use_clip,
                                   VgClipRect clip_rect) {
    Polyline *pr = node_obj;
    ID pts = pr->pts;
    ID closed = pr->closed;
    if (!pts || TAG(pts) != CLJ_VECTOR_PERSISTENT) {
        return true;
    }
    CljPersistentVector *pv = as_vector(pts);
    unsigned int n = vector_count(pv);
    if (n < 2 || n > GFX_FILL_MAX_VERTS) {
        return n < 2;
    }
    VgPoint points[GFX_FILL_MAX_VERTS];
    for (unsigned int i = 0; i < n; i++) {
        ID point = vector_nth(pv, i);
        if (!point || TAG(point) != CLJ_VECTOR_PERSISTENT) {
            return false;
        }
        CljPersistentVector *xy = as_vector(point);
        if (vector_count(xy) < 2) {
            return false;
        }
        points[i].x = id_to_i16_default(vector_nth(xy, 0), 0);
        points[i].y = id_to_i16_default(vector_nth(xy, 1), 0);
    }

    int sw = style.stroke_width ? style.stroke_width : 1;
    {
        int mn_x = INT_MAX, mn_y = INT_MAX, mx_x = INT_MIN, mx_y = INT_MIN;
        for (unsigned int i = 0; i < n; i++) {
            int wx, wy;
            transform_point(world_t, points[i].x, points[i].y, &wx, &wy);
            if (wx < mn_x) mn_x = wx; if (wx > mx_x) mx_x = wx;
            if (wy < mn_y) mn_y = wy; if (wy > mx_y) mx_y = wy;
        }
        if (aabb_outside_fb(mn_x - sw, mn_y - sw, mx_x + sw, mx_y + sw,
                            fb->width, fb->height, use_clip, clip_rect))
            return true;
    }

    VgNode temp;
    memset(&temp, 0, sizeof(temp));
    temp.type = VG_NODE_POLYLINE;
    temp.has_transform = false;
    temp.style = style;
    temp.data.polyline.points = points;
    temp.data.polyline.point_count = (size_t)n;
    temp.data.polyline.closed = id_to_bool_default(closed, false);
    return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
}

static bool render_record_node(ID node_obj,
                               VgTransformFixed parent_t,
                               VgFrameBuffer *fb,
                               bool use_clip,
                               VgClipRect clip_rect) {
    if (!node_obj || TAG(node_obj) != CLJ_RECORD || !fb) {
        return false;
    }
    const VgRecordSchema *sc = tiny_gfx_schema();
    uint32_t h = record_type_hash(node_obj);

    ID local_t_obj = NULL;
    if      (h == sc->h_group)    local_t_obj = ((Group *)node_obj)->t;
    else if (h == sc->h_line)     local_t_obj = ((Line *)node_obj)->t;
    else if (h == sc->h_polyline) local_t_obj = ((Polyline *)node_obj)->t;
    else if (h == sc->h_rect)     local_t_obj = ((Rect *)node_obj)->t;
    else if (h == sc->h_tri)      local_t_obj = ((Tri *)node_obj)->t;
    else if (h == sc->h_vtext)    local_t_obj = ((VText *)node_obj)->t;

    VgTransformFixed world_t = parent_t;
    if (local_t_obj) {
        VgTransformFixed local_t = decode_transform_fixed(local_t_obj, sc);
        world_t = vg_transform_fixed_compose(parent_t, local_t);
    }
    VgStyle style = decode_style(node_obj, h, sc);
    if (!style.visible) {
        return true;
    }

    if (h == sc->h_group) {
        Group *group = node_obj;
        ID children = group->children;
        if (!children || TAG(children) != CLJ_VECTOR_PERSISTENT) {
            return true;
        }
        CljPersistentVector *vec = as_vector(children);
        unsigned int count = vector_count(vec);
        for (unsigned int i = 0; i < count; i++) {
            ID child = vector_nth(vec, i);
            if (!child) {
                continue;
            }
            if (!render_record_node(child, world_t, fb, use_clip, clip_rect)) {
                return false;
            }
        }
        return true;
    }

    int sw = style.stroke_width ? style.stroke_width : 1;
    int fb_w = fb->width, fb_h = fb->height;

    VgNode temp;
    memset(&temp, 0, sizeof(temp));
    temp.has_transform = false;
    temp.style = style;

    if (h == sc->h_line) {
        temp.type = VG_NODE_LINE;
        Line *line = node_obj;
        temp.data.line.x1 = id_to_i16_default(line->x1, 0);
        temp.data.line.y1 = id_to_i16_default(line->y1, 0);
        temp.data.line.x2 = id_to_i16_default(line->x2, 0);
        temp.data.line.y2 = id_to_i16_default(line->y2, 0);
        if (node_culled_line(world_t, temp.data.line.x1, temp.data.line.y1,
                             temp.data.line.x2, temp.data.line.y2, sw,
                             fb_w, fb_h, use_clip, clip_rect))
            return true;
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (h == sc->h_rect) {
        temp.type = VG_NODE_RECT;
        Rect *rect = node_obj;
        temp.data.rect.x = id_to_i16_default(rect->x, 0);
        temp.data.rect.y = id_to_i16_default(rect->y, 0);
        temp.data.rect.w = id_to_i16_default(rect->w, 0);
        temp.data.rect.h = id_to_i16_default(rect->h, 0);
        if (node_culled_rect(world_t, temp.data.rect.x, temp.data.rect.y,
                             temp.data.rect.w, temp.data.rect.h, sw,
                             fb_w, fb_h, use_clip, clip_rect))
            return true;
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (h == sc->h_tri) {
        temp.type = VG_NODE_TRI;
        Tri *tri = node_obj;
        temp.data.tri.x1 = id_to_i16_default(tri->x1, 0);
        temp.data.tri.y1 = id_to_i16_default(tri->y1, 0);
        temp.data.tri.x2 = id_to_i16_default(tri->x2, 0);
        temp.data.tri.y2 = id_to_i16_default(tri->y2, 0);
        temp.data.tri.x3 = id_to_i16_default(tri->x3, 0);
        temp.data.tri.y3 = id_to_i16_default(tri->y3, 0);
        if (node_culled_tri(world_t,
                            temp.data.tri.x1, temp.data.tri.y1,
                            temp.data.tri.x2, temp.data.tri.y2,
                            temp.data.tri.x3, temp.data.tri.y3, sw,
                            fb_w, fb_h, use_clip, clip_rect))
            return true;
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (h == sc->h_vtext) {
        temp.type = VG_NODE_VTEXT;
        VText *text = node_obj;
        temp.data.text.x = id_to_i16_default(text->x, 0);
        temp.data.text.y = id_to_i16_default(text->y, 0);
        temp.data.text.scale = id_to_fixed_raw_default(text->scale, VG_SCALE_ONE);
        temp.data.text.rot_deg = id_to_i16_default(text->rot, 0);
        temp.data.text.text = id_to_text_cstr(text->text);
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (h == sc->h_polyline) {
        return render_polyline_record(node_obj, world_t, style, fb, use_clip, clip_rect);
    }
    return false;
}

static bool decode_scene_fields(ID scene_record, ID *out_root, ID *out_clip, ID *out_erase) {
    if (!scene_record || TAG(scene_record) != CLJ_RECORD) return false;
    const VgRecordSchema *sc = tiny_gfx_schema();
    uint32_t h = record_type_hash(scene_record);
    if (h == sc->h_frame_scene) {
        FrameScene *fs = scene_record;
        *out_root = fs->root;
        *out_clip = fs->clip_rect;
        if (out_erase) *out_erase = fs->erase_rgb565;
        return true;
    }
    if (h == sc->h_scene) {
        Scene *s = scene_record;
        *out_root = s->root;
        *out_clip = s->clip_rect;
        if (out_erase) *out_erase = s->erase_rgb565;
        return true;
    }
    return false;
}

bool vg_render_scene_record(ID scene_record, VgFrameBuffer *fb) {
    if (!scene_record || !fb || TAG(scene_record) != CLJ_RECORD) {
        return false;
    }

    ID root = NULL;
    ID clip_source = NULL;
    ID erase_source = NULL;
    if (!decode_scene_fields(scene_record, &root, &clip_source, &erase_source)) {
        return false;
    }

    const VgRecordSchema *sc = tiny_gfx_schema();
    VgClipRect effective_rect = {0, 0, 0, 0};
    bool has_effective_rect = decode_rect(clip_source, &effective_rect, sc);

    if (has_effective_rect) {
        uint16_t erase_rgb565 = id_to_u16_default(erase_source, 0x0000u);
        vg_framebuffer_clear_rect(fb, effective_rect, erase_rgb565);
    }

    if (!root) {
        return true;
    }
    return render_record_node(root, vg_transform_fixed_identity(), fb, has_effective_rect, effective_rect);
}

bool vg_render_scene_record_clipped(ID scene_record, VgFrameBuffer *fb, VgClipRect clip_rect) {
    if (!scene_record || !fb || TAG(scene_record) != CLJ_RECORD) {
        return false;
    }

    ID root = NULL;
    ID clip_source = NULL;
    if (!decode_scene_fields(scene_record, &root, &clip_source, NULL)) {
        return false;
    }
    if (!root) {
        return true;
    }

    const VgRecordSchema *sc = tiny_gfx_schema();
    VgClipRect effective_clip = clip_rect;
    VgClipRect scene_clip = {0, 0, 0, 0};
    bool has_scene_clip = decode_rect(clip_source, &scene_clip, sc);
    if (has_scene_clip) {
        if (!vg_clip_rect_intersect(clip_rect, scene_clip, &effective_clip)) {
            return true;
        }
    }
    return render_record_node(root, vg_transform_fixed_identity(), fb, true, effective_clip);
}

bool vg_decode_frame_slot_record(ID frame_scene_record, VgRenderSlot *out_slot) {
    if (!frame_scene_record || !out_slot || TAG(frame_scene_record) != CLJ_RECORD) {
        return false;
    }
    if (TAG(frame_scene_record) != CLJ_RECORD) return false;
    const VgRecordSchema *sc = tiny_gfx_schema();
    if (record_type_hash(frame_scene_record) != sc->h_frame_scene) {
        return false;
    }

    FrameScene *scene = frame_scene_record;
    VgClipRect clip = {0, 0, 0, 0};
    if (!decode_rect(scene->clip_rect, &clip, sc)) {
        return false;
    }

    out_slot->root = scene->root;
    out_slot->clip_rect = clip;
    out_slot->z = id_to_i16_default(scene->z, 0);
    out_slot->visible = id_to_bool_default(scene->visible, true);
    out_slot->opaque = id_to_bool_default(scene->opaque, true);
    out_slot->clear_rgb565 = id_to_u16_default(scene->erase_rgb565, 0x0000u);
    out_slot->guard_px = id_to_u8_default(scene->guard_px, 0);
    return true;
}

bool vg_render_frame_slot_record_if_changed(ID frame_scene_record,
                                            VgRenderSlotState *state,
                                            VgFrameBuffer *fb,
                                            uint32_t snapshot_id) {
    VgRenderSlot slot;
    if (!state || !fb || !vg_decode_frame_slot_record(frame_scene_record, &slot)) {
        return false;
    }

    bool props_changed = !state->initialized ||
                         state->last_visible != slot.visible ||
                         state->last_opaque != slot.opaque ||
                         state->last_clear_rgb565 != slot.clear_rgb565 ||
                         state->last_guard_px != slot.guard_px ||
                         !vg_clip_rect_equal(state->last_clip_rect, slot.clip_rect);
    bool snapshot_changed = !state->initialized || state->snapshot_id != snapshot_id;
    if (!props_changed && !snapshot_changed) {
        return false;
    }

    VgClipRect dirty = vg_clip_rect_expand(slot.clip_rect, slot.guard_px);
    if (state->initialized) {
        VgClipRect prev = vg_clip_rect_expand(state->last_clip_rect, state->last_guard_px);
        dirty = vg_clip_rect_union(prev, dirty);
    }
    vg_framebuffer_clear_rect(fb, dirty, slot.clear_rgb565);

    if (slot.visible) {
        (void)vg_render_scene_record_clipped(frame_scene_record, fb, slot.clip_rect);
    }

    state->initialized = true;
    state->snapshot_id = snapshot_id;
    state->last_clip_rect = slot.clip_rect;
    state->last_visible = slot.visible;
    state->last_opaque = slot.opaque;
    state->last_clear_rgb565 = slot.clear_rgb565;
    state->last_guard_px = slot.guard_px;
    return true;
}
