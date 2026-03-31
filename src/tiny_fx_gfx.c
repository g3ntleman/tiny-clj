#include "tiny_fx_gfx.h"

#include "builtins.h"
#include "callbacks.h"
#include "memory.h"
#include "symbol.h"
#include "vector.h"

static VgRecordSchema g_record_schema = {0};
static VgRecordKeys g_record_keys = {0};
static bool g_record_keys_initialized = false;
static bool g_record_parse_symbols_registered = false;

// Static symbol payloads (like other module-local symbol singletons).
#define STATIC_SYMBOL_DATA(name, cname_literal) \
    static StaticSymbolData name = { \
        .sym = { \
            .base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = 0}, \
            .ns_name = NULL, \
            .unqualified = NULL, \
            .cname = cname_literal \
        } \
    }

STATIC_SYMBOL_DATA(sym_kw_root_data, ":root");
STATIC_SYMBOL_DATA(sym_kw_index_data, ":index");
STATIC_SYMBOL_DATA(sym_kw_id_data, ":id");
STATIC_SYMBOL_DATA(sym_kw_t_data, ":t");
STATIC_SYMBOL_DATA(sym_kw_style_data, ":style");
STATIC_SYMBOL_DATA(sym_kw_visible_data, ":visible");
STATIC_SYMBOL_DATA(sym_kw_children_data, ":children");
STATIC_SYMBOL_DATA(sym_kw_x1_data, ":x1");
STATIC_SYMBOL_DATA(sym_kw_y1_data, ":y1");
STATIC_SYMBOL_DATA(sym_kw_x2_data, ":x2");
STATIC_SYMBOL_DATA(sym_kw_y2_data, ":y2");
STATIC_SYMBOL_DATA(sym_kw_x3_data, ":x3");
STATIC_SYMBOL_DATA(sym_kw_y3_data, ":y3");
STATIC_SYMBOL_DATA(sym_kw_x_data, ":x");
STATIC_SYMBOL_DATA(sym_kw_y_data, ":y");
STATIC_SYMBOL_DATA(sym_kw_w_data, ":w");
STATIC_SYMBOL_DATA(sym_kw_h_data, ":h");
STATIC_SYMBOL_DATA(sym_kw_pts_data, ":pts");
STATIC_SYMBOL_DATA(sym_kw_closed_data, ":closed");
STATIC_SYMBOL_DATA(sym_kw_scale_data, ":scale");
STATIC_SYMBOL_DATA(sym_kw_rot_data, ":rot");
STATIC_SYMBOL_DATA(sym_kw_text_data, ":text");
STATIC_SYMBOL_DATA(sym_kw_keyframes_data, ":keyframes");
STATIC_SYMBOL_DATA(sym_kw_loop_data, ":loop");
STATIC_SYMBOL_DATA(sym_kw_end_event_data, ":end-event");
STATIC_SYMBOL_DATA(sym_kw_event_id_data, ":event-id");
STATIC_SYMBOL_DATA(sym_kw_tx_data, ":tx");
STATIC_SYMBOL_DATA(sym_kw_ty_data, ":ty");
STATIC_SYMBOL_DATA(sym_kw_sx_data, ":sx");
STATIC_SYMBOL_DATA(sym_kw_sy_data, ":sy");
STATIC_SYMBOL_DATA(sym_kw_stroke_color_data, ":stroke-color");
STATIC_SYMBOL_DATA(sym_kw_stroke_width_data, ":stroke-width");
STATIC_SYMBOL_DATA(sym_kw_has_fill_data, ":has-fill");
STATIC_SYMBOL_DATA(sym_kw_fill_color_data, ":fill-color");
STATIC_SYMBOL_DATA(sym_kw_has_bg_color_data, ":has-bg-color");
STATIC_SYMBOL_DATA(sym_kw_bg_color_data, ":bg-color");
STATIC_SYMBOL_DATA(sym_kw_clip_rect_data, ":clip-rect");
STATIC_SYMBOL_DATA(sym_kw_erase_color_data, ":erase-color");
STATIC_SYMBOL_DATA(sym_kw_z_data, ":z");
STATIC_SYMBOL_DATA(sym_kw_opaque_data, ":opaque");
STATIC_SYMBOL_DATA(sym_kw_guard_px_data, ":guard-px");

STATIC_SYMBOL_DATA(sym_type_transform_data, "Transform");
STATIC_SYMBOL_DATA(sym_type_style_data, "Style");
STATIC_SYMBOL_DATA(sym_type_group_data, "Group");
STATIC_SYMBOL_DATA(sym_type_line_data, "Line");
STATIC_SYMBOL_DATA(sym_type_polyline_data, "Polyline");
STATIC_SYMBOL_DATA(sym_type_rect_data, "Rect");
STATIC_SYMBOL_DATA(sym_type_tri_data, "Tri");
STATIC_SYMBOL_DATA(sym_type_vtext_data, "VText");
STATIC_SYMBOL_DATA(sym_type_timeline_data, "Timeline");
STATIC_SYMBOL_DATA(sym_type_frame_scene_data, "FrameScene");
STATIC_SYMBOL_DATA(sym_type_scene_data, "Scene");
STATIC_SYMBOL_DATA(sym_type_collision_rule_data, "CollisionRule");
STATIC_SYMBOL_DATA(sym_type_collision_event_data, "CollisionEvent");
STATIC_SYMBOL_DATA(sym_type_spatial_rule_data, "SpatialRule");
STATIC_SYMBOL_DATA(sym_type_aabb_data, "Aabb");
STATIC_SYMBOL_DATA(sym_type_spatial_event_data, "SpatialEvent");

STATIC_SYMBOL_DATA(sym_ctor_transform_data, "->Transform");
STATIC_SYMBOL_DATA(sym_ctor_style_data, "->Style");
STATIC_SYMBOL_DATA(sym_ctor_group_data, "->Group");
STATIC_SYMBOL_DATA(sym_ctor_line_data, "->Line");
STATIC_SYMBOL_DATA(sym_ctor_polyline_data, "->Polyline");
STATIC_SYMBOL_DATA(sym_ctor_rect_data, "->Rect");
STATIC_SYMBOL_DATA(sym_ctor_tri_data, "->Tri");
STATIC_SYMBOL_DATA(sym_ctor_vtext_data, "->VText");
STATIC_SYMBOL_DATA(sym_ctor_timeline_data, "->Timeline");
STATIC_SYMBOL_DATA(sym_ctor_frame_scene_data, "->FrameScene");
STATIC_SYMBOL_DATA(sym_ctor_scene_data, "->Scene");
STATIC_SYMBOL_DATA(sym_ctor_collision_rule_data, "->CollisionRule");
STATIC_SYMBOL_DATA(sym_ctor_collision_event_data, "->CollisionEvent");
STATIC_SYMBOL_DATA(sym_ctor_spatial_rule_data, "->SpatialRule");
STATIC_SYMBOL_DATA(sym_ctor_aabb_data, "->Aabb");
STATIC_SYMBOL_DATA(sym_ctor_spatial_event_data, "->SpatialEvent");

STATIC_SYMBOL_DATA(sym_map_ctor_transform_data, "map->Transform");
STATIC_SYMBOL_DATA(sym_map_ctor_style_data, "map->Style");
STATIC_SYMBOL_DATA(sym_map_ctor_group_data, "map->Group");
STATIC_SYMBOL_DATA(sym_map_ctor_line_data, "map->Line");
STATIC_SYMBOL_DATA(sym_map_ctor_polyline_data, "map->Polyline");
STATIC_SYMBOL_DATA(sym_map_ctor_rect_data, "map->Rect");
STATIC_SYMBOL_DATA(sym_map_ctor_tri_data, "map->Tri");
STATIC_SYMBOL_DATA(sym_map_ctor_vtext_data, "map->VText");
STATIC_SYMBOL_DATA(sym_map_ctor_timeline_data, "map->Timeline");
STATIC_SYMBOL_DATA(sym_map_ctor_frame_scene_data, "map->FrameScene");
STATIC_SYMBOL_DATA(sym_map_ctor_scene_data, "map->Scene");
STATIC_SYMBOL_DATA(sym_map_ctor_collision_rule_data, "map->CollisionRule");
STATIC_SYMBOL_DATA(sym_map_ctor_collision_event_data, "map->CollisionEvent");
STATIC_SYMBOL_DATA(sym_map_ctor_spatial_rule_data, "map->SpatialRule");
STATIC_SYMBOL_DATA(sym_map_ctor_aabb_data, "map->Aabb");
STATIC_SYMBOL_DATA(sym_map_ctor_spatial_event_data, "map->SpatialEvent");

STATIC_SYMBOL_DATA(sym_field_tx_data, "tx");
STATIC_SYMBOL_DATA(sym_field_ty_data, "ty");
STATIC_SYMBOL_DATA(sym_field_sx_data, "sx");
STATIC_SYMBOL_DATA(sym_field_sy_data, "sy");
STATIC_SYMBOL_DATA(sym_field_rot_data, "rot");
STATIC_SYMBOL_DATA(sym_field_stroke_color_data, "stroke-color");
STATIC_SYMBOL_DATA(sym_field_stroke_width_data, "stroke-width");
STATIC_SYMBOL_DATA(sym_field_visible_data, "visible");
STATIC_SYMBOL_DATA(sym_field_has_fill_data, "has-fill");
STATIC_SYMBOL_DATA(sym_field_fill_color_data, "fill-color");
STATIC_SYMBOL_DATA(sym_field_has_bg_color_data, "has-bg-color");
STATIC_SYMBOL_DATA(sym_field_bg_color_data, "bg-color");
STATIC_SYMBOL_DATA(sym_field_id_data, "id");
STATIC_SYMBOL_DATA(sym_field_t_data, "t");
STATIC_SYMBOL_DATA(sym_field_style_data, "style");
STATIC_SYMBOL_DATA(sym_field_children_data, "children");
STATIC_SYMBOL_DATA(sym_field_prototype_data, "prototype");
STATIC_SYMBOL_DATA(sym_field_x1_data, "x1");
STATIC_SYMBOL_DATA(sym_field_y1_data, "y1");
STATIC_SYMBOL_DATA(sym_field_x2_data, "x2");
STATIC_SYMBOL_DATA(sym_field_y2_data, "y2");
STATIC_SYMBOL_DATA(sym_field_pts_data, "pts");
STATIC_SYMBOL_DATA(sym_field_closed_data, "closed");
STATIC_SYMBOL_DATA(sym_field_x_data, "x");
STATIC_SYMBOL_DATA(sym_field_y_data, "y");
STATIC_SYMBOL_DATA(sym_field_w_data, "w");
STATIC_SYMBOL_DATA(sym_field_h_data, "h");
STATIC_SYMBOL_DATA(sym_field_text_data, "text");
STATIC_SYMBOL_DATA(sym_field_keyframes_data, "keyframes");
STATIC_SYMBOL_DATA(sym_field_loop_data, "loop");
STATIC_SYMBOL_DATA(sym_field_end_event_data, "end-event");
STATIC_SYMBOL_DATA(sym_field_event_id_data, "event-id");
STATIC_SYMBOL_DATA(sym_field_root_data, "root");
STATIC_SYMBOL_DATA(sym_field_index_data, "index");
STATIC_SYMBOL_DATA(sym_field_clip_rect_data, "clip-rect");
STATIC_SYMBOL_DATA(sym_field_erase_color_data, "erase-color");
STATIC_SYMBOL_DATA(sym_field_collision_rules_data, "collision-rules");
STATIC_SYMBOL_DATA(sym_field_z_data, "z");
STATIC_SYMBOL_DATA(sym_field_opaque_data, "opaque");
STATIC_SYMBOL_DATA(sym_field_guard_px_data, "guard-px");
STATIC_SYMBOL_DATA(sym_field_slot_data, "slot");
STATIC_SYMBOL_DATA(sym_field_a_id_data, "a-id");
STATIC_SYMBOL_DATA(sym_field_b_id_data, "b-id");
STATIC_SYMBOL_DATA(sym_field_phase_mask_data, "phase-mask");
STATIC_SYMBOL_DATA(sym_field_enabled_data, "enabled");
STATIC_SYMBOL_DATA(sym_field_cooldown_ms_data, "cooldown-ms");
STATIC_SYMBOL_DATA(sym_field_rule_id_data, "rule-id");
STATIC_SYMBOL_DATA(sym_field_phase_data, "phase");
STATIC_SYMBOL_DATA(sym_field_snapshot_gen_data, "snapshot-gen");
STATIC_SYMBOL_DATA(sym_field_ts_ms_data, "ts-ms");
STATIC_SYMBOL_DATA(sym_field_kind_data, "kind");
STATIC_SYMBOL_DATA(sym_field_self_data, "self");
STATIC_SYMBOL_DATA(sym_field_other_data, "other");
STATIC_SYMBOL_DATA(sym_field_radius_data, "radius");
STATIC_SYMBOL_DATA(sym_field_channel_data, "channel");
STATIC_SYMBOL_DATA(sym_field_min_x_data, "min-x");
STATIC_SYMBOL_DATA(sym_field_min_y_data, "min-y");
STATIC_SYMBOL_DATA(sym_field_max_x_data, "max-x");
STATIC_SYMBOL_DATA(sym_field_max_y_data, "max-y");
STATIC_SYMBOL_DATA(sym_field_source_data, "source");
STATIC_SYMBOL_DATA(sym_field_slot_id_data, "slot-id");
STATIC_SYMBOL_DATA(sym_field_self_entity_data, "self-entity");
STATIC_SYMBOL_DATA(sym_field_other_entity_data, "other-entity");
STATIC_SYMBOL_DATA(sym_field_rule_data, "rule");
STATIC_SYMBOL_DATA(sym_field_self_aabb_data, "self-aabb");
STATIC_SYMBOL_DATA(sym_field_other_aabb_data, "other-aabb");
STATIC_SYMBOL_DATA(sym_field_self_prototype_data, "self-prototype");
STATIC_SYMBOL_DATA(sym_field_other_prototype_data, "other-prototype");

#undef STATIC_SYMBOL_DATA

static void tiny_fx_gfx_register_record_parse_symbols(void) {
    if (g_record_parse_symbols_registered) {
        return;
    }

    CljSymbol *symbols[] = {
        &sym_type_transform_data.sym,
        &sym_type_style_data.sym,
        &sym_type_group_data.sym,
        &sym_type_line_data.sym,
        &sym_type_polyline_data.sym,
        &sym_type_rect_data.sym,
        &sym_type_tri_data.sym,
        &sym_type_vtext_data.sym,
        &sym_type_timeline_data.sym,
        &sym_type_frame_scene_data.sym,
        &sym_type_scene_data.sym,
        &sym_type_collision_rule_data.sym,
        &sym_type_collision_event_data.sym,
        &sym_type_spatial_rule_data.sym,
        &sym_type_aabb_data.sym,
        &sym_type_spatial_event_data.sym,
        &sym_ctor_transform_data.sym,
        &sym_ctor_style_data.sym,
        &sym_ctor_group_data.sym,
        &sym_ctor_line_data.sym,
        &sym_ctor_polyline_data.sym,
        &sym_ctor_rect_data.sym,
        &sym_ctor_tri_data.sym,
        &sym_ctor_vtext_data.sym,
        &sym_ctor_timeline_data.sym,
        &sym_ctor_frame_scene_data.sym,
        &sym_ctor_scene_data.sym,
        &sym_ctor_collision_rule_data.sym,
        &sym_ctor_collision_event_data.sym,
        &sym_ctor_spatial_rule_data.sym,
        &sym_ctor_aabb_data.sym,
        &sym_ctor_spatial_event_data.sym,
        &sym_map_ctor_transform_data.sym,
        &sym_map_ctor_style_data.sym,
        &sym_map_ctor_group_data.sym,
        &sym_map_ctor_line_data.sym,
        &sym_map_ctor_polyline_data.sym,
        &sym_map_ctor_rect_data.sym,
        &sym_map_ctor_tri_data.sym,
        &sym_map_ctor_vtext_data.sym,
        &sym_map_ctor_timeline_data.sym,
        &sym_map_ctor_frame_scene_data.sym,
        &sym_map_ctor_scene_data.sym,
        &sym_map_ctor_collision_rule_data.sym,
        &sym_map_ctor_collision_event_data.sym,
        &sym_map_ctor_spatial_rule_data.sym,
        &sym_map_ctor_aabb_data.sym,
        &sym_map_ctor_spatial_event_data.sym,
        &sym_field_tx_data.sym,
        &sym_field_ty_data.sym,
        &sym_field_sx_data.sym,
        &sym_field_sy_data.sym,
        &sym_field_rot_data.sym,
        &sym_field_stroke_color_data.sym,
        &sym_field_stroke_width_data.sym,
        &sym_field_visible_data.sym,
        &sym_field_has_fill_data.sym,
        &sym_field_fill_color_data.sym,
        &sym_field_has_bg_color_data.sym,
        &sym_field_bg_color_data.sym,
        &sym_field_id_data.sym,
        &sym_field_t_data.sym,
        &sym_field_style_data.sym,
        &sym_field_children_data.sym,
        &sym_field_prototype_data.sym,
        &sym_field_x1_data.sym,
        &sym_field_y1_data.sym,
        &sym_field_x2_data.sym,
        &sym_field_y2_data.sym,
        &sym_field_pts_data.sym,
        &sym_field_closed_data.sym,
        &sym_field_x_data.sym,
        &sym_field_y_data.sym,
        &sym_field_w_data.sym,
        &sym_field_h_data.sym,
        &sym_field_text_data.sym,
        &sym_field_keyframes_data.sym,
        &sym_field_loop_data.sym,
        &sym_field_end_event_data.sym,
        &sym_field_event_id_data.sym,
        &sym_field_root_data.sym,
        &sym_field_index_data.sym,
        &sym_field_clip_rect_data.sym,
        &sym_field_erase_color_data.sym,
        &sym_field_collision_rules_data.sym,
        &sym_field_z_data.sym,
        &sym_field_opaque_data.sym,
        &sym_field_guard_px_data.sym,
        &sym_field_slot_data.sym,
        &sym_field_a_id_data.sym,
        &sym_field_b_id_data.sym,
        &sym_field_phase_mask_data.sym,
        &sym_field_enabled_data.sym,
        &sym_field_cooldown_ms_data.sym,
        &sym_field_rule_id_data.sym,
        &sym_field_phase_data.sym,
        &sym_field_snapshot_gen_data.sym,
        &sym_field_ts_ms_data.sym,
        &sym_field_kind_data.sym,
        &sym_field_self_data.sym,
        &sym_field_other_data.sym,
        &sym_field_radius_data.sym,
        &sym_field_channel_data.sym,
        &sym_field_min_x_data.sym,
        &sym_field_min_y_data.sym,
        &sym_field_max_x_data.sym,
        &sym_field_max_y_data.sym,
        &sym_field_source_data.sym,
        &sym_field_slot_id_data.sym,
        &sym_field_self_entity_data.sym,
        &sym_field_other_entity_data.sym,
        &sym_field_rule_data.sym,
        &sym_field_self_aabb_data.sym,
        &sym_field_other_aabb_data.sym,
        &sym_field_self_prototype_data.sym,
        &sym_field_other_prototype_data.sym,
    };

    for (size_t i = 0; i < (sizeof(symbols) / sizeof(symbols[0])); i++) {
        symbol_table_add(symbols[i]);
    }
    g_record_parse_symbols_registered = true;
}

static int descriptor_index_of(CljRecordDescriptor *desc, ID key) {
    if (!desc || !desc->field_keys || !key) {
        return -1;
    }
    return vector_index_of(desc->field_keys, key);
}

bool tiny_fx_gfx_require_records_namespace(EvalState *st) {
    EvalState *load_state = st ? st : get_global_eval_state();
    if (!load_state) {
        return false;
    }
    tiny_fx_gfx_register_record_parse_symbols();
    return require_namespace_by_name(load_state, "tiny-fx.gfx-records");
}

static void init_record_keys(void) {
    if (g_record_keys_initialized) {
        return;
    }

    g_record_keys.k_root = &sym_kw_root_data.sym;
    g_record_keys.k_index = &sym_kw_index_data.sym;
    g_record_keys.k_id = &sym_kw_id_data.sym;
    g_record_keys.k_t = &sym_kw_t_data.sym;
    g_record_keys.k_style = &sym_kw_style_data.sym;
    g_record_keys.k_visible = &sym_kw_visible_data.sym;
    g_record_keys.k_children = &sym_kw_children_data.sym;
    g_record_keys.k_x1 = &sym_kw_x1_data.sym;
    g_record_keys.k_y1 = &sym_kw_y1_data.sym;
    g_record_keys.k_x2 = &sym_kw_x2_data.sym;
    g_record_keys.k_y2 = &sym_kw_y2_data.sym;
    g_record_keys.k_x3 = &sym_kw_x3_data.sym;
    g_record_keys.k_y3 = &sym_kw_y3_data.sym;
    g_record_keys.k_x = &sym_kw_x_data.sym;
    g_record_keys.k_y = &sym_kw_y_data.sym;
    g_record_keys.k_w = &sym_kw_w_data.sym;
    g_record_keys.k_h = &sym_kw_h_data.sym;
    g_record_keys.k_pts = &sym_kw_pts_data.sym;
    g_record_keys.k_closed = &sym_kw_closed_data.sym;
    g_record_keys.k_scale = &sym_kw_scale_data.sym;
    g_record_keys.k_rot = &sym_kw_rot_data.sym;
    g_record_keys.k_text = &sym_kw_text_data.sym;
    g_record_keys.k_keyframes = &sym_kw_keyframes_data.sym;
    g_record_keys.k_loop = &sym_kw_loop_data.sym;
    g_record_keys.k_end_event = &sym_kw_end_event_data.sym;
    g_record_keys.k_event_id = &sym_kw_event_id_data.sym;
    g_record_keys.k_tx = &sym_kw_tx_data.sym;
    g_record_keys.k_ty = &sym_kw_ty_data.sym;
    g_record_keys.k_sx = &sym_kw_sx_data.sym;
    g_record_keys.k_sy = &sym_kw_sy_data.sym;
    g_record_keys.k_stroke_color = &sym_kw_stroke_color_data.sym;
    g_record_keys.k_stroke_width = &sym_kw_stroke_width_data.sym;
    g_record_keys.k_has_fill = &sym_kw_has_fill_data.sym;
    g_record_keys.k_fill_color = &sym_kw_fill_color_data.sym;
    g_record_keys.k_has_bg_color = &sym_kw_has_bg_color_data.sym;
    g_record_keys.k_bg_color = &sym_kw_bg_color_data.sym;
    g_record_keys.k_clip_rect = &sym_kw_clip_rect_data.sym;
    g_record_keys.k_erase_color = &sym_kw_erase_color_data.sym;
    g_record_keys.k_z = &sym_kw_z_data.sym;
    g_record_keys.k_opaque = &sym_kw_opaque_data.sym;
    g_record_keys.k_guard_px = &sym_kw_guard_px_data.sym;
    g_record_keys_initialized = true;
}

bool tiny_fx_gfx_ensure_schema(EvalState *st) {
    (void)st;
    init_record_keys();

    g_record_schema.t_transform = &sym_type_transform_data.sym;
    g_record_schema.t_style = &sym_type_style_data.sym;
    g_record_schema.t_group = &sym_type_group_data.sym;
    g_record_schema.t_line = &sym_type_line_data.sym;
    g_record_schema.t_polyline = &sym_type_polyline_data.sym;
    g_record_schema.t_rect = &sym_type_rect_data.sym;
    g_record_schema.t_tri = &sym_type_tri_data.sym;
    g_record_schema.t_vtext = &sym_type_vtext_data.sym;
    g_record_schema.t_timeline = &sym_type_timeline_data.sym;
    g_record_schema.t_frame_scene = &sym_type_frame_scene_data.sym;
    g_record_schema.t_scene = &sym_type_scene_data.sym;

    CljRecordDescriptor *d_transform = record_descriptor_lookup(g_record_schema.t_transform);
    CljRecordDescriptor *d_style = record_descriptor_lookup(g_record_schema.t_style);
    CljRecordDescriptor *d_group = record_descriptor_lookup(g_record_schema.t_group);
    CljRecordDescriptor *d_line = record_descriptor_lookup(g_record_schema.t_line);
    CljRecordDescriptor *d_poly = record_descriptor_lookup(g_record_schema.t_polyline);
    CljRecordDescriptor *d_rect = record_descriptor_lookup(g_record_schema.t_rect);
    CljRecordDescriptor *d_tri = record_descriptor_lookup(g_record_schema.t_tri);
    CljRecordDescriptor *d_text = record_descriptor_lookup(g_record_schema.t_vtext);
    CljRecordDescriptor *d_timeline = record_descriptor_lookup(g_record_schema.t_timeline);
    CljRecordDescriptor *d_frame = record_descriptor_lookup(g_record_schema.t_frame_scene);
    CljRecordDescriptor *d_scene = record_descriptor_lookup(g_record_schema.t_scene);
    if (!d_transform || !d_style || !d_group || !d_line || !d_poly || !d_rect || !d_tri || !d_text ||
        !d_timeline || !d_frame || !d_scene) {
        return false;
    }

    g_record_schema.d_transform = d_transform;
    g_record_schema.d_style = d_style;
    g_record_schema.d_group = d_group;
    g_record_schema.d_line = d_line;
    g_record_schema.d_polyline = d_poly;
    g_record_schema.d_rect = d_rect;
    g_record_schema.d_tri = d_tri;
    g_record_schema.d_vtext = d_text;
    g_record_schema.d_timeline = d_timeline;
    g_record_schema.d_frame_scene = d_frame;
    g_record_schema.d_scene = d_scene;

    g_record_schema.h_transform = clj_hash(g_record_schema.t_transform);
    g_record_schema.h_style = clj_hash(g_record_schema.t_style);
    g_record_schema.h_group = clj_hash(g_record_schema.t_group);
    g_record_schema.h_line = clj_hash(g_record_schema.t_line);
    g_record_schema.h_polyline = clj_hash(g_record_schema.t_polyline);
    g_record_schema.h_rect = clj_hash(g_record_schema.t_rect);
    g_record_schema.h_tri = clj_hash(g_record_schema.t_tri);
    g_record_schema.h_vtext = clj_hash(g_record_schema.t_vtext);
    g_record_schema.h_timeline = clj_hash(g_record_schema.t_timeline);
    g_record_schema.h_frame_scene = clj_hash(g_record_schema.t_frame_scene);
    g_record_schema.h_scene = clj_hash(g_record_schema.t_scene);

    g_record_schema.n_transform = vector_count(d_transform->field_keys);
    g_record_schema.n_style = vector_count(d_style->field_keys);
    g_record_schema.n_group = vector_count(d_group->field_keys);
    g_record_schema.n_line = vector_count(d_line->field_keys);
    g_record_schema.n_polyline = vector_count(d_poly->field_keys);
    g_record_schema.n_rect = vector_count(d_rect->field_keys);
    g_record_schema.n_tri = vector_count(d_tri->field_keys);
    g_record_schema.n_vtext = vector_count(d_text->field_keys);
    g_record_schema.n_timeline = vector_count(d_timeline->field_keys);
    g_record_schema.n_frame_scene = vector_count(d_frame->field_keys);
    g_record_schema.n_scene = vector_count(d_scene->field_keys);

    g_record_schema.transform_tx = descriptor_index_of(d_transform, g_record_keys.k_tx);
    g_record_schema.transform_ty = descriptor_index_of(d_transform, g_record_keys.k_ty);
    g_record_schema.transform_sx = descriptor_index_of(d_transform, g_record_keys.k_sx);
    g_record_schema.transform_sy = descriptor_index_of(d_transform, g_record_keys.k_sy);
    g_record_schema.transform_rot = descriptor_index_of(d_transform, g_record_keys.k_rot);
    g_record_schema.style_stroke_color = descriptor_index_of(d_style, g_record_keys.k_stroke_color);
    g_record_schema.style_stroke_width = descriptor_index_of(d_style, g_record_keys.k_stroke_width);
    g_record_schema.style_visible = descriptor_index_of(d_style, g_record_keys.k_visible);
    g_record_schema.style_has_fill = descriptor_index_of(d_style, g_record_keys.k_has_fill);
    g_record_schema.style_fill_color = descriptor_index_of(d_style, g_record_keys.k_fill_color);
    g_record_schema.style_has_bg_color = descriptor_index_of(d_style, g_record_keys.k_has_bg_color);
    g_record_schema.style_bg_color = descriptor_index_of(d_style, g_record_keys.k_bg_color);
    g_record_schema.group_id = descriptor_index_of(d_group, g_record_keys.k_id);
    g_record_schema.group_t = descriptor_index_of(d_group, g_record_keys.k_t);
    g_record_schema.group_style = descriptor_index_of(d_group, g_record_keys.k_style);
    g_record_schema.group_visible = descriptor_index_of(d_group, g_record_keys.k_visible);
    g_record_schema.group_children = descriptor_index_of(d_group, g_record_keys.k_children);
    g_record_schema.line_id = descriptor_index_of(d_line, g_record_keys.k_id);
    g_record_schema.line_t = descriptor_index_of(d_line, g_record_keys.k_t);
    g_record_schema.line_style = descriptor_index_of(d_line, g_record_keys.k_style);
    g_record_schema.line_visible = descriptor_index_of(d_line, g_record_keys.k_visible);
    g_record_schema.line_x1 = descriptor_index_of(d_line, g_record_keys.k_x1);
    g_record_schema.line_y1 = descriptor_index_of(d_line, g_record_keys.k_y1);
    g_record_schema.line_x2 = descriptor_index_of(d_line, g_record_keys.k_x2);
    g_record_schema.line_y2 = descriptor_index_of(d_line, g_record_keys.k_y2);
    g_record_schema.poly_id = descriptor_index_of(d_poly, g_record_keys.k_id);
    g_record_schema.poly_t = descriptor_index_of(d_poly, g_record_keys.k_t);
    g_record_schema.poly_style = descriptor_index_of(d_poly, g_record_keys.k_style);
    g_record_schema.poly_visible = descriptor_index_of(d_poly, g_record_keys.k_visible);
    g_record_schema.poly_pts = descriptor_index_of(d_poly, g_record_keys.k_pts);
    g_record_schema.poly_closed = descriptor_index_of(d_poly, g_record_keys.k_closed);
    g_record_schema.rect_id = descriptor_index_of(d_rect, g_record_keys.k_id);
    g_record_schema.rect_t = descriptor_index_of(d_rect, g_record_keys.k_t);
    g_record_schema.rect_style = descriptor_index_of(d_rect, g_record_keys.k_style);
    g_record_schema.rect_visible = descriptor_index_of(d_rect, g_record_keys.k_visible);
    g_record_schema.rect_x = descriptor_index_of(d_rect, g_record_keys.k_x);
    g_record_schema.rect_y = descriptor_index_of(d_rect, g_record_keys.k_y);
    g_record_schema.rect_w = descriptor_index_of(d_rect, g_record_keys.k_w);
    g_record_schema.rect_h = descriptor_index_of(d_rect, g_record_keys.k_h);
    g_record_schema.tri_id = descriptor_index_of(d_tri, g_record_keys.k_id);
    g_record_schema.tri_t = descriptor_index_of(d_tri, g_record_keys.k_t);
    g_record_schema.tri_style = descriptor_index_of(d_tri, g_record_keys.k_style);
    g_record_schema.tri_visible = descriptor_index_of(d_tri, g_record_keys.k_visible);
    g_record_schema.tri_x1 = descriptor_index_of(d_tri, g_record_keys.k_x1);
    g_record_schema.tri_y1 = descriptor_index_of(d_tri, g_record_keys.k_y1);
    g_record_schema.tri_x2 = descriptor_index_of(d_tri, g_record_keys.k_x2);
    g_record_schema.tri_y2 = descriptor_index_of(d_tri, g_record_keys.k_y2);
    g_record_schema.tri_x3 = descriptor_index_of(d_tri, g_record_keys.k_x3);
    g_record_schema.tri_y3 = descriptor_index_of(d_tri, g_record_keys.k_y3);
    g_record_schema.text_id = descriptor_index_of(d_text, g_record_keys.k_id);
    g_record_schema.text_t = descriptor_index_of(d_text, g_record_keys.k_t);
    g_record_schema.text_style = descriptor_index_of(d_text, g_record_keys.k_style);
    g_record_schema.text_visible = descriptor_index_of(d_text, g_record_keys.k_visible);
    g_record_schema.text_x = descriptor_index_of(d_text, g_record_keys.k_x);
    g_record_schema.text_y = descriptor_index_of(d_text, g_record_keys.k_y);
    g_record_schema.text_scale = descriptor_index_of(d_text, g_record_keys.k_scale);
    g_record_schema.text_rot = descriptor_index_of(d_text, g_record_keys.k_rot);
    g_record_schema.text_text = descriptor_index_of(d_text, g_record_keys.k_text);
    g_record_schema.timeline_keyframes = descriptor_index_of(d_timeline, g_record_keys.k_keyframes);
    g_record_schema.timeline_loop = descriptor_index_of(d_timeline, g_record_keys.k_loop);
    g_record_schema.timeline_end_event = descriptor_index_of(d_timeline, g_record_keys.k_end_event);
    g_record_schema.timeline_event_id = descriptor_index_of(d_timeline, g_record_keys.k_event_id);
    g_record_schema.frame_root = descriptor_index_of(d_frame, g_record_keys.k_root);
    g_record_schema.frame_index = descriptor_index_of(d_frame, g_record_keys.k_index);
    g_record_schema.frame_clip_rect = descriptor_index_of(d_frame, g_record_keys.k_clip_rect);
    g_record_schema.frame_z = descriptor_index_of(d_frame, g_record_keys.k_z);
    g_record_schema.frame_visible = descriptor_index_of(d_frame, g_record_keys.k_visible);
    g_record_schema.frame_opaque = descriptor_index_of(d_frame, g_record_keys.k_opaque);
    g_record_schema.frame_erase_color = descriptor_index_of(d_frame, g_record_keys.k_erase_color);
    g_record_schema.frame_guard_px = descriptor_index_of(d_frame, g_record_keys.k_guard_px);
    g_record_schema.scene_root = descriptor_index_of(d_scene, g_record_keys.k_root);
    g_record_schema.scene_index = descriptor_index_of(d_scene, g_record_keys.k_index);
    g_record_schema.scene_clip_rect = descriptor_index_of(d_scene, g_record_keys.k_clip_rect);
    g_record_schema.scene_erase_color = descriptor_index_of(d_scene, g_record_keys.k_erase_color);

    if (g_record_schema.transform_tx < 0 || g_record_schema.transform_ty < 0 || g_record_schema.transform_sx < 0 ||
        g_record_schema.transform_sy < 0 || g_record_schema.transform_rot < 0 || g_record_schema.style_stroke_color < 0 ||
        g_record_schema.style_stroke_width < 0 || g_record_schema.style_visible < 0 || g_record_schema.style_has_fill < 0 ||
        g_record_schema.style_fill_color < 0 || g_record_schema.style_has_bg_color < 0 ||
        g_record_schema.style_bg_color < 0 || g_record_schema.group_id < 0 || g_record_schema.group_t < 0 ||
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
        g_record_schema.text_rot < 0 || g_record_schema.text_text < 0 ||
        g_record_schema.timeline_keyframes < 0 || g_record_schema.timeline_loop < 0 ||
        g_record_schema.timeline_end_event < 0 || g_record_schema.timeline_event_id < 0 ||
        g_record_schema.frame_root < 0 || g_record_schema.frame_index < 0 ||
        g_record_schema.frame_clip_rect < 0 || g_record_schema.frame_z < 0 || g_record_schema.frame_visible < 0 ||
        g_record_schema.frame_opaque < 0 || g_record_schema.frame_erase_color < 0 || g_record_schema.frame_guard_px < 0 ||
        g_record_schema.scene_root < 0 || g_record_schema.scene_index < 0 ||
        g_record_schema.scene_clip_rect < 0 || g_record_schema.scene_erase_color < 0) {
        return false;
    }

    return true;
}

const VgRecordSchema *tiny_fx_gfx_schema(void) {
    return &g_record_schema;
}

const VgRecordKeys *tiny_fx_gfx_record_keys(void) {
    init_record_keys();
    return &g_record_keys;
}

ID tiny_fx_gfx_get_field(ID record_obj, ID key, ID not_found) {
    if (!record_obj || !key) {
        return not_found;
    }
    if (TAG(record_obj) == CLJ_RECORD) {
        return record_get_sentinel(record_obj, key, not_found);
    }
    return not_found;
}

ID tiny_fx_gfx_create_record_from_slots(ID type_symbol, unsigned int field_count, ID *slots) {
    CljPersistentVector *v = make_vector(field_count, STRONG);
    if (!v) {
        return NULL;
    }
    for (unsigned int i = 0; i < field_count; i++) {
        vector_conj_inplace(&v, slots[i]);
    }
    CljRecordDescriptor *desc = record_descriptor_lookup(type_symbol);
    ID rec = make_record_with_descriptor(desc, v);
    RELEASE(v);
    return rec;
}
