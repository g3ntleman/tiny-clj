#include "render_backend.h"

/**
 * @brief Starts one backend output frame.
 *
 * The backend surface is intentionally tiny so host and embedded targets can
 * share the same renderer/backend boundary: begin a frame, submit clipped
 * dirty rects, then finish the frame. Missing callbacks are treated as
 * successful no-ops so callers keep one simple control flow.
 *
 * @param backend Backend descriptor.
 * @param frame_id Monotonic renderer frame id.
 * @return true when the backend accepts frame begin.
 */
bool vg_backend_begin_frame(const VgBackend *backend, uint32_t frame_id) {
    if (!backend || !backend->ops || !backend->ops->begin_frame) {
        return true;
    }
    return backend->ops->begin_frame(backend->ctx, frame_id);
}

/**
 * @brief Submits one clipped framebuffer rect to the backend.
 *
 * The helper clips @p rect against the framebuffer bounds and forwards the
 * resulting pixel window as pointer + stride without extra allocation or copy.
 *
 * @param backend Backend descriptor.
 * @param fb Source framebuffer that owns the submitted pixels.
 * @param rect Dirty rect in framebuffer coordinates.
 * @return true when the rect is empty or successfully submitted.
 */
bool vg_backend_submit_clip_rect(const VgBackend *backend,
                                 const VgFrameBuffer *fb,
                                 VgClipRect rect) {
    if (!backend || !backend->ops || !backend->ops->submit_rect || !fb || !fb->pixels) {
        return false;
    }

    VgClipRect fb_rect = {0, 0, (int16_t)fb->width, (int16_t)fb->height};
    VgClipRect clipped = {0};
    if (!vg_clip_rect_intersect(rect, fb_rect, &clipped)) {
        return true;
    }

    size_t row0 = (size_t)clipped.y * (size_t)fb->width;
    size_t col0 = (size_t)clipped.x;
    const uint16_t *pixels = fb->pixels + row0 + col0;
    VgBackendRect backend_rect = {
        .x = clipped.x,
        .y = clipped.y,
        .w = clipped.w,
        .h = clipped.h,
    };
    return backend->ops->submit_rect(backend->ctx, backend_rect, pixels, (uint16_t)fb->width);
}

/**
 * @brief Finishes one backend output frame.
 *
 * @param backend Backend descriptor.
 * @param frame_id Monotonic renderer frame id.
 * @return true when the backend accepts frame end.
 */
bool vg_backend_end_frame(const VgBackend *backend, uint32_t frame_id) {
    if (!backend || !backend->ops || !backend->ops->end_frame) {
        return true;
    }
    return backend->ops->end_frame(backend->ctx, frame_id);
}
