#include "vector_scene_graph.h"
#include "gfx.h"
#include "memory.h"
#include "value.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <string.h>

_Static_assert(VG_SCALE_ONE == CLJ_FIXED_SCALE, "VG_SCALE_ONE must match CLJ_FIXED_SCALE");
#define VG_DIRTY_PLAN_STACK_CAP 64u

static GfxClip g_active_clip = {false, 0, 0, 0, 0};

typedef uint32_t VgAliasU32 __attribute__((__may_alias__));

static inline bool vg_transform_fixed_is_identity_matrix(VgTransformFixed t) {
    return t.m00 == CLJ_FIXED_SCALE && t.m01 == 0 && t.m02 == 0 &&
           t.m10 == 0 && t.m11 == CLJ_FIXED_SCALE && t.m12 == 0;
}

static inline bool vg_transform_fixed_is_axis_aligned_matrix(VgTransformFixed t) {
    return t.m01 == 0 && t.m10 == 0;
}

static inline bool vg_transform_fixed_is_quadrant_swap_matrix(VgTransformFixed t) {
    return t.m00 == 0 && t.m11 == 0;
}

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

static void vg_fill_u16_span(uint16_t *pixels, size_t count, uint16_t color) {
    if (!pixels || count == 0u) {
        return;
    }

    uint16_t *cursor = pixels;
    size_t remaining = count;
    const uint32_t packed = (uint32_t)color | ((uint32_t)color << 16);

    if ((((uintptr_t)cursor) & (sizeof(uint32_t) - 1u)) != 0u) {
        *cursor++ = color;
        remaining--;
    }

    VgAliasU32 *word_cursor = (VgAliasU32 *)(void *)cursor;
    size_t word_count = remaining / 2u;
    for (size_t i = 0; i < word_count; i++) {
        word_cursor[i] = packed;
    }

    if ((remaining & 1u) != 0u) {
        cursor[word_count * 2u] = color;
    }
}

void vg_framebuffer_clear(VgFrameBuffer *fb, uint16_t color) {
    if (!fb || !fb->pixels) {
        return;
    }
    size_t count = (size_t)fb->width * (size_t)fb->height;
    vg_fill_u16_span(fb->pixels, count, color);
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
     *   union_area / separate_area <= 5 / 4
     * becomes
     *   union_area * 4 <= separate_area * 5
     *
     * A slightly stricter threshold keeps skinny overlap clusters split when
     * the merged union would introduce too much redraw padding around the
     * actual dirty leaves.
     */
    const uint64_t lhs = (uint64_t)union_area * 4u;
    const uint64_t rhs = (uint64_t)separate_area * 5u;
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

    uint8_t assigned_stack[VG_DIRTY_PLAN_STACK_CAP];
    size_t cluster_members_stack[VG_DIRTY_PLAN_STACK_CAP];
    uint8_t *assigned = assigned_stack;
    size_t *cluster_members = cluster_members_stack;
    bool using_heap_scratch = false;

    if (leaf_count > VG_DIRTY_PLAN_STACK_CAP) {
        size_t assigned_bytes = leaf_count * sizeof(*assigned);
        if (leaf_count > (SIZE_MAX / sizeof(*cluster_members))) {
            out_rects[0] = merged;
            return 1u;
        }
        size_t cluster_bytes = leaf_count * sizeof(*cluster_members);
        if (assigned_bytes > (SIZE_MAX - cluster_bytes)) {
            out_rects[0] = merged;
            return 1u;
        }
        size_t scratch_bytes = assigned_bytes + cluster_bytes;
        uint8_t *scratch = (uint8_t *)CLJ_HOST_MALLOC(scratch_bytes);
        if (!scratch) {
            out_rects[0] = merged;
            return 1u;
        }
        memset(scratch, 0, scratch_bytes);
        assigned = scratch;
        cluster_members = (size_t *)(void *)(scratch + assigned_bytes);
        using_heap_scratch = true;
    } else {
        memset(assigned_stack, 0, leaf_count * sizeof(*assigned_stack));
    }

    size_t out_i = 0u;
    size_t plan_result = 0u;
    for (size_t i = 0; i < leaf_count; i++) {
        if (assigned[i] || vg_clip_rect_is_empty(dirty_leaves[i])) {
            continue;
        }

        size_t cluster_count = 0u;
        assigned[i] = 1u;
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
                    assigned[j] = 1u;
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
                plan_result = 1u;
                goto cleanup;
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
                plan_result = 1u;
                goto cleanup;
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
                    plan_result = 1u;
                    goto cleanup;
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
                plan_result = 1u;
                goto cleanup;
            }
        }
    }

    plan_result = out_i;

cleanup:
    if (using_heap_scratch) {
        CLJ_HOST_FREE(assigned);
    }
    return plan_result;
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
    if (a == 0 || b == 0) {
        return 0;
    }
    if (a == VG_FP_ONE) {
        return b;
    }
    if (b == VG_FP_ONE) {
        return a;
    }
    if (a == -VG_FP_ONE) {
        return -b;
    }
    if (b == -VG_FP_ONE) {
        return -a;
    }
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
    if (elapsed_ms <= (UINT32_MAX >> VG_FP_SHIFT)) {
        return (int32_t)((elapsed_ms << VG_FP_SHIFT) / duration_ms);
    }
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
    if (t == 0) {
        return from_q13;
    }
    if (t >= VG_FP_ONE) {
        return to_q13;
    }
    if (from_q13 == to_q13) {
        return from_q13;
    }
    int64_t delta = (int64_t)to_q13 - (int64_t)from_q13;
    int64_t scaled = (delta * (int64_t)t) >> VG_FP_SHIFT;
    return (int32_t)((int64_t)from_q13 + scaled);
}

static int16_t anim_q13_to_i16_round_sat(int32_t value_q13) {
    const int32_t half = VG_FP_ONE / 2;
    const int32_t positive_saturate = (((int32_t)INT16_MAX) << VG_FP_SHIFT) + half;
    const int32_t negative_saturate = -((((int32_t)INT16_MAX) + 1) << VG_FP_SHIFT) - half;

    if (value_q13 >= positive_saturate) {
        return INT16_MAX;
    }
    if (value_q13 <= negative_saturate) {
        return INT16_MIN;
    }

    if (value_q13 >= 0) {
        return (int16_t)((value_q13 + half) >> VG_FP_SHIFT);
    }
    return (int16_t)(-(((-value_q13) + half) >> VG_FP_SHIFT));
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
    if (vg_transform_fixed_is_identity_matrix(parent)) {
        return local;
    }
    if (vg_transform_fixed_is_identity_matrix(local)) {
        return parent;
    }
    if (vg_transform_fixed_is_axis_aligned_matrix(parent) && vg_transform_fixed_is_axis_aligned_matrix(local)) {
        VgTransformFixed m;
        m.m00 = fp_mul_fixed(parent.m00, local.m00);
        m.m01 = 0;
        m.m02 = fp_mul_fixed(parent.m00, local.m02) + parent.m02;
        m.m10 = 0;
        m.m11 = fp_mul_fixed(parent.m11, local.m11);
        m.m12 = fp_mul_fixed(parent.m11, local.m12) + parent.m12;
        return m;
    }

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
    int32_t ox = 0;
    int32_t oy = 0;

    if (vg_transform_fixed_is_axis_aligned_matrix(*m)) {
        ox = fp_mul_fixed(m->m00, x_fp) + m->m02;
        oy = fp_mul_fixed(m->m11, y_fp) + m->m12;
    } else if (vg_transform_fixed_is_quadrant_swap_matrix(*m)) {
        ox = fp_mul_fixed(m->m01, y_fp) + m->m02;
        oy = fp_mul_fixed(m->m10, x_fp) + m->m12;
    } else {
        ox = fp_mul_fixed(m->m00, x_fp) + fp_mul_fixed(m->m01, y_fp) + m->m02;
        oy = fp_mul_fixed(m->m10, x_fp) + fp_mul_fixed(m->m11, y_fp) + m->m12;
    }

    *out_x = fp_to_int_round(ox);
    *out_y = fp_to_int_round(oy);
}

void vg_transform_fixed_apply_px(VgTransformFixed t, int16_t x, int16_t y, int *out_x, int *out_y) {
    int32_t x_fp = ((int32_t)x) << VG_FP_SHIFT;
    int32_t y_fp = ((int32_t)y) << VG_FP_SHIFT;
    int32_t ox = 0;
    int32_t oy = 0;

    if (vg_transform_fixed_is_axis_aligned_matrix(t)) {
        ox = fp_mul_fixed(t.m00, x_fp) + t.m02;
        oy = fp_mul_fixed(t.m11, y_fp) + t.m12;
    } else if (vg_transform_fixed_is_quadrant_swap_matrix(t)) {
        ox = fp_mul_fixed(t.m01, y_fp) + t.m02;
        oy = fp_mul_fixed(t.m10, x_fp) + t.m12;
    } else {
        ox = fp_mul_fixed(t.m00, x_fp) + fp_mul_fixed(t.m01, y_fp) + t.m02;
        oy = fp_mul_fixed(t.m10, x_fp) + fp_mul_fixed(t.m11, y_fp) + t.m12;
    }

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
    if (x0 == 0 && x1 == fb->width) {
        size_t row_offset = (size_t)y0 * (size_t)fb->width;
        size_t count = (size_t)(y1 - y0) * (size_t)fb->width;
        vg_fill_u16_span(&fb->pixels[row_offset], count, color);
        return;
    }
    for (int y = y0; y < y1; y++) {
        size_t row_offset = (size_t)y * (size_t)fb->width + (size_t)x0;
        vg_fill_u16_span(&fb->pixels[row_offset], (size_t)(x1 - x0), color);
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

static int vg_text_glyph_advance(char c) {
    switch (c) {
        case ' ': return 5;
        case '.':
        case ',':
        case ':':
        case ';':
        case '!':
        case '(':
        case ')':
            return 4;
        case '?':
            return 6;
        case '%':
            return 7;
        default:
            return 8;
    }
}

bool vg_text_local_bounds(const VgTextData *txt, VgRectData *out_bounds) {
    if (!txt || !out_bounds || !txt->text || txt->text[0] == '\0') {
        return false;
    }
    int pen_x = 0;
    bool have_visible_glyph = false;
    int min_x = 0;
    int max_x = 0;
    size_t len = strlen(txt->text);
    for (size_t i = 0; i < len; i++) {
        unsigned char uc = (unsigned char)txt->text[i];
        char c = (char)toupper((int)uc);
        bool is_hv_mono_alnum = ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'));
        int adv = is_hv_mono_alnum ? 10 : vg_text_glyph_advance(c);
        bool visible_glyph = (c != ' ');
        if (visible_glyph) {
            int glyph_min_x = pen_x + (is_hv_mono_alnum ? 1 : 0);
            int glyph_max_x = pen_x + adv - 1;
            if (!have_visible_glyph) {
                min_x = glyph_min_x;
                max_x = glyph_max_x;
                have_visible_glyph = true;
            } else {
                if (glyph_min_x < min_x) {
                    min_x = glyph_min_x;
                }
                if (glyph_max_x > max_x) {
                    max_x = glyph_max_x;
                }
            }
        }
        pen_x += adv;
        if (pen_x < 0) {
            pen_x = 0;
        }
    }
    if (!have_visible_glyph || max_x < min_x) {
        return false;
    }
    out_bounds->x = (int16_t)min_x;
    out_bounds->y = 0;
    out_bounds->w = (int16_t)((max_x - min_x) + 1);
    out_bounds->h = 11;
    return true;
}

typedef struct {
    int8_t x1;
    int8_t y1;
    int8_t x2;
    int8_t y2;
} VgGlyphStrokeSeg;

typedef struct {
    uint8_t advance;
    uint8_t segment_count;
    const VgGlyphStrokeSeg *segments;
} VgHvStrokeGlyph;

#define VG_SEG(x1, y1, x2, y2) { (int8_t)(x1), (int8_t)(y1), (int8_t)(x2), (int8_t)(y2) }

static const VgGlyphStrokeSeg g_hv_glyph_A[] = {VG_SEG(0, 8, 0, 2), VG_SEG(0, 2, 4, 0), VG_SEG(4, 0, 8, 2), VG_SEG(8, 2, 8, 8), VG_SEG(0, 5, 8, 5)};
static const VgGlyphStrokeSeg g_hv_glyph_B[] = {VG_SEG(0, 8, 0, 0), VG_SEG(0, 0, 5, 0), VG_SEG(5, 0, 7, 2), VG_SEG(7, 2, 5, 4), VG_SEG(5, 4, 0, 4), VG_SEG(6, 4, 8, 6), VG_SEG(8, 6, 6, 8), VG_SEG(6, 8, 0, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_C[] = {VG_SEG(8, 0, 0, 0), VG_SEG(0, 0, 0, 8), VG_SEG(0, 8, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_D[] = {VG_SEG(0, 8, 0, 0), VG_SEG(0, 0, 5, 0), VG_SEG(5, 0, 8, 3), VG_SEG(8, 3, 8, 5), VG_SEG(8, 5, 5, 8), VG_SEG(5, 8, 0, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_E[] = {VG_SEG(8, 0, 0, 0), VG_SEG(0, 0, 0, 8), VG_SEG(0, 8, 8, 8), VG_SEG(0, 4, 6, 4)};
static const VgGlyphStrokeSeg g_hv_glyph_F[] = {VG_SEG(8, 0, 0, 0), VG_SEG(0, 0, 0, 8), VG_SEG(0, 4, 6, 4)};
static const VgGlyphStrokeSeg g_hv_glyph_G[] = {VG_SEG(8, 0, 0, 0), VG_SEG(0, 0, 0, 8), VG_SEG(0, 8, 8, 8), VG_SEG(8, 8, 8, 5), VG_SEG(8, 5, 4, 5)};
static const VgGlyphStrokeSeg g_hv_glyph_H[] = {VG_SEG(0, 8, 0, 0), VG_SEG(8, 8, 8, 0), VG_SEG(0, 4, 8, 4)};
static const VgGlyphStrokeSeg g_hv_glyph_I[] = {VG_SEG(0, 8, 8, 8), VG_SEG(0, 0, 8, 0), VG_SEG(4, 8, 4, 0)};
static const VgGlyphStrokeSeg g_hv_glyph_J[] = {VG_SEG(8, 0, 8, 8), VG_SEG(8, 8, 4, 8), VG_SEG(4, 8, 0, 5)};
static const VgGlyphStrokeSeg g_hv_glyph_K[] = {VG_SEG(0, 8, 0, 0), VG_SEG(8, 0, 0, 4), VG_SEG(0, 4, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_L[] = {VG_SEG(0, 0, 0, 8), VG_SEG(0, 8, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_M[] = {VG_SEG(0, 8, 0, 0), VG_SEG(0, 0, 4, 3), VG_SEG(4, 3, 8, 0), VG_SEG(8, 0, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_N[] = {VG_SEG(0, 8, 0, 0), VG_SEG(0, 0, 8, 8), VG_SEG(8, 8, 8, 0)};
static const VgGlyphStrokeSeg g_hv_glyph_O[] = {VG_SEG(0, 8, 8, 8), VG_SEG(8, 8, 8, 0), VG_SEG(8, 0, 0, 0), VG_SEG(0, 0, 0, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_P[] = {VG_SEG(0, 8, 0, 0), VG_SEG(0, 0, 8, 0), VG_SEG(8, 0, 8, 4), VG_SEG(8, 4, 0, 4)};
static const VgGlyphStrokeSeg g_hv_glyph_Q[] = {VG_SEG(0, 8, 0, 0), VG_SEG(0, 0, 8, 0), VG_SEG(8, 0, 8, 5), VG_SEG(8, 5, 4, 8), VG_SEG(4, 8, 0, 8), VG_SEG(4, 5, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_R[] = {VG_SEG(0, 8, 0, 0), VG_SEG(0, 0, 8, 0), VG_SEG(8, 0, 8, 4), VG_SEG(8, 4, 0, 4), VG_SEG(0, 4, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_S[] = {VG_SEG(0, 8, 8, 8), VG_SEG(8, 8, 8, 4), VG_SEG(8, 4, 0, 4), VG_SEG(0, 4, 0, 0), VG_SEG(0, 0, 8, 0)};
static const VgGlyphStrokeSeg g_hv_glyph_T[] = {VG_SEG(0, 0, 8, 0), VG_SEG(4, 8, 4, 0)};
static const VgGlyphStrokeSeg g_hv_glyph_U[] = {VG_SEG(0, 0, 0, 8), VG_SEG(0, 8, 8, 8), VG_SEG(8, 8, 8, 0)};
static const VgGlyphStrokeSeg g_hv_glyph_V[] = {VG_SEG(0, 0, 4, 8), VG_SEG(4, 8, 8, 0)};
static const VgGlyphStrokeSeg g_hv_glyph_W[] = {VG_SEG(0, 0, 0, 8), VG_SEG(0, 8, 4, 5), VG_SEG(4, 5, 8, 8), VG_SEG(8, 8, 8, 0)};
static const VgGlyphStrokeSeg g_hv_glyph_X[] = {VG_SEG(0, 8, 8, 0), VG_SEG(0, 0, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_Y[] = {VG_SEG(0, 0, 4, 3), VG_SEG(4, 3, 8, 0), VG_SEG(4, 3, 4, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_Z[] = {VG_SEG(0, 0, 8, 0), VG_SEG(8, 0, 0, 8), VG_SEG(0, 8, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_0[] = {VG_SEG(0, 8, 8, 8), VG_SEG(8, 8, 8, 0), VG_SEG(8, 0, 0, 0), VG_SEG(0, 0, 0, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_1[] = {VG_SEG(0, 8, 8, 8), VG_SEG(4, 8, 4, 0), VG_SEG(4, 0, 2, 2)};
static const VgGlyphStrokeSeg g_hv_glyph_2[] = {VG_SEG(0, 0, 8, 0), VG_SEG(8, 0, 8, 4), VG_SEG(8, 4, 0, 4), VG_SEG(0, 4, 0, 8), VG_SEG(0, 8, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_3[] = {VG_SEG(0, 0, 8, 0), VG_SEG(8, 0, 8, 8), VG_SEG(8, 8, 0, 8), VG_SEG(0, 4, 8, 4)};
static const VgGlyphStrokeSeg g_hv_glyph_4[] = {VG_SEG(0, 0, 0, 4), VG_SEG(0, 4, 8, 4), VG_SEG(8, 0, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_5[] = {VG_SEG(0, 8, 8, 8), VG_SEG(8, 8, 8, 4), VG_SEG(8, 4, 0, 4), VG_SEG(0, 4, 0, 0), VG_SEG(0, 0, 8, 0)};
static const VgGlyphStrokeSeg g_hv_glyph_6[] = {VG_SEG(0, 0, 0, 8), VG_SEG(0, 8, 8, 8), VG_SEG(8, 8, 8, 4), VG_SEG(8, 4, 0, 4)};
static const VgGlyphStrokeSeg g_hv_glyph_7[] = {VG_SEG(0, 0, 8, 0), VG_SEG(8, 0, 8, 8)};
static const VgGlyphStrokeSeg g_hv_glyph_8[] = {VG_SEG(0, 8, 0, 0), VG_SEG(0, 0, 8, 0), VG_SEG(8, 0, 8, 8), VG_SEG(8, 8, 0, 8), VG_SEG(0, 4, 8, 4)};
static const VgGlyphStrokeSeg g_hv_glyph_9[] = {VG_SEG(8, 8, 8, 0), VG_SEG(8, 0, 0, 0), VG_SEG(0, 0, 0, 4), VG_SEG(0, 4, 8, 4)};

static const VgHvStrokeGlyph g_hv_letters[26] = {
    {10u, (uint8_t)(sizeof(g_hv_glyph_A) / sizeof(g_hv_glyph_A[0])), g_hv_glyph_A},
    {10u, (uint8_t)(sizeof(g_hv_glyph_B) / sizeof(g_hv_glyph_B[0])), g_hv_glyph_B},
    {10u, (uint8_t)(sizeof(g_hv_glyph_C) / sizeof(g_hv_glyph_C[0])), g_hv_glyph_C},
    {10u, (uint8_t)(sizeof(g_hv_glyph_D) / sizeof(g_hv_glyph_D[0])), g_hv_glyph_D},
    {10u, (uint8_t)(sizeof(g_hv_glyph_E) / sizeof(g_hv_glyph_E[0])), g_hv_glyph_E},
    {10u, (uint8_t)(sizeof(g_hv_glyph_F) / sizeof(g_hv_glyph_F[0])), g_hv_glyph_F},
    {10u, (uint8_t)(sizeof(g_hv_glyph_G) / sizeof(g_hv_glyph_G[0])), g_hv_glyph_G},
    {10u, (uint8_t)(sizeof(g_hv_glyph_H) / sizeof(g_hv_glyph_H[0])), g_hv_glyph_H},
    {10u, (uint8_t)(sizeof(g_hv_glyph_I) / sizeof(g_hv_glyph_I[0])), g_hv_glyph_I},
    {10u, (uint8_t)(sizeof(g_hv_glyph_J) / sizeof(g_hv_glyph_J[0])), g_hv_glyph_J},
    {10u, (uint8_t)(sizeof(g_hv_glyph_K) / sizeof(g_hv_glyph_K[0])), g_hv_glyph_K},
    {10u, (uint8_t)(sizeof(g_hv_glyph_L) / sizeof(g_hv_glyph_L[0])), g_hv_glyph_L},
    {10u, (uint8_t)(sizeof(g_hv_glyph_M) / sizeof(g_hv_glyph_M[0])), g_hv_glyph_M},
    {10u, (uint8_t)(sizeof(g_hv_glyph_N) / sizeof(g_hv_glyph_N[0])), g_hv_glyph_N},
    {10u, (uint8_t)(sizeof(g_hv_glyph_O) / sizeof(g_hv_glyph_O[0])), g_hv_glyph_O},
    {10u, (uint8_t)(sizeof(g_hv_glyph_P) / sizeof(g_hv_glyph_P[0])), g_hv_glyph_P},
    {10u, (uint8_t)(sizeof(g_hv_glyph_Q) / sizeof(g_hv_glyph_Q[0])), g_hv_glyph_Q},
    {10u, (uint8_t)(sizeof(g_hv_glyph_R) / sizeof(g_hv_glyph_R[0])), g_hv_glyph_R},
    {10u, (uint8_t)(sizeof(g_hv_glyph_S) / sizeof(g_hv_glyph_S[0])), g_hv_glyph_S},
    {10u, (uint8_t)(sizeof(g_hv_glyph_T) / sizeof(g_hv_glyph_T[0])), g_hv_glyph_T},
    {10u, (uint8_t)(sizeof(g_hv_glyph_U) / sizeof(g_hv_glyph_U[0])), g_hv_glyph_U},
    {10u, (uint8_t)(sizeof(g_hv_glyph_V) / sizeof(g_hv_glyph_V[0])), g_hv_glyph_V},
    {10u, (uint8_t)(sizeof(g_hv_glyph_W) / sizeof(g_hv_glyph_W[0])), g_hv_glyph_W},
    {10u, (uint8_t)(sizeof(g_hv_glyph_X) / sizeof(g_hv_glyph_X[0])), g_hv_glyph_X},
    {10u, (uint8_t)(sizeof(g_hv_glyph_Y) / sizeof(g_hv_glyph_Y[0])), g_hv_glyph_Y},
    {10u, (uint8_t)(sizeof(g_hv_glyph_Z) / sizeof(g_hv_glyph_Z[0])), g_hv_glyph_Z},
};

static const VgHvStrokeGlyph g_hv_digits[10] = {
    {10u, (uint8_t)(sizeof(g_hv_glyph_0) / sizeof(g_hv_glyph_0[0])), g_hv_glyph_0},
    {10u, (uint8_t)(sizeof(g_hv_glyph_1) / sizeof(g_hv_glyph_1[0])), g_hv_glyph_1},
    {10u, (uint8_t)(sizeof(g_hv_glyph_2) / sizeof(g_hv_glyph_2[0])), g_hv_glyph_2},
    {10u, (uint8_t)(sizeof(g_hv_glyph_3) / sizeof(g_hv_glyph_3[0])), g_hv_glyph_3},
    {10u, (uint8_t)(sizeof(g_hv_glyph_4) / sizeof(g_hv_glyph_4[0])), g_hv_glyph_4},
    {10u, (uint8_t)(sizeof(g_hv_glyph_5) / sizeof(g_hv_glyph_5[0])), g_hv_glyph_5},
    {10u, (uint8_t)(sizeof(g_hv_glyph_6) / sizeof(g_hv_glyph_6[0])), g_hv_glyph_6},
    {10u, (uint8_t)(sizeof(g_hv_glyph_7) / sizeof(g_hv_glyph_7[0])), g_hv_glyph_7},
    {10u, (uint8_t)(sizeof(g_hv_glyph_8) / sizeof(g_hv_glyph_8[0])), g_hv_glyph_8},
    {10u, (uint8_t)(sizeof(g_hv_glyph_9) / sizeof(g_hv_glyph_9[0])), g_hv_glyph_9},
};

static const VgHvStrokeGlyph *vg_hv_stroke_glyph_lookup(char c) {
    if (c >= 'A' && c <= 'Z') {
        return &g_hv_letters[(size_t)(c - 'A')];
    }
    if (c >= '0' && c <= '9') {
        return &g_hv_digits[(size_t)(c - '0')];
    }
    return NULL;
}

#undef VG_SEG

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

        const VgHvStrokeGlyph *hv_glyph = vg_hv_stroke_glyph_lookup(c);
        if (hv_glyph) {
            for (uint8_t seg_i = 0; seg_i < hv_glyph->segment_count; seg_i++) {
                const VgGlyphStrokeSeg *seg = &hv_glyph->segments[seg_i];
                GL(seg->x1, seg->y1, seg->x2, seg->y2);
            }
            adv = (int)hv_glyph->advance;
        } else {
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
                default:
                    GL(0, 0, 5, 0); GL(5, 0, 5, 10); GL(5, 10, 0, 10); GL(0, 10, 0, 0);
                    break;
            }
        }

        pen_x += adv;
        if (pen_x < 0) pen_x = 0;
#undef GL
#undef GLH
#undef GLCELLBOX
#undef DRAW_TEXT_SEGMENT
    }
}

static bool resolve_node_style(const VgNode *node, VgStyle *out_style) {
    if (!node || !out_style) {
        return false;
    }
    *out_style = node->style;
    if (out_style->stroke_width == 0) {
        out_style->stroke_width = 1;
    }
    return out_style->visible;
}

static void render_leaf_node(const VgNode *node, VgTransformFixed world_t, VgFrameBuffer *fb, VgStyle style) {
    if (!node || !fb) {
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

static void render_node(const VgNode *node, VgTransformFixed parent_t, VgFrameBuffer *fb) {
    if (!node || !fb) {
        return;
    }
    VgStyle style = {0};
    if (!resolve_node_style(node, &style)) {
        return;
    }
    VgTransformFixed node_t = parent_t;
    if (node->has_transform) {
        VgTransformFixed local_t = vg_transform_fixed_from_transform(node->transform);
        node_t = vg_transform_fixed_compose(parent_t, local_t);
    }
    if (node->type == VG_NODE_GROUP) {
        for (size_t i = 0; i < node->data.group.child_count; i++) {
            render_node(node->data.group.children[i], node_t, fb);
        }
        return;
    }
    render_leaf_node(node, node_t, fb, style);
}

static void render_node_with_world_transform(const VgNode *node, VgTransformFixed world_t, VgFrameBuffer *fb) {
    if (!node || !fb) {
        return;
    }
    VgStyle style = {0};
    if (!resolve_node_style(node, &style)) {
        return;
    }
    render_leaf_node(node, world_t, fb, style);
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

bool vg_render_slot_compute_redraw(const VgRenderSlot *slot,
                                   const VgRenderSlotState *state,
                                   uint32_t snapshot_id,
                                   bool force_render,
                                   VgClipRect *out_slot_rect,
                                   VgClipRect *out_dirty_rect) {
    if (!slot || !state) {
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
    if (!props_changed && !snapshot_changed && !force_render) {
        return false;
    }
    if (out_slot_rect) {
        *out_slot_rect = slot_rect;
    }
    if (out_dirty_rect) {
        VgClipRect dirty_rect = slot_rect;
        if (state->initialized) {
            VgClipRect prev_rect = vg_clip_rect_expand(state->last_clip_rect, state->last_guard_px);
            dirty_rect = vg_clip_rect_union(prev_rect, slot_rect);
        }
        *out_dirty_rect = dirty_rect;
    }
    return true;
}

bool vg_render_slot_if_changed(const VgRenderSlot *slot,
                               VgRenderSlotState *state,
                               VgFrameBuffer *fb,
                               uint32_t snapshot_id) {
    if (!slot || !state || !fb) {
        return false;
    }
    VgClipRect slot_rect = {0};
    VgClipRect dirty_rect = {0};
    if (!vg_render_slot_compute_redraw(slot, state, snapshot_id, false, &slot_rect, &dirty_rect)) {
        return false;
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
