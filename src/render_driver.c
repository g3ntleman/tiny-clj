#include "render_driver.h"

#include "memory.h"
#include "platform.h"

#include <string.h>

#define VG_RENDER_DRIVER_ANIMATED_WAIT_MS 16u

static bool render_driver_config_is_valid(const VgRenderDriver *driver) {
    return driver && driver->panel && driver->stripe_pixels &&
           driver->display_width > 0 && driver->display_height > 0;
}

static int16_t render_driver_effective_stripe_rows(const VgRenderDriver *driver) {
    if (!driver) {
        return 0;
    }
    int16_t rows = driver->stripe_rows;
    if (rows <= 0 || rows > driver->display_height) {
        rows = driver->display_height;
    }
    return rows;
}

static bool render_driver_dirty_clamp(const VgRenderDriver *driver,
                                      VgClipRect dirty,
                                      VgClipRect *out_clipped) {
    if (!driver || !out_clipped) {
        return false;
    }
    VgClipRect display_rect = {
        .x = 0,
        .y = 0,
        .w = driver->display_width,
        .h = driver->display_height,
    };
    return vg_clip_rect_intersect(dirty, display_rect, out_clipped);
}

static bool render_driver_ensure_slot_buffers(VgRenderDriver *driver) {
    if (!driver || driver->slot_count == 0u) {
        return true;
    }
    if (driver->slot_states && driver->seen_generations) {
        return true;
    }
    if (driver->slot_states || driver->seen_generations) {
        return false;
    }

    driver->slot_states = (VgRenderSlotState *)CLJ_CALLOC(driver->slot_count,
                                                          sizeof(VgRenderSlotState));
    driver->seen_generations = (uint32_t *)CLJ_CALLOC(driver->slot_count,
                                                      sizeof(uint32_t));
    if (!driver->slot_states || !driver->seen_generations) {
        CLJ_FREE(driver->slot_states);
        CLJ_FREE(driver->seen_generations);
        driver->slot_states = NULL;
        driver->seen_generations = NULL;
        return false;
    }
    driver->owns_slot_state_buffers = true;
    return true;
}

static void render_driver_release_slot_buffers(VgRenderDriver *driver) {
    if (!driver || !driver->owns_slot_state_buffers) {
        return;
    }
    CLJ_FREE(driver->slot_states);
    CLJ_FREE(driver->seen_generations);
    driver->slot_states = NULL;
    driver->seen_generations = NULL;
    driver->owns_slot_state_buffers = false;
}

bool vg_render_driver_bind_runtime(VgRenderDriver *driver,
                                   VgSlotChangeTracker *tracker,
                                   const ID *scene_snapshots,
                                   uint16_t background_color) {
    if (!driver) {
        return false;
    }
    driver->tracker = tracker;
    driver->scene_snapshots = scene_snapshots;
    driver->background_color = background_color;
    return true;
}

/**
 * @brief Renders one dirty rectangle in vertical stripes and pushes each stripe to the panel.
 */
bool vg_render_driver_stripe_push(VgRenderDriver *driver,
                                  const VgNode *scene_root,
                                  VgClipRect dirty,
                                  uint32_t now_ms,
                                  uint16_t bg_color) {
    (void)now_ms;

    if (!render_driver_config_is_valid(driver)) {
        return false;
    }

    VgClipRect clipped = {0};
    if (!render_driver_dirty_clamp(driver, dirty, &clipped)) {
        return true;
    }

    int16_t stripe_rows = render_driver_effective_stripe_rows(driver);
    if (stripe_rows <= 0) {
        return false;
    }

    int32_t y_begin = clipped.y;
    int32_t y_end = clipped.y + clipped.h;
    for (int32_t stripe_y = y_begin; stripe_y < y_end; stripe_y += stripe_rows) {
        int16_t stripe_h = (int16_t)(y_end - stripe_y);
        if (stripe_h > stripe_rows) {
            stripe_h = stripe_rows;
        }

        VgFrameBuffer stripe_fb = {0};
        if (!vg_framebuffer_init(&stripe_fb,
                                 driver->display_width,
                                 stripe_h,
                                 driver->stripe_pixels,
                                 (size_t)driver->display_width * (size_t)stripe_h)) {
            return false;
        }

        vg_framebuffer_clear(&stripe_fb, bg_color);

        if (scene_root) {
            VgNode *children[1] = {(VgNode *)scene_root};
            VgNode shifted_root = {
                .id = 0u,
                .type = VG_NODE_GROUP,
                .has_transform = true,
                .transform = vg_transform_identity(),
                .style = vg_style_default(),
                .data.group = {
                    .children = children,
                    .child_count = 1u,
                },
            };
            shifted_root.transform.ty = (int16_t)(-stripe_y);

            VgClipRect stripe_clip = {
                .x = clipped.x,
                .y = 0,
                .w = clipped.w,
                .h = stripe_h,
            };
            vg_render_scene_clipped(&shifted_root, &stripe_fb, stripe_clip);
        }

        if (!vg_panel_write_bitmap_2d(driver->panel,
                                      clipped.x,
                                      (int16_t)stripe_y,
                                      (int16_t)(clipped.x + clipped.w),
                                      (int16_t)(stripe_y + stripe_h),
                                      driver->stripe_pixels,
                                      (uint16_t)driver->display_width,
                                      (uint16_t)stripe_h,
                                      clipped.x,
                                      0,
                                      (int16_t)(clipped.x + clipped.w),
                                      stripe_h)) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Runs one render-loop iteration (wait for slot changes, render changed snapshots).
 */
int vg_render_driver_step(VgRenderDriver *driver,
                          VgSlotChangeTracker *tracker,
                          const ID *scene_snapshots) {
    if (!driver) {
        return -1;
    }
    if (tracker) {
        driver->tracker = tracker;
    }
    if (scene_snapshots) {
        driver->scene_snapshots = scene_snapshots;
    }

    if (!render_driver_config_is_valid(driver) ||
        !driver->tracker ||
        !driver->scene_snapshots ||
        driver->slot_count == 0u ||
        driver->slot_count > VG_SLOT_CHANGE_TRACKER_MAX_SLOTS) {
        tread_sleep_ms(1u);
        return 0;
    }
    if (!render_driver_ensure_slot_buffers(driver)) {
        return -1;
    }

    uint32_t generations[VG_SLOT_CHANGE_TRACKER_MAX_SLOTS] = {0};
    uint32_t timeout_ms = (driver->animated_slots_mask != 0u)
                              ? VG_RENDER_DRIVER_ANIMATED_WAIT_MS
                              : UINT32_MAX;
    uint32_t changed_mask = vg_slot_change_tracker_wait_for_changes(driver->tracker,
                                                                    driver->seen_generations,
                                                                    generations,
                                                                    timeout_ms);
    if (changed_mask == 0u && driver->animated_slots_mask == 0u) {
        return 0;
    }

    uint32_t now_ms = platform_current_time_ms();
    VgClipRect display_rect = {
        .x = 0,
        .y = 0,
        .w = driver->display_width,
        .h = driver->display_height,
    };

    int rendered_slots = 0;
    for (uint8_t i = 0; i < driver->slot_count; i++) {
        bool changed = ((changed_mask & (1u << i)) != 0u);
        bool animated = ((driver->animated_slots_mask & (1u << i)) != 0u);
        if (!changed && !animated) {
            continue;
        }

        const VgNode *scene_root = (const VgNode *)driver->scene_snapshots[i];
        if (!scene_root) {
            continue;
        }

        if (vg_render_driver_stripe_push(driver,
                                         scene_root,
                                         display_rect,
                                         now_ms,
                                         driver->background_color)) {
            rendered_slots++;
            driver->slot_states[i].initialized = true;
            driver->slot_states[i].has_animation = false;
            driver->slot_states[i].snapshot_id = generations[i];
            driver->slot_states[i].last_clip_rect = display_rect;
            driver->slot_states[i].last_visible = true;
            driver->slot_states[i].last_opaque = false;
            driver->slot_states[i].last_clear_color = driver->background_color;
            driver->slot_states[i].last_guard_px = 0u;
        }
    }

    memcpy(driver->seen_generations,
           generations,
           (size_t)driver->slot_count * sizeof(driver->seen_generations[0]));
    driver->animated_slots_mask = 0u;
    return rendered_slots;
}

static void render_driver_thread_main(void *arg) {
    VgRenderDriver *driver = (VgRenderDriver *)arg;
    if (!driver) {
        return;
    }
    while (driver->running) {
        size_t stack_hwm = tread_stack_high_water_mark_bytes(driver->thread);
        if (stack_hwm > 0u &&
            (driver->stack_high_water_mark_bytes == 0u ||
             stack_hwm < driver->stack_high_water_mark_bytes)) {
            driver->stack_high_water_mark_bytes = stack_hwm;
        }
        (void)vg_render_driver_step(driver, NULL, NULL);
    }
}

/**
 * @brief Starts the render thread for one driver instance.
 */
bool vg_render_driver_start(VgRenderDriver *driver) {
    if (!driver || !render_driver_config_is_valid(driver) || driver->thread) {
        return false;
    }

    SubjectiveCThreadConfig config = {
        .name = driver->thread_name,
        .stack_bytes = driver->thread_stack_bytes,
        .priority = driver->thread_priority,
    };

    driver->stack_high_water_mark_bytes = 0u;
    driver->running = true;
    driver->thread = tread_create(render_driver_thread_main,
                                                driver,
                                                &config);
    if (!driver->thread) {
        driver->running = false;
        return false;
    }
    return true;
}

/**
 * @brief Stops the render thread and releases driver-owned runtime buffers.
 */
bool vg_render_driver_stop(VgRenderDriver *driver) {
    if (!driver) {
        return false;
    }
    if (!driver->thread) {
        driver->running = false;
        render_driver_release_slot_buffers(driver);
        return true;
    }

    driver->running = false;
    if (driver->tracker && driver->tracker->slot_count > 0u) {
        (void)vg_slot_change_tracker_publish(driver->tracker, 0u, NULL);
    }

    bool joined = tread_join(driver->thread);
    tread_destroy(driver->thread);
    driver->thread = NULL;

    render_driver_release_slot_buffers(driver);
    return joined;
}
