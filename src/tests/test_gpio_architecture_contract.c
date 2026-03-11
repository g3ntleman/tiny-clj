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
