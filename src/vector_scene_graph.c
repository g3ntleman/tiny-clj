#include "vector_scene_graph.h"
#include "gfx.h"
#include "value.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <string.h>

_Static_assert(VG_SCALE_ONE == CLJ_FIXED_SCALE, "VG_SCALE_ONE must match CLJ_FIXED_SCALE");

static GfxClip g_active_clip = {false, 0, 0, 0, 0};

static bool clip_rect_intersect_fb(VgClipRect in, const VgFrameBuffer *fb, VgClipRect *out) {
    if (!fb || !out) return false;
    VgClipRect fb_rect = {0, 0, (int16_t)fb->width, (int16_t)fb->height};
    return vg_clip_rect_intersect(in, fb_rect, out);
}

VgTransform vg_transform_identity(void) {
    VgTransform t;
    t.tx = 0;
    t.ty = 0;
    t.sx = VG_SCALE_ONE;
    t.sy = VG_SCALE_ONE;
    t.rot_deg = 0;
    return t;
}

VgStyle vg_style_default(void) {
    VgStyle s;
    s.stroke_color = 0xffffu;
    s.stroke_width = 1;
    s.visible = true;
    s.has_fill = false;
    s.fill_color = 0x0000u;
    s.has_bg_color = false;
    s.bg_color = 0x0000u;
    return s;
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

static uint32_t clip_rect_area_px(VgClipRect r) {
    if (vg_clip_rect_is_empty(r)) {
        return 0u;
    }
    return (uint32_t)r.w * (uint32_t)r.h;
}

static bool clip_rects_overlap(VgClipRect a, VgClipRect b) {
    return vg_clip_rect_intersect(a, b, NULL);
}

static bool dirty_union_area_within_merge_factor(uint32_t union_area, uint32_t separate_area) {
    /*
     * Keep this comparison division-free and overflow-safe:
     *   union_area / separate_area <= 4 / 3
     * becomes
     *   union_area * 3 <= separate_area * 4
     */
    const uint64_t lhs = (uint64_t)union_area * 3u;
    const uint64_t rhs = (uint64_t)separate_area * 4u;
    return lhs <= rhs;
}

static bool split_rect_to_budget(VgClipRect rect,
                                 uint32_t pixel_budget,
                                 VgClipRect *out_rects,
                                 size_t out_capacity,
                                 size_t *io_out_count) {
    if (vg_clip_rect_is_empty(rect)) {
        return true;
    }
    uint32_t area = clip_rect_area_px(rect);
    if (area <= pixel_budget || (rect.w <= 1 && rect.h <= 1)) {
        if (*io_out_count >= out_capacity) {
            return false;
        }
        out_rects[(*io_out_count)++] = rect;
        return true;
    }

    bool split_x = (rect.w >= rect.h && rect.w > 1) || rect.h <= 1;
    if (split_x && rect.w > 1) {
        size_t before = *io_out_count;
        int16_t w_left = (int16_t)(rect.w / 2);
        int16_t w_right = (int16_t)(rect.w - w_left);
        VgClipRect left = {.x = rect.x, .y = rect.y, .w = w_left, .h = rect.h};
        VgClipRect right = {.x = (int16_t)(rect.x + w_left), .y = rect.y, .w = w_right, .h = rect.h};
        if (split_rect_to_budget(left, pixel_budget, out_rects, out_capacity, io_out_count) &&
            split_rect_to_budget(right, pixel_budget, out_rects, out_capacity, io_out_count)) {
            return true;
        }
        *io_out_count = before;
        return false;
    }
    if (rect.h > 1) {
        size_t before = *io_out_count;
        int16_t h_top = (int16_t)(rect.h / 2);
        int16_t h_bottom = (int16_t)(rect.h - h_top);
        VgClipRect top = {.x = rect.x, .y = rect.y, .w = rect.w, .h = h_top};
        VgClipRect bottom = {.x = rect.x, .y = (int16_t)(rect.y + h_top), .w = rect.w, .h = h_bottom};
        if (split_rect_to_budget(top, pixel_budget, out_rects, out_capacity, io_out_count) &&
            split_rect_to_budget(bottom, pixel_budget, out_rects, out_capacity, io_out_count)) {
            return true;
        }
        *io_out_count = before;
        return false;
    }
    return false;
}

size_t vg_dirty_union_plan_rects(const VgClipRect *dirty_leaves,
                                 size_t leaf_count,
                                 uint32_t pixel_budget,
                                 VgClipRect *out_rects,
                                 size_t out_capacity) {
    if (!dirty_leaves || !out_rects || out_capacity == 0 || leaf_count == 0) {
        return 0u;
    }
    if (pixel_budget == 0u) {
        return 0u;
    }

    VgClipRect merged = {0};
    bool has_any = false;
    for (size_t i = 0; i < leaf_count; i++) {
        if (vg_clip_rect_is_empty(dirty_leaves[i])) {
            continue;
        }
        merged = has_any ? vg_clip_rect_union(merged, dirty_leaves[i]) : dirty_leaves[i];
        has_any = true;
    }
    if (!has_any) {
        return 0u;
    }

    bool assigned[leaf_count];
    for (size_t i = 0; i < leaf_count; i++) {
        assigned[i] = false;
    }

    size_t out_i = 0u;
    for (size_t i = 0; i < leaf_count; i++) {
        if (assigned[i] || vg_clip_rect_is_empty(dirty_leaves[i])) {
            continue;
        }

        size_t cluster_members[leaf_count];
        size_t cluster_count = 0u;
        assigned[i] = true;
        cluster_members[cluster_count++] = i;

        /*
         * Build connected components from actual leaf overlaps so we do not
         * merge a rect that only happens to lie inside the cluster union bbox.
         */
        for (size_t cursor = 0; cursor < cluster_count; cursor++) {
            size_t member_index = cluster_members[cursor];
            for (size_t j = 0; j < leaf_count; j++) {
                if (assigned[j] || vg_clip_rect_is_empty(dirty_leaves[j])) {
                    continue;
                }
                if (clip_rects_overlap(dirty_leaves[member_index], dirty_leaves[j])) {
                    assigned[j] = true;
                    cluster_members[cluster_count++] = j;
                }
            }
        }

        VgClipRect cluster_union = dirty_leaves[cluster_members[0]];
        uint32_t cluster_leaf_area_sum = clip_rect_area_px(cluster_union);
        for (size_t m = 1; m < cluster_count; m++) {
            cluster_union = vg_clip_rect_union(cluster_union, dirty_leaves[cluster_members[m]]);
            cluster_leaf_area_sum += clip_rect_area_px(dirty_leaves[cluster_members[m]]);
        }

        uint32_t cluster_union_area = clip_rect_area_px(cluster_union);
        bool cluster_union_fits_budget = cluster_union_area <= pixel_budget;
        bool cluster_union_is_worth_it =
            dirty_union_area_within_merge_factor(cluster_union_area, cluster_leaf_area_sum);

        if (cluster_union_fits_budget && cluster_union_is_worth_it) {
            if (out_i >= out_capacity) {
                out_rects[0] = merged;
                return 1u;
            }
            out_rects[out_i++] = cluster_union;
            continue;
        }

        bool cluster_leaves_fit = true;
        for (size_t m = 0; m < cluster_count; m++) {
            if (clip_rect_area_px(dirty_leaves[cluster_members[m]]) > pixel_budget) {
                cluster_leaves_fit = false;
                break;
            }
        }

        if (cluster_leaves_fit) {
            if (out_i + cluster_count > out_capacity) {
                out_rects[0] = merged;
                return 1u;
            }
            for (size_t m = 0; m < cluster_count; m++) {
                out_rects[out_i++] = dirty_leaves[cluster_members[m]];
            }
            continue;
        }

        for (size_t m = 0; m < cluster_count; m++) {
            VgClipRect leaf = dirty_leaves[cluster_members[m]];
            uint32_t area = clip_rect_area_px(leaf);
            if (area <= pixel_budget) {
                if (out_i >= out_capacity) {
                    out_rects[0] = merged;
                    return 1u;
                }
                out_rects[out_i++] = leaf;
                continue;
            }
            if (!split_rect_to_budget(leaf,
                                      pixel_budget,
                                      out_rects,
                                      out_capacity,
                                      &out_i)) {
                out_rects[0] = merged;
                return 1u;
            }
        }
    }

    return out_i;
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

static int32_t fp_clamp_unit(int32_t v) {
    if (v <= 0) return 0;
    if (v >= VG_FP_ONE) return VG_FP_ONE;
    return v;
}

int32_t vg_anim_progress_q13(uint32_t elapsed_ms, uint32_t duration_ms) {
    if (duration_ms == 0u) return VG_FP_ONE;
    if (elapsed_ms == 0u) return 0;
    if (elapsed_ms >= duration_ms) return VG_FP_ONE;
    return (int32_t)(((uint64_t)elapsed_ms << VG_FP_SHIFT) / (uint64_t)duration_ms);
}

int32_t vg_anim_ease_q13(VgAnimEase ease, int32_t t_q13) {
    int32_t t = fp_clamp_unit(t_q13);
    int32_t inv;
    int32_t sq;

    switch (ease) {
    case VG_ANIM_EASE_LINEAR:
        return t;
    case VG_ANIM_EASE_IN_QUAD:
        return fp_mul_fixed(t, t);
    case VG_ANIM_EASE_OUT_QUAD:
        inv = VG_FP_ONE - t;
        return VG_FP_ONE - fp_mul_fixed(inv, inv);
    case VG_ANIM_EASE_IN_OUT_QUAD:
        if (t <= (VG_FP_ONE >> 1)) {
            sq = fp_mul_fixed(t, t);
            return fp_clamp_unit(sq << 1);
        }
        inv = VG_FP_ONE - t;
        sq = fp_mul_fixed(inv, inv);
        return VG_FP_ONE - fp_clamp_unit(sq << 1);
    case VG_ANIM_EASE_OUT_CUBIC: {
        inv = VG_FP_ONE - t;
        int32_t inv2 = fp_mul_fixed(inv, inv);
        int32_t inv3 = fp_mul_fixed(inv2, inv);
        return VG_FP_ONE - inv3;
    }
    default:
        return t;
    }
}

int32_t vg_anim_lerp_q13(int32_t from_q13, int32_t to_q13, int32_t t_q13) {
    int32_t t = fp_clamp_unit(t_q13);
    int64_t delta = (int64_t)to_q13 - (int64_t)from_q13;
    int64_t scaled = (delta * (int64_t)t) >> VG_FP_SHIFT;
    return (int32_t)((int64_t)from_q13 + scaled);
}

static int16_t anim_q13_to_i16_round_sat(int32_t value_q13) {
    int32_t value = 0;
    if (value_q13 >= 0) {
        value = (int32_t)(((int64_t)value_q13 + (VG_FP_ONE / 2)) / VG_FP_ONE);
    } else {
        int64_t abs_q13 = -(int64_t)value_q13;
        value = (int32_t)-((abs_q13 + (VG_FP_ONE / 2)) / VG_FP_ONE);
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)value;
}

static int32_t anim_i16_to_q13(int16_t value) {
    return ((int32_t)value) << VG_FP_SHIFT;
}

static int32_t anim_step_toward_q13(int32_t current_q13, int32_t target_q13, int32_t eased_t_q13) {
    if (current_q13 == target_q13) {
        return current_q13;
    }
    int32_t next_q13 = vg_anim_lerp_q13(current_q13, target_q13, eased_t_q13);
    if (next_q13 == current_q13) {
        next_q13 += (target_q13 > current_q13) ? 1 : -1;
    }
    if (target_q13 > current_q13 && next_q13 > target_q13) {
        return target_q13;
    }
    if (target_q13 < current_q13 && next_q13 < target_q13) {
        return target_q13;
    }
    return next_q13;
}

static void anim_set_current_and_target(VgAnimTransformState *state, VgTransform value) {
    if (!state) {
        return;
    }
    int32_t tx_q13 = anim_i16_to_q13(value.tx);
    int32_t ty_q13 = anim_i16_to_q13(value.ty);
    int32_t rot_q13 = anim_i16_to_q13(value.rot_deg);
    state->current_tx_q13 = tx_q13;
    state->current_ty_q13 = ty_q13;
    state->current_sx_q13 = value.sx;
    state->current_sy_q13 = value.sy;
    state->current_rot_q13 = rot_q13;
    state->target_tx_q13 = tx_q13;
    state->target_ty_q13 = ty_q13;
    state->target_sx_q13 = value.sx;
    state->target_sy_q13 = value.sy;
    state->target_rot_q13 = rot_q13;
}

VgTransform vg_anim_transform_state_current(const VgAnimTransformState *state) {
    if (!state || !state->initialized) {
        return vg_transform_identity();
    }
    VgTransform out = vg_transform_identity();
    out.tx = anim_q13_to_i16_round_sat(state->current_tx_q13);
    out.ty = anim_q13_to_i16_round_sat(state->current_ty_q13);
    out.sx = state->current_sx_q13;
    out.sy = state->current_sy_q13;
    out.rot_deg = anim_q13_to_i16_round_sat(state->current_rot_q13);
    return out;
}

void vg_anim_transform_state_reset(VgAnimTransformState *state,
                                   VgTransform initial,
                                   uint32_t response_ms,
                                   VgAnimEase ease) {
    if (!state) {
        return;
    }
    state->initialized = true;
    state->response_ms = response_ms;
    state->ease = ease;
    anim_set_current_and_target(state, initial);
}

void vg_anim_transform_state_set_target(VgAnimTransformState *state, VgTransform target) {
    if (!state) {
        return;
    }
    if (!state->initialized) {
        vg_anim_transform_state_reset(state, target, state->response_ms, state->ease);
        return;
    }
    state->target_tx_q13 = anim_i16_to_q13(target.tx);
    state->target_ty_q13 = anim_i16_to_q13(target.ty);
    state->target_sx_q13 = target.sx;
    state->target_sy_q13 = target.sy;
    state->target_rot_q13 = anim_i16_to_q13(target.rot_deg);
}

VgTransform vg_anim_transform_state_step(VgAnimTransformState *state, uint32_t dt_ms) {
    if (!state) {
        return vg_transform_identity();
    }
    if (!state->initialized) {
        VgTransform initial = vg_transform_identity();
        vg_anim_transform_state_reset(state, initial, state->response_ms, state->ease);
        return initial;
    }
    if (state->response_ms == 0u) {
        state->current_tx_q13 = state->target_tx_q13;
        state->current_ty_q13 = state->target_ty_q13;
        state->current_sx_q13 = state->target_sx_q13;
        state->current_sy_q13 = state->target_sy_q13;
        state->current_rot_q13 = state->target_rot_q13;
        return vg_anim_transform_state_current(state);
    }

    int32_t progress_q13 = vg_anim_progress_q13(dt_ms, state->response_ms);
    int32_t eased_q13 = vg_anim_ease_q13(state->ease, progress_q13);
    if (eased_q13 <= 0) {
        return vg_anim_transform_state_current(state);
    }
    if (eased_q13 >= VG_FP_ONE) {
        state->current_tx_q13 = state->target_tx_q13;
        state->current_ty_q13 = state->target_ty_q13;
        state->current_sx_q13 = state->target_sx_q13;
        state->current_sy_q13 = state->target_sy_q13;
        state->current_rot_q13 = state->target_rot_q13;
        return vg_anim_transform_state_current(state);
    }

    state->current_tx_q13 = anim_step_toward_q13(state->current_tx_q13, state->target_tx_q13, eased_q13);
    state->current_ty_q13 = anim_step_toward_q13(state->current_ty_q13, state->target_ty_q13, eased_q13);
    state->current_sx_q13 = anim_step_toward_q13(state->current_sx_q13, state->target_sx_q13, eased_q13);
    state->current_sy_q13 = anim_step_toward_q13(state->current_sy_q13, state->target_sy_q13, eased_q13);
    state->current_rot_q13 = anim_step_toward_q13(state->current_rot_q13, state->target_rot_q13, eased_q13);
    return vg_anim_transform_state_current(state);
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
    int16_t rot = t.rot_deg;
    while (rot <= -180) rot = (int16_t)(rot + 360);
    while (rot > 180)  rot = (int16_t)(rot - 360);

    int32_t tx_fp = ((int32_t)t.tx) << VG_FP_SHIFT;
    int32_t ty_fp = ((int32_t)t.ty) << VG_FP_SHIFT;

    if (rot == 0) {
        VgTransformFixed mf = vg_transform_fixed_identity();
        mf.m00 = t.sx;
        mf.m11 = t.sy;
        mf.m02 = tx_fp;
        mf.m12 = ty_fp;
        return mf;
    }
    if (rot == 90) {
        VgTransformFixed mf = vg_transform_fixed_identity();
        mf.m00 = 0;
        mf.m01 = -t.sy;
        mf.m10 = t.sx;
        mf.m11 = 0;
        mf.m02 = tx_fp;
        mf.m12 = ty_fp;
        return mf;
    }
    if (rot == -90) {
        VgTransformFixed mf = vg_transform_fixed_identity();
        mf.m00 = 0;
        mf.m01 = t.sy;
        mf.m10 = -t.sx;
        mf.m11 = 0;
        mf.m02 = tx_fp;
        mf.m12 = ty_fp;
        return mf;
    }
    if (rot == 180 || rot == -180) {
        VgTransformFixed mf = vg_transform_fixed_identity();
        mf.m00 = -t.sx;
        mf.m11 = -t.sy;
        mf.m02 = tx_fp;
        mf.m12 = ty_fp;
        return mf;
    }

    /* Non-cardinal angle: float trig fallback (cold path). */
    float r = (float)rot * ((float)M_PI / 180.0f);
    float c = cosf(r);
    float s = sinf(r);
    float sx_f = (float)t.sx / (float)VG_FP_ONE;
    float sy_f = (float)t.sy / (float)VG_FP_ONE;
    VgTransformFixed mf;
    mf.m00 = fp_from_float(c * sx_f);
    mf.m01 = fp_from_float(-s * sy_f);
    mf.m02 = tx_fp;
    mf.m10 = fp_from_float(s * sx_f);
    mf.m11 = fp_from_float(c * sy_f);
    mf.m12 = ty_fp;
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

void vg_framebuffer_clear_rect(VgFrameBuffer *fb, VgClipRect rect, uint16_t color) {
    if (!fb || !fb->pixels || vg_clip_rect_is_empty(rect)) {
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
                        style.stroke_color,
                        (int)style.stroke_width,
                        style.has_bg_color,
                        style.bg_color);
    }
    if (closed && count > 2) {
        draw_line_thick(fb,
                        vx[count - 1], vy[count - 1],
                        vx[0], vy[0],
                        style.stroke_color,
                        (int)style.stroke_width,
                        style.has_bg_color,
                        style.bg_color);
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
    draw_line_thick(fb, x1, y1, x2, y2, style.stroke_color, (int)style.stroke_width, style.has_bg_color, style.bg_color);
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
                fill_polygon_scanline(fb, vx, vy, p->point_count, style.fill_color);
            }
            draw_stroke_polyline_xy(fb, vx, vy, p->point_count, p->closed, style);
            return;
        }
    }
    for (size_t i = 1; i < p->point_count; i++) {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        apply_xy_fixed_px(&tf, p->points[i - 1].x, p->points[i - 1].y, &x1, &y1);
        apply_xy_fixed_px(&tf, p->points[i].x, p->points[i].y, &x2, &y2);
        draw_line_thick(fb, x1, y1, x2, y2, style.stroke_color, (int)style.stroke_width, style.has_bg_color, style.bg_color);
    }
    if (p->closed && p->point_count > 2) {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        apply_xy_fixed_px(&tf, p->points[p->point_count - 1].x, p->points[p->point_count - 1].y, &x1, &y1);
        apply_xy_fixed_px(&tf, p->points[0].x, p->points[0].y, &x2, &y2);
        draw_line_thick(fb, x1, y1, x2, y2, style.stroke_color, (int)style.stroke_width, style.has_bg_color, style.bg_color);
    }
}

static void draw_rect_node(VgFrameBuffer *fb, const VgRectData *r, VgTransformFixed tf, VgStyle style) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, x4 = 0, y4 = 0;
    int16_t x_max = (r->w > 0) ? (int16_t)(r->x + r->w - 1) : r->x;
    int16_t y_max = (r->h > 0) ? (int16_t)(r->y + r->h - 1) : r->y;
    apply_xy_fixed_px(&tf, r->x, r->y, &x1, &y1);
    apply_xy_fixed_px(&tf, x_max, r->y, &x2, &y2);
    apply_xy_fixed_px(&tf, x_max, y_max, &x3, &y3);
    apply_xy_fixed_px(&tf, r->x, y_max, &x4, &y4);
    if (style.has_fill) {
        int vx[4] = {x1, x2, x3, x4};
        int vy[4] = {y1, y2, y3, y4};
        fill_polygon_scanline(fb, vx, vy, 4, style.fill_color);
    }
    draw_line_thick(fb, x1, y1, x2, y2, style.stroke_color, (int)style.stroke_width, style.has_bg_color, style.bg_color);
    draw_line_thick(fb, x2, y2, x3, y3, style.stroke_color, (int)style.stroke_width, style.has_bg_color, style.bg_color);
    draw_line_thick(fb, x3, y3, x4, y4, style.stroke_color, (int)style.stroke_width, style.has_bg_color, style.bg_color);
    draw_line_thick(fb, x4, y4, x1, y1, style.stroke_color, (int)style.stroke_width, style.has_bg_color, style.bg_color);
}

static void draw_tri_node(VgFrameBuffer *fb, const VgTriData *tr, VgTransformFixed tf, VgStyle style) {
    int vx[3] = {0, 0, 0};
    int vy[3] = {0, 0, 0};
    apply_xy_fixed_px(&tf, tr->x1, tr->y1, &vx[0], &vy[0]);
    apply_xy_fixed_px(&tf, tr->x2, tr->y2, &vx[1], &vy[1]);
    apply_xy_fixed_px(&tf, tr->x3, tr->y3, &vx[2], &vy[2]);
    if (style.has_fill) {
        fill_polygon_scanline(fb, vx, vy, 3, style.fill_color);
    }
    draw_stroke_polyline_xy(fb, vx, vy, 3, true, style);
}

static void draw_text_node(VgFrameBuffer *fb, const VgTextData *txt, VgTransformFixed parent_t, VgStyle style) {
    if (!txt->text || txt->text[0] == '\0') {
        return;
    }
    int32_t scale_fp = (txt->scale > 0) ? txt->scale : VG_FP_ONE;
    bool text_scale_large = (scale_fp >= ((VG_FP_ONE * 5) / 4));

    VgTransformFixed local = vg_transform_fixed_identity();
    local.m02 = ((int32_t)txt->x) << VG_FP_SHIFT;
    local.m12 = ((int32_t)txt->y) << VG_FP_SHIFT;
    local.m00 = scale_fp;
    local.m11 = scale_fp;
    if (txt->rot_deg != 0) {
        VgTransform local_t = vg_transform_identity();
        local_t.tx = txt->x;
        local_t.ty = txt->y;
        local_t.sx = scale_fp;
        local_t.sy = scale_fp;
        local_t.rot_deg = txt->rot_deg;
        local = vg_transform_fixed_from_transform(local_t);
    }
    VgTransformFixed t_fixed = vg_transform_fixed_compose(parent_t, local);

    // Text-specific line draw wrapper:
    // - for 1px no-AA use supercover to avoid simple Bresenham dropouts
    // - at larger non-integer scales (e.g. 1.4), draw diagonal strokes with a
    //   minimal extra thickness to prevent intermittent missing pixels.
    #define DRAW_TEXT_SEGMENT(xa, ya, xb, yb, st) do { \
        if ((st).stroke_width <= 1 && !(st).has_bg_color) { \
            int _dx = abs((xb) - (xa)); \
            int _dy = abs((yb) - (ya)); \
            if (text_scale_large && !is_arcade_ascii_glyph && _dx > 0 && _dy > 0) { \
                draw_line_thick(fb, (xa), (ya), (xb), (yb), (st).stroke_color, 2, false, (st).bg_color); \
            } else { \
                draw_line_supercover(fb, (xa), (ya), (xb), (yb), (st).stroke_color); \
            } \
        } else { \
            draw_line_thick(fb, (xa), (ya), (xb), (yb), (st).stroke_color, (int)(st).stroke_width, (st).has_bg_color, (st).bg_color); \
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
            glyph_style.has_bg_color = false;
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
                draw_fill_rect(fb, _xmin, _ymin, (_xmax - _xmin) + 1, (_ymax - _ymin) + 1, glyph_style.stroke_color); \
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

static void render_node_with_world_transform(const VgNode *node, VgTransformFixed world_t, VgFrameBuffer *fb) {
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
    switch (node->type) {
        case VG_NODE_LINE:
            draw_line_node(fb, &node->data.line, world_t, style);
            break;
        case VG_NODE_POLYLINE:
            draw_polyline_node(fb, &node->data.polyline, world_t, style);
            break;
        case VG_NODE_RECT:
            draw_rect_node(fb, &node->data.rect, world_t, style);
            break;
        case VG_NODE_TRI:
            draw_tri_node(fb, &node->data.tri, world_t, style);
            break;
        case VG_NODE_VTEXT:
            draw_text_node(fb, &node->data.text, world_t, style);
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

void vg_render_node_fixed(const VgNode *node, VgTransformFixed world_t, VgFrameBuffer *fb) {
    if (!node || !fb) {
        return;
    }
    GfxClip prev_clip = g_active_clip;
    g_active_clip.enabled = false;
    render_node_with_world_transform(node, world_t, fb);
    g_active_clip = prev_clip;
}

void vg_render_node_fixed_clipped(const VgNode *node, VgTransformFixed world_t, VgFrameBuffer *fb, VgClipRect clip_rect) {
    if (!node || !fb) {
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
    render_node_with_world_transform(node, world_t, fb);
    g_active_clip = prev_clip;
}

bool vg_render_slot_if_changed(const VgRenderSlot *slot,
                               VgRenderSlotState *state,
                               VgFrameBuffer *fb,
                               uint32_t snapshot_id) {
    if (!slot || !state || !fb) {
        return false;
    }
    VgClipRect slot_rect = vg_clip_rect_expand(slot->clip_rect, slot->guard_px);
    bool props_changed = !state->initialized ||
                         state->last_visible != slot->visible ||
                         state->last_opaque != slot->opaque ||
                         state->last_clear_color != slot->clear_color ||
                         state->last_guard_px != slot->guard_px ||
                         !vg_clip_rect_equal(state->last_clip_rect, slot->clip_rect);
    bool snapshot_changed = !state->initialized || state->snapshot_id != snapshot_id;
    if (!props_changed && !snapshot_changed) {
        return false;
    }

    VgClipRect dirty_rect = slot_rect;
    if (state->initialized) {
        VgClipRect prev_rect = vg_clip_rect_expand(state->last_clip_rect, state->last_guard_px);
        dirty_rect = vg_clip_rect_union(prev_rect, slot_rect);
    }
    vg_framebuffer_clear_rect(fb, dirty_rect, slot->clear_color);

    if (slot->visible && slot->root) {
        vg_render_scene_clipped(slot->root, fb, slot_rect);
    }

    state->initialized = true;
    state->snapshot_id = snapshot_id;
    state->last_clip_rect = slot->clip_rect;
    state->last_visible = slot->visible;
    state->last_opaque = slot->opaque;
    state->last_clear_color = slot->clear_color;
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
