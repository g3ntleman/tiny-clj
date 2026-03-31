#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdatomic.h>
#include <time.h>
#include <sched.h>
#include <errno.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#include <mach/mach_init.h>
#endif

#if defined(TINYCLJ_WITH_MINIFB)
#include "vector_scene_graph.h"
#include "scene.h"
#include "exception.h"
#include "tiny_fx_gfx.h"
#include "runtime.h"
#include "event_loop.h"
#include "renderer_lifecycle.h"
#include "tiny_fx_host_app.h"
#include "rendered_state_snapshot.h"
#include "panel.h"
#include "panel_backend.h"
#include "fx_host_runloop.h"
#include "fx_config_loader.h"
#include "fx_spatial_bridge.h"
#include "fx_timeline_ingress.h"
#include "platform.h"
#include "gpio.h"
#include "memory.h"
#if !defined(__APPLE__)
#include "MiniFB.h"
#endif
#if defined(__APPLE__)
#include "game_demo_macos_menu.h"
#include "tiny_fx_macos_app.h"
#endif

static bool fx_perf_stderr_diag_enabled(void) {
    const char *v = getenv("TINYCLJ_FX_PERF_DIAG");
    return v && v[0] != '\0' && v[0] != '0';
}

#define VIEW_W 320
#define VIEW_H 240
#define VIEW_DEFAULT_WINDOW_SCALE 2u
#define FX_MAX_SLOTS VG_RENDERED_STATE_MAX_SLOTS
#define TARGET_FPS              60u
#define SCENE_ERASE_COLOR       0x0000u
#define RGB565_BYTES_PER_PIXEL 2u
#define FX_ANIMATED_WAIT_TIMEOUT_MS 8u
#define FX_DIRTY_BUFFER_BUDGET_BYTES (20u * 1024u)
#define FX_DIRTY_PIXEL_BUDGET (FX_DIRTY_BUFFER_BUDGET_BYTES / RGB565_BYTES_PER_PIXEL)
#define FX_MAX_DIRTY_PLAN_RECTS (FX_MAX_SLOTS * 16u)

static uint64_t monotonic_now_ns(void);

typedef struct {
    int key;
    int32_t pin;
    bool active_low;
} ViewerGpioKeyBinding;

static const ViewerGpioKeyBinding g_fx_gpio_key_bindings[] = {
    {KB_KEY_1, 1, false}, {KB_KEY_2, 2, false}, {KB_KEY_3, 3, false}, {KB_KEY_4, 4, false},
    {KB_KEY_5, 5, false}, {KB_KEY_6, 6, false}, {KB_KEY_7, 7, false}, {KB_KEY_8, 8, false},
    {KB_KEY_9, 9, false}, {KB_KEY_0, 0, false},
    {KB_KEY_SPACE, 13, true}, {KB_KEY_ENTER, 13, true},
    {KB_KEY_Y, 15, true},
    {KB_KEY_LEFT, 14, true}, {KB_KEY_RIGHT, 12, true}, {KB_KEY_UP, 26, true}, {KB_KEY_DOWN, 27, true},
};

enum {
    FX_GPIO_KEY_BINDING_COUNT =
        (int)(sizeof(g_fx_gpio_key_bindings) / sizeof(g_fx_gpio_key_bindings[0]))
};

static void fx_host_app_signal_handler(int signo) {
    fprintf(stderr, "tiny-fx host signal %d\n", signo);
    exception_print_native_backtrace_symbolized();
    _exit(128 + signo);
}

static void fx_host_app_install_signal_handlers(void) {
    signal(SIGTRAP, fx_host_app_signal_handler);
    signal(SIGABRT, fx_host_app_signal_handler);
    signal(SIGSEGV, fx_host_app_signal_handler);
    signal(SIGBUS, fx_host_app_signal_handler);
}

/* Handles immediate viewer exit shortcuts. */
static bool fx_should_exit_for_keys(const uint8_t *keys) {
    if (!keys) {
        return false;
    }
    bool esc = keys[MFB_KB_KEY_ESCAPE] != 0;
    bool cmd_q = (keys[MFB_KB_KEY_Q] != 0) &&
                 ((keys[MFB_KB_KEY_LEFT_SUPER] != 0) || (keys[MFB_KB_KEY_RIGHT_SUPER] != 0));
    return esc || cmd_q;
}


typedef struct {
    bool use_mfb_waitsync;
    bool w_key_was_down;
    bool r_key_was_down;
    bool redraw_overlay_enabled;
    bool gpio_key_was_down[FX_GPIO_KEY_BINDING_COUNT];
} ViewerRuntimeFlags;

typedef struct {
    uint32_t dirty_pixels;
    uint32_t changed_slots;
    uint32_t transfer_rects;
    uint64_t transfer_ns;
    uint_fast32_t frame_serial;
} ViewerFrameRenderResult;

static bool fx_key_pressed_once(const uint8_t *keys, int key, bool *was_down) {
    if (!was_down) {
        return false;
    }
    bool down = keys && keys[key] != 0;
    bool pressed = down && !(*was_down);
    *was_down = down;
    return pressed;
}

static int32_t fx_gpio_level_for_binding(const ViewerGpioKeyBinding *binding, bool down) {
    if (!binding) {
        return down ? 1 : 0;
    }
    if (binding->active_low) {
        return down ? 0 : 1;
    }
    return down ? 1 : 0;
}

static void fx_seed_gpio_key_levels(void) {
    for (size_t i = 0; i < (sizeof(g_fx_gpio_key_bindings) / sizeof(g_fx_gpio_key_bindings[0])); i++) {
        const ViewerGpioKeyBinding *binding = &g_fx_gpio_key_bindings[i];
        gpio_runtime_store_digital_level(binding->pin, fx_gpio_level_for_binding(binding, false));
    }
}

static void fx_update_runtime_flags(const uint8_t *keys,
                                        ViewerRuntimeFlags *flags,
                                        uint64_t *next_frame_deadline_ns,
                                        uint64_t target_frame_ns) {
    if (!flags || !next_frame_deadline_ns) {
        return;
    }
    if (fx_key_pressed_once(keys, KB_KEY_W, &flags->w_key_was_down)) {
        flags->use_mfb_waitsync = !flags->use_mfb_waitsync;
        *next_frame_deadline_ns = monotonic_now_ns() + target_frame_ns;
    }
}

static void fx_simulate_gpio_keys(const uint8_t *keys, ViewerRuntimeFlags *flags) {
    if (!flags) {
        return;
    }

    for (size_t i = 0; i < (sizeof(g_fx_gpio_key_bindings) / sizeof(g_fx_gpio_key_bindings[0])); i++) {
        const ViewerGpioKeyBinding *binding = &g_fx_gpio_key_bindings[i];
        bool down = keys && keys[binding->key] != 0;
        if (down == flags->gpio_key_was_down[i]) {
            continue;
        }
        flags->gpio_key_was_down[i] = down;
        (void)gpio_simulate_digital(binding->pin, fx_gpio_level_for_binding(binding, down));
    }
}

/* Expand RGB565 framebuffer pixels to MiniFB's XRGB8888 format. */
static uint32_t rgb565_to_xrgb8888(uint16_t c) {
    uint32_t r = (uint32_t)((((c >> 11) & 0x1f) * 255) / 31);
    uint32_t g = (uint32_t)((((c >> 5) & 0x3f) * 255) / 63);
    uint32_t b = (uint32_t)(((c & 0x1f) * 255) / 31);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static void fx_draw_rect_outline_xrgb8888(uint32_t *pixels,
                                              int width,
                                              int height,
                                              VgClipRect rect,
                                              uint32_t color) {
    if (!pixels || width <= 0 || height <= 0 || vg_clip_rect_is_empty(rect)) {
        return;
    }
    int x0 = (rect.x < 0) ? 0 : rect.x;
    int y0 = (rect.y < 0) ? 0 : rect.y;
    int x1 = rect.x + rect.w - 1;
    int y1 = rect.y + rect.h - 1;
    if (x1 >= width) {
        x1 = width - 1;
    }
    if (y1 >= height) {
        y1 = height - 1;
    }
    if (x1 < x0 || y1 < y0) {
        return;
    }
    for (int x = x0; x <= x1; x++) {
        pixels[(size_t)y0 * (size_t)width + (size_t)x] = color;
        pixels[(size_t)y1 * (size_t)width + (size_t)x] = color;
    }
    for (int y = y0; y <= y1; y++) {
        pixels[(size_t)y * (size_t)width + (size_t)x0] = color;
        pixels[(size_t)y * (size_t)width + (size_t)x1] = color;
    }
}

static void fx_draw_redraw_overlay(uint32_t *pixels,
                                       int width,
                                       int height,
                                       const VgClipRect *rects,
                                       size_t rect_count) {
    if (!pixels || !rects || rect_count == 0u) {
        return;
    }
    const uint32_t color = 0xff00ffffu; /* cyan */
    for (size_t i = 0; i < rect_count; i++) {
        fx_draw_rect_outline_xrgb8888(pixels, width, height, rects[i], color);
    }
}

#if defined(__APPLE__)
typedef TinyFxMacosWindow ViewerHostWindow;
typedef ViewerHostWindow *(*ViewerHostWindowOpenFn)(const char *title, unsigned width, unsigned height);
typedef void (*ViewerHostWindowCloseFn)(ViewerHostWindow *window);
#else
typedef struct mfb_window ViewerHostWindow;
#endif

#if defined(__APPLE__)
static ViewerHostWindow *fx_host_window_open_with_backend(ViewerHostWindowOpenFn open_window_fn,
                                                              const char *title,
                                                              unsigned width,
                                                              unsigned height) {
    if (!open_window_fn) {
        return NULL;
    }
    ViewerHostWindow *window = open_window_fn(title, width, height);
    if (!window) {
        return NULL;
    }
    macos_fx_start_runloop_watchdog();
    return window;
}
#endif

static ViewerHostWindow *fx_host_window_open(const char *title,
                                                 unsigned width,
                                                 unsigned height) {
#if defined(__APPLE__)
    return fx_host_window_open_with_backend(tinyfx_macos_window_open, title, width, height);
#else
    return mfb_open_ex(title, width, height, 0u);
#endif
}

#if defined(__APPLE__)
static void fx_host_window_close_with_backend(ViewerHostWindow *window,
                                                  ViewerHostWindowCloseFn close_window_fn) {
    if (!window || !close_window_fn) {
        return;
    }
    macos_fx_stop_runloop_watchdog();
    close_window_fn(window);
}
#endif

static void fx_host_window_close(ViewerHostWindow *window) {
#if defined(__APPLE__)
    fx_host_window_close_with_backend(window, tinyfx_macos_window_close);
#else
    mfb_close(window);
#endif
}

static void fx_host_window_show_cursor(ViewerHostWindow *window, bool show) {
#if defined(__APPLE__)
    tinyfx_macos_window_show_cursor(window, show);
#else
    mfb_show_cursor(window, show);
#endif
}

static bool fx_host_window_set_viewport(ViewerHostWindow *window,
                                            unsigned offset_x,
                                            unsigned offset_y,
                                            unsigned width,
                                            unsigned height) {
#if defined(__APPLE__)
    return tinyfx_macos_window_set_viewport(window, offset_x, offset_y, width, height);
#else
    return mfb_set_viewport(window, offset_x, offset_y, width, height);
#endif
}

static const uint8_t *fx_host_window_get_key_buffer(ViewerHostWindow *window) {
#if defined(__APPLE__)
    return tinyfx_macos_window_get_key_buffer(window);
#else
    return mfb_get_key_buffer(window);
#endif
}

static bool fx_host_window_pump_events(ViewerHostWindow *window) {
#if defined(__APPLE__)
    return tinyfx_macos_window_pump_events(window);
#else
    return mfb_update_events(window) == MFB_STATE_OK;
#endif
}

static bool fx_host_window_wait_sync(ViewerHostWindow *window) {
#if defined(__APPLE__)
    return tinyfx_macos_window_wait_sync(window);
#else
    return mfb_wait_sync(window);
#endif
}

static mfb_update_state fx_host_window_update(ViewerHostWindow *window,
                                                  const uint32_t *buffer,
                                                  unsigned width,
                                                  unsigned height) {
#if defined(__APPLE__)
    return tinyfx_macos_window_update(window, buffer, width, height);
#else
    return mfb_update_ex(window, (void *)buffer, width, height);
#endif
}

static uint32_t fx_clip_rect_area_on_framebuffer(VgClipRect rect, int fb_w, int fb_h) {
    if (vg_clip_rect_is_empty(rect) || fb_w <= 0 || fb_h <= 0) {
        return 0u;
    }
    int x0 = (rect.x < 0) ? 0 : rect.x;
    int y0 = (rect.y < 0) ? 0 : rect.y;
    int x1 = rect.x + rect.w;
    int y1 = rect.y + rect.h;
    if (x1 > fb_w) {
        x1 = fb_w;
    }
    if (y1 > fb_h) {
        y1 = fb_h;
    }
    if (x1 <= x0 || y1 <= y0) {
        return 0u;
    }
    return (uint32_t)((x1 - x0) * (y1 - y0));
}

typedef struct {
    double window_start_s;
    uint64_t window_frames;
    uint64_t window_dirty_pixels;
    uint64_t window_changed_slots;
    uint64_t window_transfer_rects;
    uint64_t window_transfer_ns;
    uint32_t max_dirty_px_frame;
} ViewerPerfWindow;

typedef struct {
    double fps;
    double avg_dirty_px_per_frame;
    double dirty_ratio;
    double dirty_bytes_per_s;
    double full_bytes_per_s;
    double avg_changed_slots;
    double transfer_rects_per_s;
    double avg_transfer_rects_per_frame;
    double avg_transfer_ms_per_frame;
    double max_dirty_bytes_per_frame;
    double avg_render_lock_hold_us;
    double max_render_lock_hold_us;
    uint64_t skipped_generations;
    uint64_t skipped_max_frame;
    uint64_t skipped_max_slot;
} ViewerPerfSnapshot;

typedef struct {
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t sum_ns;
    uint32_t count;
} TimingAccumulator;


static void timing_accumulator_reset(TimingAccumulator *acc) {
    if (!acc) {
        return;
    }
    acc->min_ns = UINT64_MAX;
    acc->max_ns = 0u;
    acc->sum_ns = 0u;
    acc->count = 0u;
}

static void timing_accumulator_add(TimingAccumulator *acc, uint64_t sample_ns) {
    if (!acc) {
        return;
    }
    acc->sum_ns += sample_ns;
    acc->count++;
    if (sample_ns < acc->min_ns) {
        acc->min_ns = sample_ns;
    }
    if (sample_ns > acc->max_ns) {
        acc->max_ns = sample_ns;
    }
}

static double timing_accumulator_avg_ms(const TimingAccumulator *acc) {
    if (!acc || acc->count == 0u) {
        return 0.0;
    }
    return (double)acc->sum_ns / (double)acc->count / 1e6;
}

static double timing_accumulator_max_ms(const TimingAccumulator *acc) {
    if (!acc || acc->max_ns == 0u) {
        return 0.0;
    }
    return (double)acc->max_ns / 1e6;
}

static double bytes_per_second_to_mib_per_second(double bytes_per_second) {
    if (bytes_per_second <= 0.0) {
        return 0.0;
    }
    return bytes_per_second / (1024.0 * 1024.0);
}

/* Initialize rolling perf window counters for throughput estimation. */
static void perf_window_init(ViewerPerfWindow *perf, double start_s) {
    if (!perf) {
        return;
    }
    memset(perf, 0, sizeof(*perf));
    perf->window_start_s = start_s;
}

/* Accumulate dirty-area and slot-change stats for one rendered frame. */
static void perf_window_record_frame(ViewerPerfWindow *perf,
                                     uint32_t dirty_pixels,
                                     uint32_t changed_slots,
                                     uint32_t transfer_rects,
                                     uint64_t transfer_ns) {
    if (!perf) {
        return;
    }
    perf->window_frames++;
    perf->window_dirty_pixels += dirty_pixels;
    perf->window_changed_slots += changed_slots;
    perf->window_transfer_rects += transfer_rects;
    perf->window_transfer_ns += transfer_ns;
    if (dirty_pixels > perf->max_dirty_px_frame) {
        perf->max_dirty_px_frame = dirty_pixels;
    }
}

/* Emit one-second rolling perf snapshot and reset counters. */
static bool perf_window_take_snapshot_if_due(ViewerPerfWindow *perf,
                                              double now_s,
                                              ViewerPerfSnapshot *out_snapshot) {
    if (out_snapshot) {
        memset(out_snapshot, 0, sizeof(*out_snapshot));
    }
    if (!perf) {
        return false;
    }
    double elapsed_s = now_s - perf->window_start_s;
    if (elapsed_s < 1.0) {
        return false;
    }
    if (out_snapshot) {
        if (perf->window_frames > 0u) {
            out_snapshot->fps = (double)perf->window_frames / elapsed_s;
            out_snapshot->avg_dirty_px_per_frame = (double)perf->window_dirty_pixels / (double)perf->window_frames;
            out_snapshot->dirty_ratio = out_snapshot->avg_dirty_px_per_frame / (double)(VIEW_W * VIEW_H);
            out_snapshot->dirty_bytes_per_s =
                ((double)perf->window_dirty_pixels * (double)RGB565_BYTES_PER_PIXEL) / elapsed_s;
            out_snapshot->full_bytes_per_s = out_snapshot->fps * (double)(VIEW_W * VIEW_H * RGB565_BYTES_PER_PIXEL);
            out_snapshot->avg_changed_slots = (double)perf->window_changed_slots / (double)perf->window_frames;
            out_snapshot->transfer_rects_per_s = (double)perf->window_transfer_rects / elapsed_s;
            out_snapshot->avg_transfer_rects_per_frame =
                (double)perf->window_transfer_rects / (double)perf->window_frames;
            out_snapshot->avg_transfer_ms_per_frame =
                ((double)perf->window_transfer_ns / (double)perf->window_frames) / 1e6;
            out_snapshot->max_dirty_bytes_per_frame =
                (double)perf->max_dirty_px_frame * (double)RGB565_BYTES_PER_PIXEL;
        }
    }
    perf->window_start_s = now_s;
    perf->window_frames = 0u;
    perf->window_dirty_pixels = 0u;
    perf->window_changed_slots = 0u;
    perf->window_transfer_rects = 0u;
    perf->window_transfer_ns = 0u;
    perf->max_dirty_px_frame = 0u;
    return true;
}

static ID *g_scene_slot_snapshots = NULL;
static uint8_t *g_slot_render_priority = NULL;
static uint8_t g_fx_slot_count = 0u;
static VgSlotChangeTracker g_slot_change_tracker;

/*
 * Two-buffer model matching ESP32 SPI/I80 hardware:
 *
 *   g_render_buffer  = MCU-local render target (private to render thread)
 *   g_gram_pixels    = display GRAM (read by UI thread for presentation)
 *
 * The render thread erases + draws into g_render_buffer. After rendering is
 * complete, the dirty region is copied to g_gram_pixels — simulating the
 * SPI/DMA transfer from MCU RAM to the display's internal GRAM.
 *
 * The UI thread only reads g_gram_pixels, so it never sees the intermediate
 * erased state of g_render_buffer. Each pixel in the GRAM transitions
 * directly from old -> new, matching real SPI display behavior.
 */
static uint16_t g_render_buffer[VIEW_W * VIEW_H];
static uint16_t *g_gram_pixels = NULL;

typedef struct {
    uint16_t *gram_pixels;
    uint16_t width;
    uint16_t height;
    bool display_enabled;
    bool sleeping;
} ViewerGramPanel;

static ViewerGramPanel g_gram_panel_ctx = {0};
static VgPanel g_gram_panel = {0};

static bool fx_panel_reset(void *ctx) {
    ViewerGramPanel *panel = (ViewerGramPanel *)ctx;
    if (!panel) {
        return false;
    }
    panel->display_enabled = false;
    panel->sleeping = false;
    return true;
}

static bool fx_panel_init(void *ctx) {
    (void)ctx;
    return true;
}

static bool fx_panel_set_orientation(void *ctx, bool mirror_x, bool mirror_y, bool swap_xy) {
    (void)ctx;
    (void)mirror_x;
    (void)mirror_y;
    (void)swap_xy;
    return true;
}

static bool fx_panel_set_gap(void *ctx, int16_t x_gap, int16_t y_gap) {
    (void)ctx;
    (void)x_gap;
    (void)y_gap;
    return true;
}

static bool fx_panel_write_bitmap(void *ctx,
                                      int16_t x_start,
                                      int16_t y_start,
                                      int16_t x_end,
                                      int16_t y_end,
                                      const uint16_t *rgb565_pixels) {
    ViewerGramPanel *panel = (ViewerGramPanel *)ctx;
    VgPanelBitmapView view = {0};
    int16_t rect_w = (int16_t)(x_end - x_start);
    int16_t rect_h = (int16_t)(y_end - y_start);
    if (!panel || !panel->gram_pixels || !rgb565_pixels || rect_w <= 0 || rect_h <= 0) {
        return false;
    }
    view.pixels = rgb565_pixels;
    view.stride_px = (uint16_t)rect_w;
    view.width = rect_w;
    view.height = rect_h;
    return vg_panel_rgb565_blit(panel->gram_pixels, panel->width, panel->height, x_start, y_start, &view);
}

static bool fx_panel_write_bitmap_2d(void *ctx,
                                         int16_t x_start,
                                         int16_t y_start,
                                         int16_t x_end,
                                         int16_t y_end,
                                         const uint16_t *src_pixels,
                                         uint16_t src_w,
                                         uint16_t src_h,
                                         int16_t src_x_start,
                                         int16_t src_y_start,
                                         int16_t src_x_end,
                                         int16_t src_y_end) {
    ViewerGramPanel *panel = (ViewerGramPanel *)ctx;
    VgPanelBitmapView view = {0};
    if (!panel || !panel->gram_pixels || !src_pixels) {
        return false;
    }
    if ((int16_t)(x_end - x_start) != (int16_t)(src_x_end - src_x_start) ||
        (int16_t)(y_end - y_start) != (int16_t)(src_y_end - src_y_start)) {
        return false;
    }
    if (!vg_panel_bitmap_view_init(src_pixels,
                                   src_w,
                                   src_h,
                                   src_x_start,
                                   src_y_start,
                                   src_x_end,
                                   src_y_end,
                                   &view)) {
        return false;
    }
    return vg_panel_rgb565_blit(panel->gram_pixels, panel->width, panel->height, x_start, y_start, &view);
}

static bool fx_panel_set_display_enabled(void *ctx, bool enabled) {
    ViewerGramPanel *panel = (ViewerGramPanel *)ctx;
    if (!panel) {
        return false;
    }
    panel->display_enabled = enabled;
    return true;
}

static bool fx_panel_set_sleep(void *ctx, bool sleep) {
    ViewerGramPanel *panel = (ViewerGramPanel *)ctx;
    if (!panel) {
        return false;
    }
    panel->sleeping = sleep;
    return true;
}

static const VgPanelOps g_fx_panel_ops = {
    .reset = fx_panel_reset,
    .init = fx_panel_init,
    .set_orientation = fx_panel_set_orientation,
    .set_gap = fx_panel_set_gap,
    .write_bitmap = fx_panel_write_bitmap,
    .write_bitmap_2d = fx_panel_write_bitmap_2d,
    .set_display_enabled = fx_panel_set_display_enabled,
    .set_sleep = fx_panel_set_sleep,
};
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    atomic_bool running;
    bool started;
    VgRenderSlotState *slot_states;
    uint32_t *slot_seen_generations;
    atomic_uint_fast32_t rendered_frame_serial;
    atomic_uint_fast32_t last_dirty_pixels;
    atomic_uint_fast32_t last_changed_slots;
    uint32_t *last_rendered_generation;
    atomic_uint_fast64_t render_lock_hold_ns_total;
    atomic_uint_fast64_t render_lock_hold_ns_max;
    atomic_uint_fast64_t render_lock_samples;
    atomic_uint_fast64_t skipped_generations_total;
    atomic_uint_fast64_t skipped_max_frame;
    atomic_uint_fast64_t skipped_max_slot;
    atomic_uint_fast32_t last_transfer_rects;
    atomic_uint_fast64_t last_transfer_ns;
    VgClipRect last_transfer_clip_rects[FX_MAX_DIRTY_PLAN_RECTS];
    uint16_t last_transfer_clip_rect_count;
    VgClipRect last_overlay_clip_rects[FX_MAX_DIRTY_PLAN_RECTS];
    uint16_t last_overlay_clip_rect_count;
    uint_fast32_t last_overlay_frame_serial;
    pthread_mutex_t transfer_rects_mutex;
    atomic_uint_fast32_t animated_slots_mask;
    ViewerSceneBundle *collision_bundle;
    ViewerSpatialRuleSet *collision_rule_set;
    bool collision_in_render_thread;
} ViewerRenderThread;
static ViewerRenderThread g_render_thread = {0};

static uint32_t fx_compute_animated_slots_mask(const VgRenderSlotState *slot_states) {
    if (!slot_states || g_fx_slot_count == 0u) {
        return 0u;
    }
    uint32_t mask = 0u;
    for (uint8_t i = 0; i < g_fx_slot_count; i++) {
        if (slot_states[i].initialized && slot_states[i].has_animation) {
            mask |= (1u << i);
        }
    }
    return mask;
}

static void fx_destroy_slot_runtime_buffers(void) {
    if (g_scene_slot_snapshots) {
        for (uint8_t i = 0; i < g_fx_slot_count; i++) {
            RELEASE(g_scene_slot_snapshots[i]);
        }
    }
    CLJ_HOST_FREE(g_render_thread.slot_states);
    CLJ_HOST_FREE(g_render_thread.slot_seen_generations);
    CLJ_HOST_FREE(g_render_thread.last_rendered_generation);
    g_render_thread.slot_states = NULL;
    g_render_thread.slot_seen_generations = NULL;
    g_render_thread.last_rendered_generation = NULL;
    CLJ_HOST_FREE(g_scene_slot_snapshots);
    CLJ_HOST_FREE(g_slot_render_priority);
    g_scene_slot_snapshots = NULL;
    g_slot_render_priority = NULL;
    g_fx_slot_count = 0u;
}

static void fx_publish_slot_scene_snapshots_locked(const ViewerSceneBundle *bundle) {
    if (!bundle || !bundle->slots || !g_scene_slot_snapshots || bundle->slot_count != g_fx_slot_count) {
        return;
    }
    for (uint8_t i = 0; i < g_fx_slot_count; i++) {
        ID next_snapshot = bundle->slots[i].scene;
        if (g_scene_slot_snapshots[i] == next_snapshot) {
            continue;
        }
        ID retained_snapshot = RETAIN(next_snapshot);
        RELEASE(g_scene_slot_snapshots[i]);
        g_scene_slot_snapshots[i] = retained_snapshot;
    }
}

static void fx_sync_and_publish_configured_slots(ViewerSceneBundle *bundle,
                                                 ViewerSpatialRuleSet *rule_set,
                                                 VgSlotChangeTracker *slot_change_tracker,
                                                 bool publish_changes) {
    bool locked = false;
    if (g_render_thread.started) {
        if (pthread_mutex_lock(&g_render_thread.mutex) != 0) {
            return;
        }
        locked = true;
    }
    fx_sync_configured_slots(bundle, rule_set, slot_change_tracker, publish_changes);
    fx_publish_slot_scene_snapshots_locked(bundle);
    if (locked) {
        (void)pthread_mutex_unlock(&g_render_thread.mutex);
    }
}

static void fx_configure_render_thread_collision(ViewerSceneBundle *bundle,
                                                 ViewerSpatialRuleSet *rule_set,
                                                 bool enabled) {
    g_render_thread.collision_bundle = enabled ? bundle : NULL;
    g_render_thread.collision_rule_set = enabled ? rule_set : NULL;
    g_render_thread.collision_in_render_thread =
        enabled && bundle != NULL && rule_set != NULL;
}

static bool fx_init_slot_runtime_buffers(const ViewerSceneBundle *bundle) {
    if (!bundle || !bundle->slots || bundle->slot_count == 0u || bundle->slot_count > FX_MAX_SLOTS) {
        return false;
    }
    fx_destroy_slot_runtime_buffers();
    g_scene_slot_snapshots = (ID *)CLJ_HOST_CALLOC(bundle->slot_count, sizeof(ID));
    g_slot_render_priority = (uint8_t *)CLJ_HOST_MALLOC(bundle->slot_count * sizeof(uint8_t));
    g_render_thread.slot_states =
        (VgRenderSlotState *)CLJ_HOST_CALLOC(bundle->slot_count, sizeof(VgRenderSlotState));
    g_render_thread.slot_seen_generations =
        (uint32_t *)CLJ_HOST_CALLOC(bundle->slot_count, sizeof(uint32_t));
    g_render_thread.last_rendered_generation =
        (uint32_t *)CLJ_HOST_CALLOC(bundle->slot_count, sizeof(uint32_t));
    if (!g_scene_slot_snapshots || !g_slot_render_priority || !g_render_thread.slot_states ||
        !g_render_thread.slot_seen_generations || !g_render_thread.last_rendered_generation) {
        fx_destroy_slot_runtime_buffers();
        return false;
    }
    g_fx_slot_count = bundle->slot_count;
    for (uint8_t i = 0; i < g_fx_slot_count; i++) {
        g_scene_slot_snapshots[i] = RETAIN(bundle->slots[i].scene);
        g_slot_render_priority[i] = i;
    }
    return true;
}

static uint64_t monotonic_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

#if defined(__APPLE__)
static mach_timebase_info_data_t g_mach_timebase = {0};

static void fx_init_mach_timebase(void) {
    if (g_mach_timebase.denom == 0) {
        (void)mach_timebase_info(&g_mach_timebase);
    }
}

static uint64_t ns_to_mach_abs(uint64_t ns) {
    return (ns * g_mach_timebase.denom) / g_mach_timebase.numer;
}
#endif

static void fx_set_realtime_thread_policy(void) {
#if defined(__APPLE__)
    fx_init_mach_timebase();
    thread_time_constraint_policy_data_t policy;
    policy.period      = (uint32_t)ns_to_mach_abs(16666667u);
    policy.computation = (uint32_t)ns_to_mach_abs(2000000u);
    policy.constraint  = (uint32_t)ns_to_mach_abs(16666667u);
    policy.preemptible = true;
    (void)thread_policy_set(
        mach_thread_self(),
        THREAD_TIME_CONSTRAINT_POLICY,
        (thread_policy_t)&policy,
        THREAD_TIME_CONSTRAINT_POLICY_COUNT);
#endif
}

static void collect_render_thread_metrics(ViewerPerfSnapshot *out_snapshot) {
    if (!out_snapshot) {
        return;
    }
    uint64_t render_samples = atomic_exchange_explicit(&g_render_thread.render_lock_samples,
                                                       0u,
                                                       memory_order_acq_rel);
    uint64_t render_hold_ns = atomic_exchange_explicit(&g_render_thread.render_lock_hold_ns_total,
                                                       0u,
                                                       memory_order_acq_rel);
    uint64_t render_hold_max_ns = atomic_exchange_explicit(&g_render_thread.render_lock_hold_ns_max,
                                                           0u,
                                                           memory_order_acq_rel);
    out_snapshot->skipped_generations =
        atomic_exchange_explicit(&g_render_thread.skipped_generations_total, 0u, memory_order_acq_rel);
    out_snapshot->skipped_max_frame =
        atomic_exchange_explicit(&g_render_thread.skipped_max_frame, 0u, memory_order_acq_rel);
    out_snapshot->skipped_max_slot =
        atomic_exchange_explicit(&g_render_thread.skipped_max_slot, 0u, memory_order_acq_rel);
    out_snapshot->avg_render_lock_hold_us =
        render_samples ? ((double)render_hold_ns / (double)render_samples) / 1000.0 : 0.0;
    out_snapshot->max_render_lock_hold_us = (double)render_hold_max_ns / 1000.0;
}

static size_t fx_copy_last_transfer_rects(VgClipRect *out_rects, size_t out_capacity) {
    if (!out_rects || out_capacity == 0u) {
        return 0u;
    }
    if (pthread_mutex_lock(&g_render_thread.transfer_rects_mutex) != 0) {
        return 0u;
    }
    size_t count = (size_t)g_render_thread.last_transfer_clip_rect_count;
    if (count > out_capacity) {
        count = out_capacity;
    }
    if (count > 0u) {
        memcpy(out_rects, g_render_thread.last_transfer_clip_rects, count * sizeof(VgClipRect));
    }
    (void)pthread_mutex_unlock(&g_render_thread.transfer_rects_mutex);
    return count;
}

static void fx_store_last_transfer_result(uint_fast32_t frame_serial,
                                              const VgClipRect *rects,
                                              size_t rect_count,
                                              uint64_t transfer_ns) {
    size_t stored_count = rect_count;
    if (stored_count > FX_MAX_DIRTY_PLAN_RECTS) {
        stored_count = FX_MAX_DIRTY_PLAN_RECTS;
    }
    atomic_store_explicit(&g_render_thread.last_transfer_rects, (uint32_t)stored_count, memory_order_relaxed);
    atomic_store_explicit(&g_render_thread.last_transfer_ns, transfer_ns, memory_order_relaxed);
    if (pthread_mutex_lock(&g_render_thread.transfer_rects_mutex) != 0) {
        return;
    }
    g_render_thread.last_transfer_clip_rect_count = (uint16_t)stored_count;
    if (stored_count > 0u && rects) {
        memcpy(g_render_thread.last_transfer_clip_rects, rects, stored_count * sizeof(VgClipRect));
        g_render_thread.last_overlay_clip_rect_count = (uint16_t)stored_count;
        memcpy(g_render_thread.last_overlay_clip_rects, rects, stored_count * sizeof(VgClipRect));
        g_render_thread.last_overlay_frame_serial = frame_serial;
    }
    (void)pthread_mutex_unlock(&g_render_thread.transfer_rects_mutex);
}

static size_t fx_take_pending_overlay_rects(uint_fast32_t *io_last_presented_overlay_frame_serial,
                                                VgClipRect *out_rects,
                                                size_t out_capacity,
                                                uint_fast32_t *out_frame_serial) {
    if (!io_last_presented_overlay_frame_serial || !out_rects || out_capacity == 0u) {
        return 0u;
    }
    if (out_frame_serial) {
        *out_frame_serial = 0u;
    }
    if (pthread_mutex_lock(&g_render_thread.transfer_rects_mutex) != 0) {
        return 0u;
    }
    uint_fast32_t overlay_frame_serial = g_render_thread.last_overlay_frame_serial;
    if (overlay_frame_serial == 0u || overlay_frame_serial == *io_last_presented_overlay_frame_serial) {
        (void)pthread_mutex_unlock(&g_render_thread.transfer_rects_mutex);
        return 0u;
    }
    size_t count = (size_t)g_render_thread.last_overlay_clip_rect_count;
    if (count > out_capacity) {
        count = out_capacity;
    }
    if (count > 0u) {
        memcpy(out_rects, g_render_thread.last_overlay_clip_rects, count * sizeof(VgClipRect));
    }
    (void)pthread_mutex_unlock(&g_render_thread.transfer_rects_mutex);
    *io_last_presented_overlay_frame_serial = overlay_frame_serial;
    if (out_frame_serial) {
        *out_frame_serial = overlay_frame_serial;
    }
    return count;
}

/* Render-thread loop: changed slots render immediately; animated slots tick continuously. */
static void *fx_render_thread_main(void *arg) {
    VgFrameBuffer *fb = (VgFrameBuffer *)arg;
    if (!fb || !g_render_thread.slot_states || !g_render_thread.slot_seen_generations ||
        !g_render_thread.last_rendered_generation || !g_scene_slot_snapshots || !g_slot_render_priority ||
        g_fx_slot_count == 0u) {
        return NULL;
    }
    CLJ_ASSERT(!subjective_c_is_interpreter_thread());
    fx_set_realtime_thread_policy();
    while (atomic_load_explicit(&g_render_thread.running, memory_order_acquire)) {
        uint32_t animated_mask = (uint32_t)atomic_load_explicit(&g_render_thread.animated_slots_mask,
                                                                 memory_order_acquire);
        uint32_t wait_timeout_ms = (animated_mask == 0u) ? UINT32_MAX : FX_ANIMATED_WAIT_TIMEOUT_MS;
        uint32_t slot_generations[FX_MAX_SLOTS] = {0};
        uint32_t changed_mask = vg_slot_change_tracker_wait_for_changes(&g_slot_change_tracker,
                                                                        g_render_thread.slot_seen_generations,
                                                                        slot_generations,
                                                                        wait_timeout_ms);
        if (!atomic_load_explicit(&g_render_thread.running, memory_order_acquire)) {
            break;
        }
        if (changed_mask == 0u && animated_mask == 0u) {
            continue;
        }

        uint32_t frame_now_ms = platform_current_time_ms();
        uint32_t frame_dirty_pixels = 0u;
        uint32_t frame_changed_slots = 0u;
        uint64_t frame_skipped_total = 0u;
        VgClipRect frame_dirty_rects[FX_MAX_DIRTY_PLAN_RECTS] = {0};
        size_t frame_dirty_rect_count = 0u;
        int publish_lock_rc = pthread_mutex_trylock(&g_render_thread.mutex);
#ifdef DEBUG
        if (publish_lock_rc != 0) {
            /*
             * Render thread must never block on publish lock. Busy is expected
             * under contention; other errors indicate a threading bug.
             */
            CLJ_ASSERT(publish_lock_rc == EBUSY);
        }
#endif
        if (publish_lock_rc != 0) {
            /*
             * Keep render progress independent from interpreter/main-thread
             * publish work. If the publish lock is busy, skip this cycle.
             */
            sched_yield();
            continue;
        }
        uint64_t lock_acquired_ns = monotonic_now_ns();
        for (size_t p = 0; p < g_fx_slot_count; p++) {
            uint8_t i = g_slot_render_priority[p];
            bool slot_changed = (changed_mask & (1u << i)) != 0u;
            bool slot_animated_tick = !slot_changed && ((animated_mask & (1u << i)) != 0u);
            if (!slot_changed && !slot_animated_tick) {
                continue;
            }
            ID snapshot = g_scene_slot_snapshots[i];
            if (!snapshot) {
                continue;
            }
            /*
             * Ownership contract:
             * the main thread publishes immutable per-slot scene snapshots under
             * g_render_thread.mutex; the render thread only consumes the already
             * retained snapshot and never touches atom/eval/refcount APIs here.
             */
            bool had_prior_frame = g_render_thread.slot_states[i].initialized;
            VgClipRect prev_clip_rect = g_render_thread.slot_states[i].last_clip_rect;
            bool prev_visible = g_render_thread.slot_states[i].last_visible;
            bool prev_opaque = g_render_thread.slot_states[i].last_opaque;
            uint16_t prev_clear_color = g_render_thread.slot_states[i].last_clear_color;
            uint8_t prev_guard_px = g_render_thread.slot_states[i].last_guard_px;
            VgRenderFrameSlotResult slot_result = {0};
            VgClipRect refined_dirty_rects[FX_MAX_DIRTY_PLAN_RECTS] = {0};
            size_t refined_dirty_count = 0u;
            bool use_refined_dirty_rects = false;
            vg_rendered_state_capture_begin(i, slot_generations[i], frame_now_ms);
            fx_timeline_ingress_set_current_slot(i);
            bool rendered = vg_render_frame_slot_record_result_at_ms(snapshot,
                                                                     &g_render_thread.slot_states[i],
                                                                     fb,
                                                                     slot_generations[i],
                                                                     frame_now_ms,
                                                                     slot_animated_tick,
                                                                     &slot_result);
            fx_timeline_ingress_clear_current_slot();
            bool slot_props_unchanged = had_prior_frame &&
                                        vg_clip_rect_equal(prev_clip_rect,
                                                           g_render_thread.slot_states[i].last_clip_rect) &&
                                        prev_visible == g_render_thread.slot_states[i].last_visible &&
                                        prev_opaque == g_render_thread.slot_states[i].last_opaque &&
                                        prev_clear_color == g_render_thread.slot_states[i].last_clear_color &&
                                        prev_guard_px == g_render_thread.slot_states[i].last_guard_px;
            if (rendered && slot_props_unchanged) {
                uint8_t dirty_padding = (g_render_thread.slot_states[i].last_guard_px <= 253u)
                                            ? (uint8_t)(g_render_thread.slot_states[i].last_guard_px + 2u)
                                            : UINT8_MAX;
                if (vg_rendered_state_capture_collect_dirty_rects(i,
                                                                  g_render_thread.slot_states[i].last_clip_rect,
                                                                  dirty_padding,
                                                                  refined_dirty_rects,
                                                                  FX_MAX_DIRTY_PLAN_RECTS,
                                                                  &refined_dirty_count) &&
                    refined_dirty_count > 0u) {
                    use_refined_dirty_rects = true;
                    slot_result.dirty_rect = refined_dirty_rects[0];
                    slot_result.dirty_pixels = 0u;
                    for (size_t dirty_i = 0; dirty_i < refined_dirty_count; dirty_i++) {
                        slot_result.dirty_pixels +=
                            fx_clip_rect_area_on_framebuffer(refined_dirty_rects[dirty_i], fb->width, fb->height);
                    }
                }
            }
            if (rendered && slot_props_unchanged && !use_refined_dirty_rects) {
                /*
                 * No detected dirty leaves for this slot revision. Avoid
                 * falling back to the full slot dirty rect, which forces
                 * full-screen transfer plans while the image is unchanged.
                 */
                slot_result.dirty_rect = (VgClipRect){0};
                slot_result.dirty_pixels = 0u;
            }
            if (rendered) {
                vg_rendered_state_capture_commit();
            } else {
                vg_rendered_state_capture_discard();
            }
            if (rendered) {
                uint32_t prev_gen = g_render_thread.last_rendered_generation[i];
                uint32_t curr_gen = slot_generations[i];
                if (slot_changed && curr_gen > (prev_gen + 1u)) {
                    uint64_t skipped = (uint64_t)(curr_gen - prev_gen - 1u);
                    frame_skipped_total += skipped;
                    atomic_fetch_add_explicit(&g_render_thread.skipped_generations_total,
                                              skipped,
                                              memory_order_relaxed);
                    uint64_t slot_max = atomic_load_explicit(&g_render_thread.skipped_max_slot, memory_order_relaxed);
                    while (skipped > slot_max &&
                           !atomic_compare_exchange_weak_explicit(&g_render_thread.skipped_max_slot,
                                                                  &slot_max,
                                                                  skipped,
                                                                  memory_order_relaxed,
                                                                  memory_order_relaxed)) {
                    }
                }
                g_render_thread.last_rendered_generation[i] = curr_gen;
                if (slot_result.dirty_pixels > 0u && !vg_clip_rect_is_empty(slot_result.dirty_rect)) {
                    frame_changed_slots++;
                    frame_dirty_pixels += slot_result.dirty_pixels;
                    if (use_refined_dirty_rects &&
                        frame_dirty_rect_count + refined_dirty_count <= FX_MAX_DIRTY_PLAN_RECTS) {
                        for (size_t dirty_i = 0; dirty_i < refined_dirty_count; dirty_i++) {
                            frame_dirty_rects[frame_dirty_rect_count++] = refined_dirty_rects[dirty_i];
                        }
                    } else if (frame_dirty_rect_count < FX_MAX_DIRTY_PLAN_RECTS) {
                        frame_dirty_rects[frame_dirty_rect_count++] = slot_result.dirty_rect;
                    }
                }
            }
        }
        if (frame_skipped_total > 0u) {
            uint64_t frame_max = atomic_load_explicit(&g_render_thread.skipped_max_frame, memory_order_relaxed);
            while (frame_skipped_total > frame_max &&
                   !atomic_compare_exchange_weak_explicit(&g_render_thread.skipped_max_frame,
                                                          &frame_max,
                                                          frame_skipped_total,
                                                          memory_order_relaxed,
                                                          memory_order_relaxed)) {
            }
        }
        memcpy(g_render_thread.slot_seen_generations,
               slot_generations,
               (size_t)g_fx_slot_count * sizeof(uint32_t));
        atomic_store_explicit(&g_render_thread.animated_slots_mask,
                              fx_compute_animated_slots_mask(g_render_thread.slot_states),
                              memory_order_release);
        atomic_store_explicit(&g_render_thread.last_dirty_pixels, frame_dirty_pixels, memory_order_relaxed);
        atomic_store_explicit(&g_render_thread.last_changed_slots, frame_changed_slots, memory_order_relaxed);
        if (g_render_thread.collision_in_render_thread &&
            g_render_thread.collision_bundle &&
            g_render_thread.collision_rule_set) {
            const VgClipRect *collision_dirty_rects =
                (frame_dirty_rect_count > 0u) ? frame_dirty_rects : NULL;
            size_t collision_dirty_count =
                (frame_dirty_rect_count > 0u) ? frame_dirty_rect_count : 0u;
            (void)fx_collision_detect_step(g_render_thread.collision_bundle,
                                           g_render_thread.collision_rule_set,
                                           frame_now_ms,
                                           collision_dirty_rects,
                                           collision_dirty_count);
        }
        uint64_t lock_release_ns = monotonic_now_ns();
        uint64_t hold_ns = (lock_release_ns > lock_acquired_ns)
                               ? (lock_release_ns - lock_acquired_ns)
                               : 0u;
        atomic_fetch_add_explicit(&g_render_thread.render_lock_hold_ns_total, hold_ns, memory_order_relaxed);
        atomic_fetch_add_explicit(&g_render_thread.render_lock_samples, 1u, memory_order_relaxed);
        uint64_t max_hold = atomic_load_explicit(&g_render_thread.render_lock_hold_ns_max, memory_order_relaxed);
        while (hold_ns > max_hold &&
               !atomic_compare_exchange_weak_explicit(&g_render_thread.render_lock_hold_ns_max,
                                                      &max_hold,
                                                      hold_ns,
                                                      memory_order_relaxed,
                                                      memory_order_relaxed)) {
        }
        (void)pthread_mutex_unlock(&g_render_thread.mutex);
        /*
         * Simulate SPI/I80 dirty-rect transfer into display GRAM.
         * The render buffer remains private to the render thread; only the
         * finished dirty rects are copied into the live GRAM after rendering.
         */
        if (g_gram_pixels && frame_dirty_rect_count > 0u) {
            uint64_t transfer_begin_ns = monotonic_now_ns();
            uint_fast32_t completed_frame_serial =
                atomic_load_explicit(&g_render_thread.rendered_frame_serial, memory_order_relaxed) + 1u;
            VgClipRect planned_rects[FX_MAX_DIRTY_PLAN_RECTS] = {0};
            size_t planned_count = vg_dirty_union_plan_rects(frame_dirty_rects,
                                                             frame_dirty_rect_count,
                                                             FX_DIRTY_PIXEL_BUDGET,
                                                             planned_rects,
                                                             FX_MAX_DIRTY_PLAN_RECTS);
            for (size_t rect_i = 0; rect_i < planned_count; rect_i++) {
                (void)vg_panel_backend_submit_clip_rect(&g_gram_panel, fb, planned_rects[rect_i]);
            }
            uint64_t transfer_end_ns = monotonic_now_ns();
            uint64_t transfer_ns = (transfer_end_ns > transfer_begin_ns)
                                       ? (transfer_end_ns - transfer_begin_ns)
                                       : 0u;
            fx_store_last_transfer_result(completed_frame_serial, planned_rects, planned_count, transfer_ns);
        } else {
            fx_store_last_transfer_result(0u, NULL, 0u, 0u);
        }
        atomic_fetch_add_explicit(&g_render_thread.rendered_frame_serial, 1u, memory_order_release);
    }
    return NULL;
}

/* Start dedicated render thread that owns slot rendering. */
static bool start_render_thread(VgFrameBuffer *fb) {
    if (!fb || !g_render_thread.slot_states || !g_render_thread.slot_seen_generations ||
        !g_render_thread.last_rendered_generation) {
        return false;
    }
    memset(g_render_thread.slot_states, 0, (size_t)g_fx_slot_count * sizeof(VgRenderSlotState));
    memset(g_render_thread.slot_seen_generations, 0, (size_t)g_fx_slot_count * sizeof(uint32_t));
    memset(g_render_thread.last_rendered_generation, 0, (size_t)g_fx_slot_count * sizeof(uint32_t));
    atomic_store_explicit(&g_render_thread.rendered_frame_serial, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.last_dirty_pixels, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.last_changed_slots, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.render_lock_hold_ns_total, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.render_lock_hold_ns_max, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.render_lock_samples, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.skipped_generations_total, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.skipped_max_frame, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.skipped_max_slot, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.last_transfer_rects, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.last_transfer_ns, 0u, memory_order_release);
    atomic_store_explicit(&g_render_thread.animated_slots_mask, 0u, memory_order_release);
    fx_timeline_ingress_init();
    if (pthread_mutex_init(&g_render_thread.mutex, NULL) != 0) {
        fx_timeline_ingress_shutdown();
        return false;
    }
    if (pthread_mutex_init(&g_render_thread.transfer_rects_mutex, NULL) != 0) {
        (void)pthread_mutex_destroy(&g_render_thread.mutex);
        fx_timeline_ingress_shutdown();
        return false;
    }
    g_render_thread.last_transfer_clip_rect_count = 0u;
    g_render_thread.last_overlay_clip_rect_count = 0u;
    g_render_thread.last_overlay_frame_serial = 0u;
    atomic_store_explicit(&g_render_thread.running, true, memory_order_release);
    if (subjective_c_pthread_create_named(&g_render_thread.thread, NULL, fx_render_thread_main, fb, "render") != 0) {
        atomic_store_explicit(&g_render_thread.running, false, memory_order_release);
        (void)pthread_mutex_destroy(&g_render_thread.transfer_rects_mutex);
        (void)pthread_mutex_destroy(&g_render_thread.mutex);
        fx_timeline_ingress_shutdown();
        return false;
    }
    g_render_thread.started = true;
    return true;
}

/* Stop render thread and free synchronization primitives. */
static void stop_render_thread(void) {
    if (!g_render_thread.started) {
        return;
    }
    atomic_store_explicit(&g_render_thread.running, false, memory_order_release);
    /* Wake blocked render wait to allow clean shutdown. */
    (void)vg_slot_change_tracker_publish(&g_slot_change_tracker, 0u, NULL);
    (void)pthread_join(g_render_thread.thread, NULL);
    fx_timeline_ingress_shutdown();
    (void)pthread_mutex_destroy(&g_render_thread.transfer_rects_mutex);
    (void)pthread_mutex_destroy(&g_render_thread.mutex);
    fx_configure_render_thread_collision(NULL, NULL, false);
    g_render_thread.started = false;
}

static bool fx_renderer_start_callback(ID slot_atoms, void *user_data) {
    (void)slot_atoms;
    VgFrameBuffer *fb = (VgFrameBuffer *)user_data;
    return start_render_thread(fb);
}

static bool fx_renderer_stop_callback(void *user_data) {
    (void)user_data;
    stop_render_thread();
    return true;
}

/* Wait for the next presentation deadline. */
static bool fx_wait_for_frame_pacing(ViewerHostWindow *window,
                                         bool use_mfb_waitsync,
                                         uint64_t target_frame_ns,
                                         uint64_t *next_frame_deadline_ns,
                                         TimingAccumulator *waitsync_stats) {
    uint64_t waitsync_begin_ns = monotonic_now_ns();
    bool waitsync_ok = true;
    if (use_mfb_waitsync) {
        waitsync_ok = fx_host_window_wait_sync(window);
    } else {
#if defined(__APPLE__)
        uint64_t deadline_mach = mach_absolute_time() +
            ns_to_mach_abs((*next_frame_deadline_ns > waitsync_begin_ns)
                           ? (*next_frame_deadline_ns - waitsync_begin_ns)
                           : 0u);
        (void)mach_wait_until(deadline_mach);
#else
        uint64_t t = waitsync_begin_ns;
        while (t < *next_frame_deadline_ns) {
            uint64_t remaining_ns = *next_frame_deadline_ns - t;
            if (remaining_ns > 1500000u) {
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = (long)(remaining_ns - 800000u);
                (void)nanosleep(&ts, NULL);
            } else {
                sched_yield();
            }
            t = monotonic_now_ns();
        }
#endif
        uint64_t now_ns = monotonic_now_ns();
        *next_frame_deadline_ns += target_frame_ns;
        if (now_ns > *next_frame_deadline_ns + (target_frame_ns * 3u)) {
            *next_frame_deadline_ns = now_ns + target_frame_ns;
        }
    }
    uint64_t waitsync_end_ns = monotonic_now_ns();
    uint64_t waitsync_ns = (waitsync_end_ns > waitsync_begin_ns)
                               ? (waitsync_end_ns - waitsync_begin_ns)
                               : 0u;
    timing_accumulator_add(waitsync_stats, waitsync_ns);
    return waitsync_ok;
}

/*
 * Lock-free frame polling: reads atomic counters from the render thread.
 * The main thread reads fb_pixels directly (no copy), faithfully simulating
 * ESP32 SPI/I80 displays where the bus reads the live GRAM with no double-buffer.
 * Tearing is possible and accepted — same as on real hardware.
 */
static ViewerFrameRenderResult fx_poll_render_frame(void) {
    ViewerFrameRenderResult result = {0};
    result.frame_serial = atomic_load_explicit(&g_render_thread.rendered_frame_serial, memory_order_acquire);
    result.dirty_pixels = atomic_load_explicit(&g_render_thread.last_dirty_pixels, memory_order_relaxed);
    result.changed_slots = atomic_load_explicit(&g_render_thread.last_changed_slots, memory_order_relaxed);
    result.transfer_rects = atomic_load_explicit(&g_render_thread.last_transfer_rects, memory_order_relaxed);
    result.transfer_ns = atomic_load_explicit(&g_render_thread.last_transfer_ns, memory_order_relaxed);
    return result;
}

static void fx_expand_rgb565_to_window(const uint16_t *src, uint32_t *dst, size_t count) {
    if (!src || !dst) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        dst[i] = rgb565_to_xrgb8888(src[i]);
    }
}

static bool fx_should_run_collision_step(uint_fast32_t frame_serial,
                                             uint_fast32_t *io_last_collision_frame_serial) {
    if (!io_last_collision_frame_serial || frame_serial == 0u) {
        return false;
    }
    if (frame_serial == *io_last_collision_frame_serial) {
        return false;
    }
    *io_last_collision_frame_serial = frame_serial;
    return true;
}

static size_t fx_collect_collision_dirty_rects(VgClipRect *out_rects, size_t out_capacity) {
    if (!out_rects || out_capacity == 0u) {
        return 0u;
    }
    return fx_copy_last_transfer_rects(out_rects, out_capacity);
}

static void fx_update_redraw_overlay_toggle(const uint8_t *keys, ViewerRuntimeFlags *flags) {
    if (!flags) {
        return;
    }
    if (fx_key_pressed_once(keys, KB_KEY_R, &flags->r_key_was_down)) {
        flags->redraw_overlay_enabled = !flags->redraw_overlay_enabled;
    }
}
#endif

int fx_host_app_run(void) {
#if !defined(TINYCLJ_WITH_MINIFB)
    fprintf(stderr, "MiniFB support is disabled for this build.\n");
    return 1;
#else
    fx_host_app_install_signal_handlers();
    uint16_t fb_pixels[VIEW_W * VIEW_H];
    uint32_t window_pixels[VIEW_W * VIEW_H];
    ViewerHostWindow *window = NULL;
    bool slot_runtime_initialized = false;
    bool slot_tracker_initialized = false;
    bool render_thread_started = false;
    bool runloop_thread_started = false;
    bool demo_bundle_initialized = false;
    ViewerSceneBundle demo_bundle = {0};
    ViewerSpatialRuleSet spatial_rules = {0};
    int exit_code = 1;
    uint64_t app_start_ns = monotonic_now_ns();
    fx_set_realtime_thread_policy();

    g_gram_pixels = fb_pixels;
    g_gram_panel_ctx.gram_pixels = fb_pixels;
    g_gram_panel_ctx.width = VIEW_W;
    g_gram_panel_ctx.height = VIEW_H;
    g_gram_panel_ctx.display_enabled = false;
    g_gram_panel_ctx.sleeping = false;
    g_gram_panel.ops = &g_fx_panel_ops;
    g_gram_panel.ctx = &g_gram_panel_ctx;
    g_gram_panel.initialized = false;
    memset(g_render_buffer, 0, sizeof(g_render_buffer));
    memset(fb_pixels, 0, sizeof(uint16_t) * VIEW_W * VIEW_H);
    if (!vg_panel_reset(&g_gram_panel) ||
        !vg_panel_init(&g_gram_panel) ||
        !vg_panel_set_display_enabled(&g_gram_panel, true)) {
        fprintf(stderr, "Failed to initialize host panel simulation\n");
        return 1;
    }
    VgFrameBuffer fb;
    if (!vg_framebuffer_init(&fb, VIEW_W, VIEW_H, g_render_buffer, VIEW_W * VIEW_H)) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    runtime_init(&g_runtime);
    event_loop_init();
    vg_rendered_state_reset_all();
    fx_seed_gpio_key_levels();
    tiny_fx_host_apply_heap_limit();
    fprintf(stderr, "[heap-diag] after heap limit: %zu / %zu bytes\n",
            memory_current_usage_bytes(), memory_get_heap_limit_bytes());
    EvalState *fx_eval_state = evalstate_new(true);
    if (!fx_eval_state) {
        fprintf(stderr, "Failed to initialize eval state\n");
        goto cleanup;
    }
    fprintf(stderr, "[heap-diag] after evalstate_new: %zu / %zu bytes\n",
            memory_current_usage_bytes(), memory_get_heap_limit_bytes());
    evalstate_set_ns(fx_eval_state, "user");
    if (!tiny_fx_gfx_require_records_namespace(fx_eval_state) ||
        !tiny_fx_gfx_ensure_schema(fx_eval_state)) {
        fprintf(stderr, "Failed to initialize vector scene record schema via tiny-fx.gfx\n");
        goto cleanup;
    }
    fprintf(stderr, "[heap-diag] after gfx schema: %zu / %zu bytes\n",
            memory_current_usage_bytes(), memory_get_heap_limit_bytes());
    ViewerConfigSource config_source = fx_default_config_source();
    TRY {
        if (!fx_load_deployment_config(fx_eval_state, config_source, &demo_bundle, &spatial_rules)) {
            fprintf(stderr, "Failed to load viewer config from %s\n", config_source.display_name);
            goto cleanup;
        }
    } CATCH(ex) {
        fprintf(stderr, "Failed to load viewer config from %s\n", config_source.display_name);
        if (ex) {
            print_exception(ex);
        }
        goto cleanup;
    } END_TRY
    fprintf(stderr, "[heap-diag] after deployment config: %zu / %zu bytes\n",
            memory_current_usage_bytes(), memory_get_heap_limit_bytes());
    demo_bundle_initialized = true;
    if (!fx_init_slot_runtime_buffers(&demo_bundle)) {
        fprintf(stderr, "Failed to initialize configured slot runtime\n");
        goto cleanup;
    }
    slot_runtime_initialized = true;
    if (!start_runloop_thread(fx_eval_state)) {
        fprintf(stderr, "Failed to start Clojure runloop thread\n");
        goto cleanup;
    }
    runloop_thread_started = true;
    if (!vg_slot_change_tracker_init(&g_slot_change_tracker, demo_bundle.slot_count)) {
        fprintf(stderr, "Failed to initialize slot change tracker\n");
        goto cleanup;
    }
    slot_tracker_initialized = true;
    tiny_renderer_lifecycle_set_callbacks(fx_renderer_start_callback,
                                          fx_renderer_stop_callback,
                                          &fb);
    fx_configure_render_thread_collision(&demo_bundle, &spatial_rules, true);
    if (!tiny_renderer_lifecycle_start(NULL)) {
        fprintf(stderr, "Failed to start render thread\n");
        goto cleanup;
    }
    render_thread_started = true;

#if defined(__APPLE__)
    macos_fx_begin_performance_activity();
#endif
    const unsigned default_win_w = VIEW_W * VIEW_DEFAULT_WINDOW_SCALE;
    const unsigned default_win_h = VIEW_H * VIEW_DEFAULT_WINDOW_SCALE;
    window = fx_host_window_open("tiny-fx", default_win_w, default_win_h);
    if (!window) {
        fprintf(stderr, "Failed to open MiniFB window\n");
        goto cleanup;
    }
    fx_host_window_show_cursor(window, true);
    (void)fx_host_window_set_viewport(window, 0, 0, default_win_w, default_win_h);
    ViewerPerfWindow perf_window;
    perf_window_init(&perf_window, 0.0);

    uint_fast32_t last_seen_render_frame_serial = 0u;
    uint_fast32_t last_presented_overlay_frame_serial = 0u;
    uint_fast32_t last_collision_frame_serial = 0u;
    bool has_presented_window_frame = false;
    bool startup_callback_pending = demo_bundle.startup_callback != NULL;
    ViewerRuntimeFlags runtime_flags = {
#if defined(__APPLE__)
        .use_mfb_waitsync = false,
#else
        .use_mfb_waitsync = true,
#endif
        .r_key_was_down = false,
        .redraw_overlay_enabled = false,
        .w_key_was_down = false
    };
    const uint64_t target_frame_ns = 1000000000ull / TARGET_FPS;
    uint64_t next_frame_deadline_ns = monotonic_now_ns() + target_frame_ns;
    uint64_t last_present_ns = 0u;
    TimingAccumulator frame_dt_stats;
    TimingAccumulator waitsync_stats;
    TimingAccumulator update_stats;
    timing_accumulator_reset(&frame_dt_stats);
    timing_accumulator_reset(&waitsync_stats);
    timing_accumulator_reset(&update_stats);
    uint32_t long_frame_count = 0u;
    vg_framebuffer_clear(&fb, SCENE_ERASE_COLOR);
    memcpy(fb_pixels, g_render_buffer, sizeof(g_render_buffer));
    for (uint8_t i = 0; i < demo_bundle.slot_count; i++) {
        (void)vg_slot_change_tracker_publish(&g_slot_change_tracker, i, NULL);
    }

    while (true) {
        double time_s = (double)(monotonic_now_ns() - app_start_ns) / 1e9;
        if (!fx_wait_for_frame_pacing(window,
                                          runtime_flags.use_mfb_waitsync,
                                          target_frame_ns,
                                          &next_frame_deadline_ns,
                                          &waitsync_stats)) {
            break;
        }

        if (!fx_host_window_pump_events(window)) {
            break;
        }
        const uint8_t *keys = fx_host_window_get_key_buffer(window);
        if (fx_should_exit_for_keys(keys)) {
            break;
        }
        fx_update_runtime_flags(keys, &runtime_flags, &next_frame_deadline_ns, target_frame_ns);
        fx_update_redraw_overlay_toggle(keys, &runtime_flags);
        fx_simulate_gpio_keys(keys, &runtime_flags);
        fx_sync_and_publish_configured_slots(&demo_bundle,
                                             &spatial_rules,
                                             &g_slot_change_tracker,
                                             true);

        ViewerFrameRenderResult frame_result = fx_poll_render_frame();
        bool has_new_render_frame = frame_result.frame_serial != last_seen_render_frame_serial;
        if (has_new_render_frame) {
            last_seen_render_frame_serial = frame_result.frame_serial;
        }
        VgClipRect overlay_rects[FX_MAX_DIRTY_PLAN_RECTS] = {0};
        uint_fast32_t transfer_frame_serial = 0u;
        size_t overlay_count = fx_take_pending_overlay_rects(&last_presented_overlay_frame_serial,
                                                             overlay_rects,
                                                             FX_MAX_DIRTY_PLAN_RECTS,
                                                             &transfer_frame_serial);
        bool has_new_transfer_frame = transfer_frame_serial != 0u;
        if (!g_render_thread.collision_in_render_thread &&
            fx_should_run_collision_step(frame_result.frame_serial, &last_collision_frame_serial)) {
            VgClipRect collision_dirty_rects[FX_MAX_DIRTY_PLAN_RECTS] = {0};
            size_t collision_dirty_count = fx_collect_collision_dirty_rects(collision_dirty_rects,
                                                                                FX_MAX_DIRTY_PLAN_RECTS);
            (void)fx_collision_detect_step(&demo_bundle,
                                               &spatial_rules,
                                               platform_current_time_ms(),
                                               collision_dirty_rects,
                                               collision_dirty_count);
        }

        if (has_new_transfer_frame || !has_presented_window_frame) {
            fx_expand_rgb565_to_window(fb_pixels, window_pixels, (size_t)VIEW_W * (size_t)VIEW_H);
        }
        if (has_new_transfer_frame) {
            if (runtime_flags.redraw_overlay_enabled && overlay_count > 0u) {
                fx_draw_redraw_overlay(window_pixels, VIEW_W, VIEW_H, overlay_rects, overlay_count);
            }
        }

        if (has_new_transfer_frame) {
            uint32_t presented_dirty_pixels = 0u;
            for (size_t i = 0; i < overlay_count; i++) {
                presented_dirty_pixels +=
                    fx_clip_rect_area_on_framebuffer(overlay_rects[i], VIEW_W, VIEW_H);
            }
            perf_window_record_frame(&perf_window,
                                     presented_dirty_pixels,
                                     frame_result.changed_slots,
                                     (uint32_t)overlay_count,
                                     frame_result.transfer_ns);
        }

        ViewerPerfSnapshot perf_snapshot;
        bool perf_ready = perf_window_take_snapshot_if_due(&perf_window, time_s, &perf_snapshot);
        if (perf_ready) {
            collect_render_thread_metrics(&perf_snapshot);
        }
#if defined(__APPLE__)
        if (perf_ready) {
            double dt_avg_ms = timing_accumulator_avg_ms(&frame_dt_stats);
            double dt_max_ms = timing_accumulator_max_ms(&frame_dt_stats);
            double ws_avg_ms = timing_accumulator_avg_ms(&waitsync_stats);
            double up_avg_ms = timing_accumulator_avg_ms(&update_stats);
            timing_accumulator_reset(&frame_dt_stats);
            timing_accumulator_reset(&waitsync_stats);
            timing_accumulator_reset(&update_stats);
            /*
             * Keep the title intentionally short: macOS truncates long titles,
             * and skip diagnostics should stay visible even in narrow windows.
             */
            double full_frame_kb = (double)(VIEW_W * VIEW_H * RGB565_BYTES_PER_PIXEL) / 1024.0;
            double max_bw_kb = perf_snapshot.max_dirty_bytes_per_frame / 1024.0;
            double spi_mib_s = bytes_per_second_to_mib_per_second(perf_snapshot.dirty_bytes_per_s);
            char title[200];
            if (perf_snapshot.fps <= 0.0) {
                (void)snprintf(title,
                               sizeof(title),
                               "IDLE spi %.2fMiB/s tx %.1f/s tm %.2f bw %.1f/%.0fKB sk %llu/%llu lf %u dmx %.1f lk %.0fus dt %.1f up %.1f",
                               spi_mib_s,
                               perf_snapshot.transfer_rects_per_s,
                               perf_snapshot.avg_transfer_ms_per_frame,
                               max_bw_kb,
                               full_frame_kb,
                               (unsigned long long)perf_snapshot.skipped_generations,
                               (unsigned long long)perf_snapshot.skipped_max_frame,
                               long_frame_count,
                               dt_max_ms,
                               perf_snapshot.max_render_lock_hold_us,
                               dt_avg_ms,
                               up_avg_ms);
            } else if (runtime_flags.use_mfb_waitsync || runtime_flags.redraw_overlay_enabled) {
                (void)snprintf(title,
                               sizeof(title),
                               "[%s%s] FPS %.1f spi %.2fMiB/s tx %.1f/s tm %.2f bw %.1f/%.0fKB sk %llu/%llu lf %u dmx %.1f lk %.0fus dt %.1f up %.1f",
                               runtime_flags.use_mfb_waitsync ? "WAITSYNC" : "",
                               runtime_flags.redraw_overlay_enabled
                                   ? (runtime_flags.use_mfb_waitsync ? "+R" : "R")
                                   : "",
                               perf_snapshot.fps,
                               spi_mib_s,
                               perf_snapshot.transfer_rects_per_s,
                               perf_snapshot.avg_transfer_ms_per_frame,
                               max_bw_kb,
                               full_frame_kb,
                               (unsigned long long)perf_snapshot.skipped_generations,
                               (unsigned long long)perf_snapshot.skipped_max_frame,
                               long_frame_count,
                               dt_max_ms,
                               perf_snapshot.max_render_lock_hold_us,
                               dt_avg_ms,
                               up_avg_ms);
            } else {
                (void)snprintf(title,
                               sizeof(title),
                               "FPS %.1f spi %.2fMiB/s tx %.1f/s tm %.2f bw %.1f/%.0fKB sk %llu/%llu lf %u dmx %.1f lk %.0fus dt %.1f up %.1f",
                               perf_snapshot.fps,
                               spi_mib_s,
                               perf_snapshot.transfer_rects_per_s,
                               perf_snapshot.avg_transfer_ms_per_frame,
                               max_bw_kb,
                               full_frame_kb,
                               (unsigned long long)perf_snapshot.skipped_generations,
                               (unsigned long long)perf_snapshot.skipped_max_frame,
                               long_frame_count,
                               dt_max_ms,
                               perf_snapshot.max_render_lock_hold_us,
                               dt_avg_ms,
                               up_avg_ms);
            }
            macos_fx_set_window_title(title);
            if (fx_perf_stderr_diag_enabled()) {
                if (perf_snapshot.skipped_generations > 0u) {
                    fprintf(stderr,
                            "[viewer] skip-diag: total=%llu frame-max=%llu slot-max=%llu "
                            "lock-max=%.0fus fps=%.1f dt=%.1f ws=%.1f up=%.1f\n",
                            (unsigned long long)perf_snapshot.skipped_generations,
                            (unsigned long long)perf_snapshot.skipped_max_frame,
                            (unsigned long long)perf_snapshot.skipped_max_slot,
                            perf_snapshot.max_render_lock_hold_us,
                            perf_snapshot.fps,
                            dt_avg_ms,
                            ws_avg_ms,
                            up_avg_ms);
                }
                if (long_frame_count > 0u) {
                    fprintf(stderr,
                            "[viewer] stutter-diag: long=%u dt-max=%.1fms "
                            "fps=%.1f dt=%.1f ws=%.1f up=%.1f\n",
                            long_frame_count,
                            dt_max_ms,
                            perf_snapshot.fps,
                            dt_avg_ms,
                            ws_avg_ms,
                            up_avg_ms);
                }
            }
            long_frame_count = 0u;
        }
#else
        (void)perf_snapshot;
        (void)perf_ready;
#endif

        uint64_t now_ns = monotonic_now_ns();
        if (last_present_ns > 0u) {
            uint64_t dt = (now_ns > last_present_ns) ? (now_ns - last_present_ns) : 0u;
            timing_accumulator_add(&frame_dt_stats, dt);
            if (dt > 20000000ull) {
                long_frame_count++;
            }
        }
        last_present_ns = now_ns;

        if (has_new_transfer_frame || !has_presented_window_frame) {
            uint64_t update_begin_ns = monotonic_now_ns();
            mfb_update_state state = fx_host_window_update(window, window_pixels, VIEW_W, VIEW_H);
            uint64_t update_end_ns = monotonic_now_ns();
            uint64_t update_ns = (update_end_ns > update_begin_ns)
                                     ? (update_end_ns - update_begin_ns)
                                     : 0u;
            timing_accumulator_add(&update_stats, update_ns);
            if (state != STATE_OK) {
                break;
            }
            has_presented_window_frame = true;
        }
        if (startup_callback_pending) {
            if (!event_loop_enqueue_ingress_call((CljObject *)demo_bundle.startup_callback, NULL)) {
                fprintf(stderr, "Failed to enqueue viewer startup callback\n");
                break;
            }
            startup_callback_pending = false;
        }
    }

    exit_code = 0;

cleanup:
#if defined(__APPLE__)
    if (window) {
        macos_fx_save_window_position();
    }
    macos_fx_end_performance_activity();
#endif
    if (window) {
        fx_host_window_close(window);
    }
    if (runloop_thread_started) {
        stop_runloop_thread();
    }
    if (render_thread_started) {
        (void)tiny_renderer_lifecycle_stop();
    }
    tiny_renderer_lifecycle_set_callbacks(NULL, NULL, NULL);
    if (slot_runtime_initialized) {
        fx_destroy_slot_runtime_buffers();
    }
    if (slot_tracker_initialized) {
        vg_slot_change_tracker_destroy(&g_slot_change_tracker);
    }
    if (demo_bundle_initialized) {
        destroy_scene_bundle(&demo_bundle);
    }
    destroy_spatial_rule_set(&spatial_rules);
    g_gram_pixels = NULL;
    memset(&g_gram_panel_ctx, 0, sizeof(g_gram_panel_ctx));
    memset(&g_gram_panel, 0, sizeof(g_gram_panel));
    runtime_reset(&g_runtime);
    return exit_code;
#endif
}
