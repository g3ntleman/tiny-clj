#include "scene.h"
#include "gfx.h"
#include "tiny_gfx.h"

#include <string.h>

#include "record.h"
#include "strings.h"
#include "symbol.h"
#include "value.h"
#include "vector.h"

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

static bool record_type_is(ID record_obj, const char *simple_type_name) {
    if (!record_obj || TAG(record_obj) != CLJ_RECORD || !simple_type_name) {
        return false;
    }
    ID type_sym = record_type_symbol(record_obj);
    if (!type_sym || TAG(type_sym) != CLJ_SYMBOL) {
        return false;
    }
    CljSymbol *sym = as_symbol(type_sym);
    if (!sym || !sym->cname) {
        return false;
    }
    const char *name = sym->cname;
    if (strcmp(name, simple_type_name) == 0) {
        return true;
    }
    const char *slash = strrchr(name, '/');
    if (!slash) {
        return false;
    }
    return strcmp(slash + 1, simple_type_name) == 0;
}

static void assert_record_field_count_at_least(ID record_obj, unsigned int min_count, const char *type_name) {
    CLJ_ASSERT(record_obj && TAG(record_obj) == CLJ_RECORD && "expected record object");
    CljPersistentRecord *record = record_obj;
    CLJ_ASSERT(record_declared_field_count(record) >= min_count && "record layout mismatch for typed overlay");
    (void)type_name;
}

static VgTransformFixed decode_transform_fixed(ID obj) {
    VgTransform t = vg_transform_identity();
    if (!obj) {
        return vg_transform_fixed_identity();
    }
    if (record_type_is(obj, "Transform")) {
        assert_record_field_count_at_least(obj, 5, "Transform");
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

static ID node_style_field(ID node_obj) {
    if (record_type_is(node_obj, "Group")) {
        assert_record_field_count_at_least(node_obj, 5, "Group");
        Group *r = node_obj;
        return r->style;
    }
    if (record_type_is(node_obj, "Line")) {
        assert_record_field_count_at_least(node_obj, 8, "Line");
        Line *r = node_obj;
        return r->style;
    }
    if (record_type_is(node_obj, "Polyline")) {
        assert_record_field_count_at_least(node_obj, 6, "Polyline");
        Polyline *r = node_obj;
        return r->style;
    }
    if (record_type_is(node_obj, "Rect")) {
        assert_record_field_count_at_least(node_obj, 8, "Rect");
        Rect *r = node_obj;
        return r->style;
    }
    if (record_type_is(node_obj, "Tri")) {
        assert_record_field_count_at_least(node_obj, 10, "Tri");
        Tri *r = node_obj;
        return r->style;
    }
    if (record_type_is(node_obj, "VText")) {
        assert_record_field_count_at_least(node_obj, 9, "VText");
        VText *r = node_obj;
        return r->style;
    }
    return NULL;
}

static ID node_visible_field(ID node_obj) {
    if (record_type_is(node_obj, "Group")) {
        assert_record_field_count_at_least(node_obj, 5, "Group");
        Group *r = node_obj;
        return r->visible;
    }
    if (record_type_is(node_obj, "Line")) {
        assert_record_field_count_at_least(node_obj, 8, "Line");
        Line *r = node_obj;
        return r->visible;
    }
    if (record_type_is(node_obj, "Polyline")) {
        assert_record_field_count_at_least(node_obj, 6, "Polyline");
        Polyline *r = node_obj;
        return r->visible;
    }
    if (record_type_is(node_obj, "Rect")) {
        assert_record_field_count_at_least(node_obj, 8, "Rect");
        Rect *r = node_obj;
        return r->visible;
    }
    if (record_type_is(node_obj, "Tri")) {
        assert_record_field_count_at_least(node_obj, 10, "Tri");
        Tri *r = node_obj;
        return r->visible;
    }
    if (record_type_is(node_obj, "VText")) {
        assert_record_field_count_at_least(node_obj, 9, "VText");
        VText *r = node_obj;
        return r->visible;
    }
    return NULL;
}

static VgStyle decode_style(ID node_obj) {
    VgStyle s = vg_style_default();
    if (!node_obj) {
        return s;
    }
    ID style_obj = node_style_field(node_obj);
    if (style_obj && record_type_is(style_obj, "Style")) {
        assert_record_field_count_at_least(style_obj, 7, "Style");
        Style *sr = style_obj;
        s.stroke_rgb565 = id_to_u16_default(sr->stroke_rgb565, s.stroke_rgb565);
        s.stroke_width = id_to_u8_default(sr->stroke_width, s.stroke_width);
        s.has_fill = id_to_bool_default(sr->has_fill, s.has_fill);
        s.fill_rgb565 = id_to_u16_default(sr->fill_rgb565, s.fill_rgb565);
        s.has_bg_rgb565 = id_to_bool_default(sr->has_bg_rgb565, s.has_bg_rgb565);
        s.bg_rgb565 = id_to_u16_default(sr->bg_rgb565, s.bg_rgb565);
        s.visible = id_to_bool_default(sr->visible, s.visible);
    }
    ID node_visible = node_visible_field(node_obj);
    if (node_visible) {
        s.visible = id_to_bool_default(node_visible, s.visible);
    } else if (node_visible == clj_false) {
        s.visible = false;
    }
    if (s.stroke_width == 0) {
        s.stroke_width = 1;
    }
    return s;
}

static bool render_record_node(ID node_obj,
                               VgTransformFixed parent_t,
                               VgFrameBuffer *fb,
                               bool use_clip,
                               VgClipRect clip_rect);

static bool decode_rect(ID obj, VgClipRect *out_rect) {
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
    if (obj_tag == CLJ_RECORD && record_type_is(obj, "Rect")) {
        assert_record_field_count_at_least(obj, 8, "Rect");
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
    assert_record_field_count_at_least(node_obj, 6, "Polyline");
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

    ID local_t_obj = NULL;
    if (record_type_is(node_obj, "Group")) {
        assert_record_field_count_at_least(node_obj, 5, "Group");
        Group *r = node_obj;
        local_t_obj = r->t;
    } else if (record_type_is(node_obj, "Line")) {
        assert_record_field_count_at_least(node_obj, 8, "Line");
        Line *r = node_obj;
        local_t_obj = r->t;
    } else if (record_type_is(node_obj, "Polyline")) {
        assert_record_field_count_at_least(node_obj, 6, "Polyline");
        Polyline *r = node_obj;
        local_t_obj = r->t;
    } else if (record_type_is(node_obj, "Rect")) {
        assert_record_field_count_at_least(node_obj, 8, "Rect");
        Rect *r = node_obj;
        local_t_obj = r->t;
    } else if (record_type_is(node_obj, "Tri")) {
        assert_record_field_count_at_least(node_obj, 10, "Tri");
        Tri *r = node_obj;
        local_t_obj = r->t;
    } else if (record_type_is(node_obj, "VText")) {
        assert_record_field_count_at_least(node_obj, 9, "VText");
        VText *r = node_obj;
        local_t_obj = r->t;
    }
    VgTransformFixed world_t = parent_t;
    if (local_t_obj) {
        VgTransformFixed local_t = decode_transform_fixed(local_t_obj);
        world_t = vg_transform_fixed_compose(parent_t, local_t);
    }
    VgStyle style = decode_style(node_obj);
    if (!style.visible) {
        return true;
    }

    if (record_type_is(node_obj, "Group")) {
        assert_record_field_count_at_least(node_obj, 5, "Group");
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

    VgNode temp;
    memset(&temp, 0, sizeof(temp));
    temp.has_transform = false;
    temp.style = style;

    if (record_type_is(node_obj, "Line")) {
        temp.type = VG_NODE_LINE;
        assert_record_field_count_at_least(node_obj, 8, "Line");
        Line *line = node_obj;
        temp.data.line.x1 = id_to_i16_default(line->x1, 0);
        temp.data.line.y1 = id_to_i16_default(line->y1, 0);
        temp.data.line.x2 = id_to_i16_default(line->x2, 0);
        temp.data.line.y2 = id_to_i16_default(line->y2, 0);
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (record_type_is(node_obj, "Rect")) {
        temp.type = VG_NODE_RECT;
        assert_record_field_count_at_least(node_obj, 8, "Rect");
        Rect *rect = node_obj;
        temp.data.rect.x = id_to_i16_default(rect->x, 0);
        temp.data.rect.y = id_to_i16_default(rect->y, 0);
        temp.data.rect.w = id_to_i16_default(rect->w, 0);
        temp.data.rect.h = id_to_i16_default(rect->h, 0);
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (record_type_is(node_obj, "Tri")) {
        temp.type = VG_NODE_TRI;
        assert_record_field_count_at_least(node_obj, 10, "Tri");
        Tri *tri = node_obj;
        temp.data.tri.x1 = id_to_i16_default(tri->x1, 0);
        temp.data.tri.y1 = id_to_i16_default(tri->y1, 0);
        temp.data.tri.x2 = id_to_i16_default(tri->x2, 0);
        temp.data.tri.y2 = id_to_i16_default(tri->y2, 0);
        temp.data.tri.x3 = id_to_i16_default(tri->x3, 0);
        temp.data.tri.y3 = id_to_i16_default(tri->y3, 0);
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (record_type_is(node_obj, "VText")) {
        temp.type = VG_NODE_VTEXT;
        assert_record_field_count_at_least(node_obj, 9, "VText");
        VText *text = node_obj;
        temp.data.text.x = id_to_i16_default(text->x, 0);
        temp.data.text.y = id_to_i16_default(text->y, 0);
        temp.data.text.scale = id_to_fixed_raw_default(text->scale, VG_SCALE_ONE);
        temp.data.text.rot_deg = id_to_i16_default(text->rot, 0);
        temp.data.text.text = id_to_text_cstr(text->text);
        return render_one_temp_node(&temp, world_t, fb, use_clip, clip_rect);
    }
    if (record_type_is(node_obj, "Polyline")) {
        return render_polyline_record(node_obj, world_t, style, fb, use_clip, clip_rect);
    }
    return false;
}

static bool decode_scene_fields(ID scene_record, ID *out_root, ID *out_clip, ID *out_erase) {
    if (record_type_is(scene_record, "FrameScene")) {
        assert_record_field_count_at_least(scene_record, 7, "FrameScene");
        FrameScene *fs = scene_record;
        *out_root = fs->root;
        *out_clip = fs->clip_rect;
        if (out_erase) *out_erase = fs->erase_rgb565;
        return true;
    }
    if (record_type_is(scene_record, "Scene")) {
        assert_record_field_count_at_least(scene_record, 3, "Scene");
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

    VgClipRect effective_rect = {0, 0, 0, 0};
    bool has_effective_rect = decode_rect(clip_source, &effective_rect);

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

    VgClipRect effective_clip = clip_rect;
    VgClipRect scene_clip = {0, 0, 0, 0};
    bool has_scene_clip = decode_rect(clip_source, &scene_clip);
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
    if (!record_type_is(frame_scene_record, "FrameScene")) {
        return false;
    }

    assert_record_field_count_at_least(frame_scene_record, 7, "FrameScene");
    FrameScene *scene = frame_scene_record;
    VgClipRect clip = {0, 0, 0, 0};
    if (!decode_rect(scene->clip_rect, &clip)) {
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
