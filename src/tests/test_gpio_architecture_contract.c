/*
 * Architecture contract tests for ESP32 GPIO integration.
 *
 * These tests guard structural constraints from the plan:
 * - ISR path must not use old direct-scheduling helper.
 * - ISR path should set a drain-request flag and use thread-context polling.
 */

#include "tests_common.h"

static char *read_text_file(const char *path, size_t *out_len) {
    if (out_len) *out_len = 0;
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "failed to open source file");
    (void)fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    TEST_ASSERT_TRUE_MESSAGE(sz >= 0, "ftell failed");
    (void)fseek(f, 0, SEEK_SET);

    char *buf = (char*)CLJ_MALLOC((size_t)sz + 1u);
    TEST_ASSERT_NOT_NULL_MESSAGE(buf, "allocation failed");
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)sz, (uint64_t)n);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static char *read_gpio_esp32_source(size_t *out_len) {
    const char *candidates[] = {
        "src/gpio_esp32.c",
        "../src/gpio_esp32.c",
        "../../src/gpio_esp32.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate src/gpio_esp32.c (tried cwd-relative candidates)");
    return NULL;
}

static char *read_gpio_core_source(size_t *out_len) {
    const char *candidates[] = {
        "src/gpio.c",
        "../src/gpio.c",
        "../../src/gpio.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate src/gpio.c (tried cwd-relative candidates)");
    return NULL;
}

static char *read_gpio_host_source(size_t *out_len) {
    const char *candidates[] = {
        "src/gpio_host.c",
        "../src/gpio_host.c",
        "../../src/gpio_host.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate src/gpio_host.c (tried cwd-relative candidates)");
    return NULL;
}

static char *read_gpio_lib_source(size_t *out_len) {
    const char *candidates[] = {
        "libs/tiny-clj/gpio.clj",
        "../libs/tiny-clj/gpio.clj",
        "../../libs/tiny-clj/gpio.clj"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate libs/tiny-clj/gpio.clj (tried cwd-relative candidates)");
    return NULL;
}

static char *read_sound_backend_esp32_source(size_t *out_len) {
    const char *candidates[] = {
        "src/sound_backend_esp32.c",
        "../src/sound_backend_esp32.c",
        "../../src/sound_backend_esp32.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate src/sound_backend_esp32.c (tried cwd-relative candidates)");
    return NULL;
}

static char *read_tinyclj_idf_run_source(size_t *out_len) {
    const char *candidates[] = {
        "esp32-idf/main/tinyclj_idf_run.c",
        "../esp32-idf/main/tinyclj_idf_run.c",
        "../../esp32-idf/main/tinyclj_idf_run.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate esp32-idf/main/tinyclj_idf_run.c (tried cwd-relative candidates)");
    return NULL;
}

static char *read_tinyclj_idf_main_source(size_t *out_len) {
    const char *candidates[] = {
        "esp32-idf/main/tinyclj_idf_main.c",
        "../esp32-idf/main/tinyclj_idf_main.c",
        "../../esp32-idf/main/tinyclj_idf_main.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate esp32-idf/main/tinyclj_idf_main.c (tried cwd-relative candidates)");
    return NULL;
}

static char *read_fx_host_runloop_source(size_t *out_len) {
    const char *candidates[] = {
        "src/fx_host_runloop.c",
        "../src/fx_host_runloop.c",
        "../../src/fx_host_runloop.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate src/fx_host_runloop.c (tried cwd-relative candidates)");
    return NULL;
}

static char *read_builtins_source(size_t *out_len) {
    const char *candidates[] = {
        "src/builtins.c",
        "../src/builtins.c",
        "../../src/builtins.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate src/builtins.c (tried cwd-relative candidates)");
    return NULL;
}

static char *read_event_loop_source(size_t *out_len) {
    const char *candidates[] = {
        "src/event_loop.c",
        "../src/event_loop.c",
        "../../src/event_loop.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate src/event_loop.c (tried cwd-relative candidates)");
    return NULL;
}

TEST(test_gpio_architecture_uses_request_flag_polling) {
    size_t len = 0;
    char *src = read_gpio_esp32_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "g_gpio_drain_requested"),
                                 "expected drain-request flag for ISR->thread bridge");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_esp32_poll_drain"),
                                 "expected thread-context drain polling function");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "gpio_schedule_drain_from_isr"),
                             "legacy direct-scheduling ISR helper should be removed");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_isr_section_has_no_direct_enqueue) {
    size_t len = 0;
    char *src = read_gpio_esp32_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    const char *isr_start = strstr(src, "static void IRAM_ATTR gpio_isr_handler");
    TEST_ASSERT_NOT_NULL_MESSAGE(isr_start, "gpio_isr_handler not found");

    const char *drain_comment = strstr(isr_start, "/** Drain all queued GPIO events");
    TEST_ASSERT_NOT_NULL_MESSAGE(drain_comment, "drain-events section marker not found");

    size_t isr_len = (size_t)(drain_comment - isr_start);
    TEST_ASSERT_TRUE(isr_len > 0);

    char *isr_slice = (char*)CLJ_MALLOC(isr_len + 1u);
    TEST_ASSERT_NOT_NULL(isr_slice);
    memcpy(isr_slice, isr_start, isr_len);
    isr_slice[isr_len] = '\0';

    TEST_ASSERT_NULL_MESSAGE(strstr(isr_slice, "event_loop_enqueue"),
                             "ISR section must not enqueue directly");

    CLJ_FREE(isr_slice);
    CLJ_FREE(src);
}

TEST(test_gpio_architecture_isr_requests_runloop_wakeup) {
    size_t len = 0;
    char *src = read_gpio_esp32_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    const char *helper_start = strstr(src, "static inline void gpio_request_drain_from_isr");
    TEST_ASSERT_NOT_NULL_MESSAGE(helper_start, "gpio_request_drain_from_isr helper not found");

    const char *helper_end = strstr(helper_start, "void gpio_esp32_poll_drain");
    TEST_ASSERT_NOT_NULL_MESSAGE(helper_end, "gpio_esp32_poll_drain marker not found");

    size_t helper_len = (size_t)(helper_end - helper_start);
    TEST_ASSERT_TRUE(helper_len > 0);

    char *helper_slice = (char *)CLJ_MALLOC(helper_len + 1u);
    TEST_ASSERT_NOT_NULL(helper_slice);
    memcpy(helper_slice, helper_start, helper_len);
    helper_slice[helper_len] = '\0';

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(helper_slice, "event_loop_wake("),
                                 "ISR drain-request helper should wake blocking runloop");

    CLJ_FREE(helper_slice);
    CLJ_FREE(src);
}

TEST(test_gpio_architecture_esp32_irq_consumers_cover_c_callbacks_and_watchers) {
    size_t len = 0;
    char *src = read_gpio_esp32_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_esp32_input_irq_consumer_acquire"),
                                 "expected shared ESP32 IRQ consumer acquire helper");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_esp32_input_irq_consumer_release"),
                                 "expected shared ESP32 IRQ consumer release helper");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_runtime_pin_has_c_callbacks("),
                                 "expected ISR path to consider raw C callbacks");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_runtime_pin_has_watcher("),
                                 "expected ISR/drain path to consider Clojure watchers");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_native_builtins_live_in_shared_core) {
    size_t core_len = 0;
    size_t host_len = 0;
    size_t esp32_len = 0;
    char *core_src = read_gpio_core_source(&core_len);
    char *host_src = read_gpio_host_source(&host_len);
    char *esp32_src = read_gpio_esp32_source(&esp32_len);
    TEST_ASSERT_TRUE(core_len > 0);
    TEST_ASSERT_TRUE(host_len > 0);
    TEST_ASSERT_TRUE(esp32_len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(core_src, "ID native_gpio_watch"),
                                 "shared gpio core should own native_gpio_watch");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(core_src, "ID native_gpio_pwm"),
                                 "shared gpio core should own native_gpio_pwm");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(core_src, "bool gpio_pwm_start_or_update"),
                                 "shared gpio core should expose direct C PWM API");
    TEST_ASSERT_NULL_MESSAGE(strstr(host_src, "ID native_gpio_watch"),
                             "host backend should not own native_gpio_watch anymore");
    TEST_ASSERT_NULL_MESSAGE(strstr(esp32_src, "ID native_gpio_watch"),
                             "esp32 backend should not own native_gpio_watch anymore");

    CLJ_FREE(core_src);
    CLJ_FREE(host_src);
    CLJ_FREE(esp32_src);
}

TEST(test_gpio_architecture_sound_backend_uses_shared_pwm_backend) {
    size_t len = 0;
    char *src = read_sound_backend_esp32_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_pwm_start_or_update"),
                                 "sound backend should use shared gpio PWM backend");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_pwm_stop"),
                                 "sound backend should use shared gpio PWM stop backend");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "g_sound_engine.voice_count"),
                                 "sound backend should derive active voices from shared engine state");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "gpio_esp32_pwm_start_or_update"),
                             "sound backend should not depend on ESP32 backend header directly");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "bool          initialized;"),
                             "sound backend should not keep separate per-voice initialized ownership state");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "ledc_set_freq"),
                             "sound backend should not drive LEDC frequency directly anymore");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "ledc_channel_config"),
                             "sound backend should not configure LEDC channels directly anymore");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "for (uint32_t i = 0; i < due; i++)"),
                                 "sound backend callback should advance due ticks one by one");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "sound_engine_advance_ticks"),
                             "sound backend should not batch-advance ticks anymore");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_sound_backend_normalizes_zero_tick_rearm) {
    size_t len = 0;
    char *src = read_sound_backend_esp32_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "sound_backend_normalize_due_ticks"),
                                 "sound backend should normalize zero-tick rearm requests");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "uint32_t normalized_ticks = sound_backend_normalize_due_ticks(ticks);"),
                                 "sound backend should normalize ticks before scheduling esp_timer");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "sound_backend_ticks_to_delay_us(normalized_ticks)"),
                                 "sound backend should arm esp_timer using normalized tick delays");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "if (ticks == 0u) {\n        return 1u;\n    }\n    return (uint64_t)ticks *"),
                             "sound backend should not map zero ticks to a 1us spin delay");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_sound_backend_keeps_pwm_duty_at_half_scale_max) {
    size_t len = 0;
    char *src = read_sound_backend_esp32_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "sound_backend_volume_to_half_duty"),
                                 "sound backend should map voice volume through a dedicated half-duty helper");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "uint8_t duty8 = (freq_hz > 0) ? sound_backend_volume_to_half_duty(volume) : 0;"),
                                 "sound backend should derive per-voice duty via the capped half-duty helper");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "int32_t gpio_duty = (int32_t)duty8;"),
                                 "sound backend should keep GPIO duty within 0..127 (max ~50%)");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "SOUND_PWM_MIN_STABLE_DUTY"),
                                 "sound backend should define a minimum stable nonzero PWM duty");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "if (boosted < SOUND_PWM_MIN_STABLE_DUTY)"),
                                 "sound backend should clamp very low duty levels to zero to avoid unstable overtone tails");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "int32_t gpio_duty = ((int32_t)duty8 * 255 + 63) / 127;"),
                             "sound backend should not remap duty8 back to full 0..255 scale");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_runtime_uses_shared_gpio_api_above_backend) {
    size_t builtins_len = 0;
    size_t event_loop_len = 0;
    char *builtins_src = read_builtins_source(&builtins_len);
    char *event_loop_src = read_event_loop_source(&event_loop_len);
    TEST_ASSERT_TRUE(builtins_len > 0);
    TEST_ASSERT_TRUE(event_loop_len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(builtins_src, "gpio_get_event_drop_count"),
                                 "builtins should use shared gpio API for drop counts");
    TEST_ASSERT_NULL_MESSAGE(strstr(builtins_src, "#include \"gpio_esp32.h\""),
                             "builtins should not include gpio_esp32.h directly");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(event_loop_src, "gpio_poll_drain"),
                                 "event loop should use shared gpio poll API");
    TEST_ASSERT_NULL_MESSAGE(strstr(event_loop_src, "#include \"gpio_esp32.h\""),
                             "event loop should not include gpio_esp32.h directly");

    CLJ_FREE(builtins_src);
    CLJ_FREE(event_loop_src);
}

TEST(test_gpio_architecture_esp32_repl_wait_uses_blocking_event_loop_driver) {
    size_t len = 0;
    char *src = read_tinyclj_idf_run_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "tinyclj_esp32_uart_has_pending_input()"),
                                 "ESP32 REPL wait loop should drain queued UART input before blocking runloop waits");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "event_loop_run(NULL, st)"),
                                 "ESP32 REPL wait loop should use blocking event_loop_run");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "repl_process_event_loop(st)"),
                             "ESP32 REPL wait loop should not poll run_next via repl_process_event_loop");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "platform_sleep_ms(esp_repl_idle_sleep_ms())"),
                             "ESP32 REPL wait loop should not use timed sleep polling");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_esp32_gpio_ingress_path_is_renderer_independent) {
    size_t gpio_len = 0;
    size_t run_len = 0;
    size_t loop_len = 0;
    char *gpio_src = read_gpio_esp32_source(&gpio_len);
    char *run_src = read_tinyclj_idf_run_source(&run_len);
    char *loop_src = read_event_loop_source(&loop_len);
    TEST_ASSERT_TRUE(gpio_len > 0);
    TEST_ASSERT_TRUE(run_len > 0);
    TEST_ASSERT_TRUE(loop_len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(gpio_src, "gpio_request_drain_from_isr();"),
                                 "ESP32 ISR should request deferred GPIO drain");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(gpio_src, "event_loop_wake()"),
                                 "ESP32 ISR drain request should wake the blocking event loop");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(gpio_src, "gpio_runtime_enqueue_watch_event(ev.pin, ev.value)"),
                                 "ESP32 drain path should enqueue GPIO watch events into shared ingress");
    TEST_ASSERT_NULL_MESSAGE(strstr(gpio_src, "renderer"),
                             "ESP32 GPIO backend should not depend on renderer symbols");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(run_src, "event_loop_run(NULL, st)"),
                                 "ESP32 interpreter loop should use blocking event-loop driver");

    const char *run_next_start = strstr(loop_src, "bool event_loop_run_next");
    TEST_ASSERT_NOT_NULL_MESSAGE(run_next_start, "event_loop_run_next should exist in shared event-loop core");
    const char *drain_call = strstr(run_next_start, "gpio_poll_drain();");
    TEST_ASSERT_NOT_NULL_MESSAGE(drain_call, "runloop step should always drain pending GPIO input");
    const char *timer_call = strstr(run_next_start, "timer_process();");
    TEST_ASSERT_NOT_NULL_MESSAGE(timer_call, "runloop step should process timers");
    TEST_ASSERT_TRUE_MESSAGE(drain_call < timer_call,
                             "GPIO drain should happen before timer/task processing in each runloop step");

    CLJ_FREE(gpio_src);
    CLJ_FREE(run_src);
    CLJ_FREE(loop_src);
}

TEST(test_gpio_architecture_host_and_esp32_share_blocking_runloop_driver_callsite) {
    size_t host_len = 0;
    size_t esp32_len = 0;
    char *host_src = read_fx_host_runloop_source(&host_len);
    char *esp32_src = read_tinyclj_idf_run_source(&esp32_len);
    TEST_ASSERT_TRUE(host_len > 0);
    TEST_ASSERT_TRUE(esp32_len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(host_src, "event_loop_run(NULL, st)"),
                                 "Host runloop thread should call blocking event_loop_run");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(esp32_src, "event_loop_run(NULL, st)"),
                                 "ESP32 REPL wait loop should call blocking event_loop_run");
    TEST_ASSERT_NULL_MESSAGE(strstr(esp32_src, "event_loop_run_next("),
                             "ESP32 REPL loop should not maintain a private run_next polling driver");
    TEST_ASSERT_NULL_MESSAGE(strstr(esp32_src, "event_loop_time_until_next_timer_ms("),
                             "ESP32 REPL loop should not maintain private timer polling heuristics");

    CLJ_FREE(host_src);
    CLJ_FREE(esp32_src);
}

TEST(test_gpio_architecture_esp32_adapter_wakes_shared_runloop_without_private_driver_logic) {
    size_t main_len = 0;
    char *main_src = read_tinyclj_idf_main_source(&main_len);
    TEST_ASSERT_TRUE(main_len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(main_src, "event_loop_wake();"),
                                 "ESP32 UART adapter should wake the shared blocking runloop");
    TEST_ASSERT_NULL_MESSAGE(strstr(main_src, "event_loop_run_next("),
                             "ESP32 UART adapter should not execute a private run_next polling loop");
    TEST_ASSERT_NULL_MESSAGE(strstr(main_src, "event_loop_time_until_next_timer_ms("),
                             "ESP32 UART adapter should not implement private timer polling");

    CLJ_FREE(main_src);
}

TEST(test_gpio_architecture_watch_events_include_signal_discriminators) {
    size_t len = 0;
    char *src = read_gpio_lib_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, ":signal :digital"),
                                 "public gpio library should document digital watch events with :signal :digital");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, ":signal :analog"),
                                 "public gpio library should document analog watch events with :signal :analog");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, ":kind :analog"),
                                 "public gpio library should keep analog :kind information alongside :signal");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_esp32_adc_channel_config_is_cached) {
    size_t len = 0;
    char *src = read_gpio_esp32_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "g_gpio_adc1_channels_configured"),
                                 "esp32 gpio backend should keep ADC1 channel config cache");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "g_gpio_adc2_channels_configured"),
                                 "esp32 gpio backend should keep ADC2 channel config cache");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_adc_ensure_channel_configured"),
                                 "esp32 gpio backend should centralize ADC channel configuration");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_esp32_digital_and_pwm_paths_use_caches) {
    size_t len = 0;
    char *src = read_gpio_esp32_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "slot->pwm_binding_index"),
                                 "esp32 gpio backend should keep O(1) PWM binding index cache in shared pin-state");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "slot->output_mode_configured"),
                                 "esp32 gpio backend should cache output-mode configuration in shared pin-state");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "slot->watch_input_irq_configured"),
                                 "esp32 gpio backend should cache watch-input GPIO configuration in shared pin-state");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_ensure_output_mode_configured"),
                                 "esp32 gpio backend should centralize output-mode setup");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_ensure_watch_input_irq_configured"),
                                 "esp32 gpio backend should centralize watch-input setup");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_esp32_watcher_runtime_uses_shared_pin_state) {
    size_t len = 0;
    char *src = read_gpio_esp32_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "GpioEsp32PinState g_gpio_pin_state[GPIO_NUM_MAX]"),
                                 "esp32 gpio backend should keep one shared pin-state array");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "slot->watcher_callback"),
                                 "esp32 watcher runtime should store callbacks in the shared pin-state slot");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "slot->pwm_binding_index"),
                                 "esp32 pwm cache should live in the shared pin-state slot");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "slot->output_mode_configured"),
                                 "esp32 output-mode cache should live in the shared pin-state slot");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "slot->watch_input_irq_configured"),
                                 "esp32 watch-input cache should live in the shared pin-state slot");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_runtime_watch_set(pin, callback)"),
                                 "esp32 digital watchers should register in the shared runtime watcher map");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_runtime_watch_clear(pin)"),
                                 "esp32 digital watchers should clear the shared runtime watcher map");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_isr_handler_add((gpio_num_t)pin, gpio_isr_handler, (void*)(intptr_t)pin)"),
                                 "esp32 watcher ISR should pass the pin directly into the shared pin-state lookup");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_esp32_pin_mode_runtime_uses_shared_pin_state) {
    size_t len = 0;
    char *src = read_gpio_core_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "g_gpio_pin_state[pin].mode_entry"),
                                 "shared gpio core should keep ESP32 pin-mode state in the shared pin-state array");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_runtime_pin_slot_valid"),
                                 "shared gpio core should centralize ESP32 pin-slot bounds checks");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_store_pin_mode_entry"),
                                 "shared gpio core should centralize pin-mode shared-slot updates");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_clear_pin_mode_entry"),
                                 "shared gpio core should centralize pin-mode shared-slot clears");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_esp32_runtime_reset_state()"),
                                 "shared gpio core should delegate ESP32 shared pin-state reset to the backend");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_shared_pin_write_routes_dac_through_gpio_core) {
    size_t len = 0;
    char *src = read_gpio_core_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "if (mode == SYM_KW_DAC)"),
                                 "shared gpio core should dispatch pin-write for :dac");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "gpio_write_analog(pin, value);"),
                                 "shared gpio core should route :dac writes through the analog backend API");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, ":dac mode is not implemented yet"),
                             "shared gpio core should no longer keep the :dac placeholder error");

    CLJ_FREE(src);
}

TEST(test_gpio_architecture_esp32_spit_uses_kv_backed_fs_store) {
    size_t len = 0;
    char *src = read_builtins_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "ID native_spit(ID *args, unsigned int argc)"),
                                 "builtins should define native_spit");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "builtin_fs_write_bytes_or_throw(\"spit\", filename_str,"),
                                 "spit should route through the shared FS write helper");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "No-op on ESP32"),
                             "spit must no longer be implemented as a no-op on ESP32");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "FILE *fp = fopen(filename_str, \"w\")"),
                             "spit should no longer keep a separate fopen/fwrite host path");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "fs_write_bytes(st,"),
                             "spit should not duplicate the low-level FS write call");

    CLJ_FREE(src);
}
