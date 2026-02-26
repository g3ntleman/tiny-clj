#include "vector_scene_graph_records.h"
#include "gfx.h"

#include <string.h>

#include "map.h"
#include "record.h"
#include "strings.h"
#include "symbol.h"
#include "value.h"
#include "vector.h"

typedef struct {
    ID k_root;
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
    ID k_tx;
    ID k_ty;
    ID k_sx;
    ID k_sy;
    ID k_stroke_rgb565;
    ID k_stroke_width;
    ID k_has_fill;
    ID k_fill_rgb565;
    ID k_has_bg_rgb565;
    ID k_bg_rgb565;
    ID k_clip_rect;
    ID k_erase_rgb565;
    ID k_z;
    ID k_opaque;
    ID k_guard_px;
} VgRecordKeys;

static VgRecordKeys vg_record_keys(void) {
    VgRecordKeys k;
    k.k_root = intern_symbol_global(":root");
    k.k_t = intern_symbol_global(":t");
    k.k_style = intern_symbol_global(":style");
    k.k_visible = intern_symbol_global(":visible");
    k.k_children = intern_symbol_global(":children");
    k.k_x1 = intern_symbol_global(":x1");
    k.k_y1 = intern_symbol_global(":y1");
    k.k_x2 = intern_symbol_global(":x2");
    k.k_y2 = intern_symbol_global(":y2");
    k.k_x3 = intern_symbol_global(":x3");
    k.k_y3 = intern_symbol_global(":y3");
    k.k_x = intern_symbol_global(":x");
    k.k_y = intern_symbol_global(":y");
    k.k_w = intern_symbol_global(":w");
    k.k_h = intern_symbol_global(":h");
    k.k_pts = intern_symbol_global(":pts");
    k.k_closed = intern_symbol_global(":closed");
    k.k_scale = intern_symbol_global(":scale");
    k.k_rot = intern_symbol_global(":rot");
    k.k_text = intern_symbol_global(":text");
    k.k_tx = intern_symbol_global(":tx");
    k.k_ty = intern_symbol_global(":ty");
    k.k_sx = intern_symbol_global(":sx");
    k.k_sy = intern_symbol_global(":sy");
    k.k_stroke_rgb565 = intern_symbol_global(":stroke_rgb565");
    k.k_stroke_width = intern_symbol_global(":stroke_width");
    k.k_has_fill = intern_symbol_global(":has_fill");
    k.k_fill_rgb565 = intern_symbol_global(":fill_rgb565");
    k.k_has_bg_rgb565 = intern_symbol_global(":has_bg_rgb565");
    k.k_bg_rgb565 = intern_symbol_global(":bg_rgb565");
    k.k_clip_rect = intern_symbol_global(":clip-rect");
    k.k_erase_rgb565 = intern_symbol_global(":erase-rgb565");
    k.k_z = intern_symbol_global(":z");
    k.k_opaque = intern_symbol_global(":opaque");
    k.k_guard_px = intern_symbol_global(":guard-px");
    return k;
}

static ID get_field(ID obj, ID key, ID not_found) {
    if (!obj || !key) {
        return not_found;
    }
    CljType tag = TAG(obj);
    if (tag == CLJ_RECORD) {
        return record_get_sentinel(obj, key, not_found);
    }
    if (tag == CLJ_MAP_PERSISTENT || tag == CLJ_MAP_TRANSIENT) {
        return map_get_sentinel(obj, key, not_found);
    }
    return not_found;
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
        return (int32_t)((intptr_t)v >> TAG_BITS);
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
        return (int16_t)as_fixed(v);
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
        return (uint16_t)as_fixed(v);
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
        return (uint8_t)as_fixed(v);
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

static VgTransform decode_transform(ID obj, const VgRecordKeys *k) {
    VgTransform t = vg_transform_identity();
    if (!obj || !k) {
        return t;
    }
    t.tx = id_to_i16_default(get_field(obj, k->k_tx, NULL), 0);
    t.ty = id_to_i16_default(get_field(obj, k->k_ty, NULL), 0);
    t.sx = id_to_fixed_raw_default(get_field(obj, k->k_sx, NULL), VG_SCALE_ONE);
    t.sy = id_to_fixed_raw_default(get_field(obj, k->k_sy, NULL), VG_SCALE_ONE);
    t.rot_deg = id_to_i16_default(get_field(obj, k->k_rot, NULL), 0);
    return t;
}

static VgStyle decode_style(ID node_obj, const VgRecordKeys *k) {
    VgStyle s = vg_style_default();
    if (!node_obj || !k) {
        return s;
    }
    ID style_obj = get_field(node_obj, k->k_style, NULL);
    if (style_obj) {
        s.stroke_rgb565 = id_to_u16_default(get_field(style_obj, k->k_stroke_rgb565, NULL), s.stroke_rgb565);
        s.stroke_width = id_to_u8_default(get_field(style_obj, k->k_stroke_width, NULL), s.stroke_width);
        s.has_fill = id_to_bool_default(get_field(style_obj, k->k_has_fill, NULL), s.has_fill);
        s.fill_rgb565 = id_to_u16_default(get_field(style_obj, k->k_fill_rgb565, NULL), s.fill_rgb565);
        s.has_bg_rgb565 = id_to_bool_default(get_field(style_obj, k->k_has_bg_rgb565, NULL), s.has_bg_rgb565);
        s.bg_rgb565 = id_to_u16_default(get_field(style_obj, k->k_bg_rgb565, NULL), s.bg_rgb565);
        s.visible = id_to_bool_default(get_field(style_obj, k->k_visible, NULL), s.visible);
    }
    ID node_visible = get_field(node_obj, k->k_visible, NULL);
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
                               VgTransform parent_t,
                               VgFrameBuffer *fb,
                               const VgRecordKeys *k,
                               bool use_clip,
                               VgClipRect clip_rect);

static bool decode_rect(ID obj, const VgRecordKeys *k, VgClipRect *out_rect) {
    if (!obj || !k || !out_rect) {
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
    if (obj_tag == CLJ_RECORD || obj_tag == CLJ_MAP_PERSISTENT || obj_tag == CLJ_MAP_TRANSIENT) {
        out_rect->x = id_to_i16_default(get_field(obj, k->k_x, NULL), 0);
        out_rect->y = id_to_i16_default(get_field(obj, k->k_y, NULL), 0);
        out_rect->w = id_to_i16_default(get_field(obj, k->k_w, NULL), 0);
        out_rect->h = id_to_i16_default(get_field(obj, k->k_h, NULL), 0);
        return !vg_clip_rect_is_empty(*out_rect);
    }
    return false;
}

static bool is_scene_record_type(ID obj) {
    return record_type_is(obj, "Scene") || record_type_is(obj, "FrameScene");
}


static bool render_one_temp_node(const VgNode *node, VgFrameBuffer *fb, bool use_clip, VgClipRect clip_rect) {
    if (!node || !fb) {
        return false;
    }
    if (use_clip) {
        vg_render_scene_clipped(node, fb, clip_rect);
    } else {
        vg_render_scene(node, fb);
    }
    return true;
}

static bool render_polyline_record(ID node_obj,
                                   VgTransform world_t,
                                   VgStyle style,
                                   VgFrameBuffer *fb,
                                   const VgRecordKeys *k,
                                   bool use_clip,
                                   VgClipRect clip_rect) {
    ID pts = get_field(node_obj, k->k_pts, NULL);
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
    temp.has_transform = true;
    temp.transform = world_t;
    temp.style = style;
    temp.data.polyline.points = points;
    temp.data.polyline.point_count = (size_t)n;
    temp.data.polyline.closed = id_to_bool_default(get_field(node_obj, k->k_closed, NULL), false);
    return render_one_temp_node(&temp, fb, use_clip, clip_rect);
}

static bool render_record_node(ID node_obj,
                               VgTransform parent_t,
                               VgFrameBuffer *fb,
                               const VgRecordKeys *k,
                               bool use_clip,
                               VgClipRect clip_rect) {
    if (!node_obj || TAG(node_obj) != CLJ_RECORD || !fb || !k) {
        return false;
    }

    ID local_t_obj = get_field(node_obj, k->k_t, NULL);
    VgTransform world_t = parent_t;
    if (local_t_obj) {
        VgTransform local_t = decode_transform(local_t_obj, k);
        world_t = vg_transform_compose(parent_t, local_t);
    }
    VgStyle style = decode_style(node_obj, k);
    if (!style.visible) {
        return true;
    }

    if (record_type_is(node_obj, "Group")) {
        ID children = get_field(node_obj, k->k_children, NULL);
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
            if (!render_record_node(child, world_t, fb, k, use_clip, clip_rect)) {
                return false;
            }
        }
        return true;
    }

    VgNode temp;
    memset(&temp, 0, sizeof(temp));
    temp.has_transform = true;
    temp.transform = world_t;
    temp.style = style;

    if (record_type_is(node_obj, "Line")) {
        temp.type = VG_NODE_LINE;
        temp.data.line.x1 = id_to_i16_default(get_field(node_obj, k->k_x1, NULL), 0);
        temp.data.line.y1 = id_to_i16_default(get_field(node_obj, k->k_y1, NULL), 0);
        temp.data.line.x2 = id_to_i16_default(get_field(node_obj, k->k_x2, NULL), 0);
        temp.data.line.y2 = id_to_i16_default(get_field(node_obj, k->k_y2, NULL), 0);
        return render_one_temp_node(&temp, fb, use_clip, clip_rect);
    }
    if (record_type_is(node_obj, "Rect")) {
        temp.type = VG_NODE_RECT;
        temp.data.rect.x = id_to_i16_default(get_field(node_obj, k->k_x, NULL), 0);
        temp.data.rect.y = id_to_i16_default(get_field(node_obj, k->k_y, NULL), 0);
        temp.data.rect.w = id_to_i16_default(get_field(node_obj, k->k_w, NULL), 0);
        temp.data.rect.h = id_to_i16_default(get_field(node_obj, k->k_h, NULL), 0);
        return render_one_temp_node(&temp, fb, use_clip, clip_rect);
    }
    if (record_type_is(node_obj, "Tri")) {
        temp.type = VG_NODE_TRI;
        temp.data.tri.x1 = id_to_i16_default(get_field(node_obj, k->k_x1, NULL), 0);
        temp.data.tri.y1 = id_to_i16_default(get_field(node_obj, k->k_y1, NULL), 0);
        temp.data.tri.x2 = id_to_i16_default(get_field(node_obj, k->k_x2, NULL), 0);
        temp.data.tri.y2 = id_to_i16_default(get_field(node_obj, k->k_y2, NULL), 0);
        temp.data.tri.x3 = id_to_i16_default(get_field(node_obj, k->k_x3, NULL), 0);
        temp.data.tri.y3 = id_to_i16_default(get_field(node_obj, k->k_y3, NULL), 0);
        return render_one_temp_node(&temp, fb, use_clip, clip_rect);
    }
    if (record_type_is(node_obj, "VText")) {
        temp.type = VG_NODE_VTEXT;
        temp.data.text.x = id_to_i16_default(get_field(node_obj, k->k_x, NULL), 0);
        temp.data.text.y = id_to_i16_default(get_field(node_obj, k->k_y, NULL), 0);
        temp.data.text.scale = id_to_fixed_raw_default(get_field(node_obj, k->k_scale, NULL), VG_SCALE_ONE);
        temp.data.text.rot_deg = id_to_i16_default(get_field(node_obj, k->k_rot, NULL), 0);
        temp.data.text.text = id_to_text_cstr(get_field(node_obj, k->k_text, NULL));
        return render_one_temp_node(&temp, fb, use_clip, clip_rect);
    }
    if (record_type_is(node_obj, "Polyline")) {
        return render_polyline_record(node_obj, world_t, style, fb, k, use_clip, clip_rect);
    }
    return false;
}

bool vg_render_scene_record(ID scene_record, VgFrameBuffer *fb) {
    if (!scene_record || !fb || TAG(scene_record) != CLJ_RECORD) {
        return false;
    }
    VgRecordKeys keys = vg_record_keys();
    if (!is_scene_record_type(scene_record)) {
        return false;
    }

    ID root = get_field(scene_record, keys.k_root, NULL);

    VgClipRect effective_rect = {0, 0, 0, 0};
    bool has_effective_rect = decode_rect(get_field(scene_record, keys.k_clip_rect, NULL), &keys, &effective_rect);

    if (has_effective_rect) {
        uint16_t erase_rgb565 = id_to_u16_default(get_field(scene_record, keys.k_erase_rgb565, NULL), 0x0000u);
        vg_framebuffer_clear_rect(fb, effective_rect, erase_rgb565);
    }

    if (!root) {
        return true;
    }
    return render_record_node(root, vg_transform_identity(), fb, &keys, has_effective_rect, effective_rect);
}

bool vg_render_scene_record_clipped(ID scene_record, VgFrameBuffer *fb, VgClipRect clip_rect) {
    if (!scene_record || !fb || TAG(scene_record) != CLJ_RECORD) {
        return false;
    }
    VgRecordKeys keys = vg_record_keys();
    if (!is_scene_record_type(scene_record)) {
        return false;
    }

    ID root = get_field(scene_record, keys.k_root, NULL);
    if (!root) {
        return true;
    }

    VgClipRect effective_clip = clip_rect;
    VgClipRect scene_clip = {0, 0, 0, 0};
    bool has_scene_clip = decode_rect(get_field(scene_record, keys.k_clip_rect, NULL), &keys, &scene_clip);
    if (has_scene_clip) {
        if (!vg_clip_rect_intersect(clip_rect, scene_clip, &effective_clip)) {
            return true;
        }
    }
    return render_record_node(root, vg_transform_identity(), fb, &keys, true, effective_clip);
}

bool vg_decode_frame_slot_record(ID frame_scene_record, VgRenderSlot *out_slot) {
    if (!frame_scene_record || !out_slot || TAG(frame_scene_record) != CLJ_RECORD) {
        return false;
    }
    VgRecordKeys keys = vg_record_keys();
    if (!is_scene_record_type(frame_scene_record)) {
        return false;
    }

    ID root = get_field(frame_scene_record, keys.k_root, NULL);
    VgClipRect clip = {0, 0, 0, 0};
    if (!decode_rect(get_field(frame_scene_record, keys.k_clip_rect, NULL), &keys, &clip)) {
        return false;
    }

    out_slot->root = root;
    out_slot->clip_rect = clip;
    out_slot->z = id_to_i16_default(get_field(frame_scene_record, keys.k_z, NULL), 0);
    out_slot->visible = id_to_bool_default(get_field(frame_scene_record, keys.k_visible, NULL), true);
    out_slot->opaque = id_to_bool_default(get_field(frame_scene_record, keys.k_opaque, NULL), true);
    out_slot->clear_rgb565 = id_to_u16_default(get_field(frame_scene_record, keys.k_erase_rgb565, NULL), 0x0000u);
    out_slot->guard_px = id_to_u8_default(get_field(frame_scene_record, keys.k_guard_px, NULL), 0);
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
