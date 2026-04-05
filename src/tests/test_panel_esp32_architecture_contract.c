#include "tests_common.h"

static char *read_text_file(const char *path, size_t *out_len) {
    if (out_len) *out_len = 0;
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "failed to open source file");
    (void)fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    TEST_ASSERT_TRUE_MESSAGE(sz >= 0, "ftell failed");
    (void)fseek(f, 0, SEEK_SET);

    char *buf = (char *)CLJ_MALLOC((size_t)sz + 1u);
    TEST_ASSERT_NOT_NULL_MESSAGE(buf, "allocation failed");
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)sz, (uint64_t)n);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static char *read_tinyclj_idf_display_source(size_t *out_len) {
    const char *candidates[] = {
        "esp32-idf/main/tinyclj_idf_display.c",
        "../esp32-idf/main/tinyclj_idf_display.c",
        "../../esp32-idf/main/tinyclj_idf_display.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate esp32-idf/main/tinyclj_idf_display.c (tried cwd-relative candidates)");
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

static char *read_tinyclj_component_cmakelists_source(size_t *out_len) {
    const char *candidates[] = {
        "esp32-idf/components/tinyclj/CMakeLists.txt",
        "../esp32-idf/components/tinyclj/CMakeLists.txt",
        "../../esp32-idf/components/tinyclj/CMakeLists.txt"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate esp32-idf/components/tinyclj/CMakeLists.txt (tried cwd-relative candidates)");
    return NULL;
}

static char *read_tinyclj_idf_compile_flags_source(size_t *out_len) {
    const char *candidates[] = {
        "esp32-idf/cmake/tinyclj_idf_compile_flags.cmake",
        "../esp32-idf/cmake/tinyclj_idf_compile_flags.cmake",
        "../../esp32-idf/cmake/tinyclj_idf_compile_flags.cmake"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate esp32-idf/cmake/tinyclj_idf_compile_flags.cmake (tried cwd-relative candidates)");
    return NULL;
}

static char *read_tinyclj_idf_renderer_source(size_t *out_len) {
    const char *candidates[] = {
        "esp32-idf/main/tinyclj_idf_renderer.c",
        "../esp32-idf/main/tinyclj_idf_renderer.c",
        "../../esp32-idf/main/tinyclj_idf_renderer.c"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate esp32-idf/main/tinyclj_idf_renderer.c (tried cwd-relative candidates)");
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

static char *read_scene_header_source(size_t *out_len) {
    const char *candidates[] = {
        "src/scene.h",
        "../src/scene.h",
        "../../src/scene.h"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate src/scene.h (tried cwd-relative candidates)");
    return NULL;
}

TEST(test_panel_esp32_architecture_uses_board_profile_and_panel_adapter) {
    size_t len = 0;
    char *src = read_tinyclj_idf_display_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "#include \"vector_handheld_config.h\""),
                                 "expected display bootstrap to use board profile");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "#include \"panel_esp_lcd.h\""),
                                 "expected display bootstrap to wrap esp_lcd panels via panel_esp_lcd");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "vg_esp_lcd_panel_init_handle"),
                                 "expected esp_lcd handle to be exposed through VgPanel");

    CLJ_FREE(src);
}

TEST(test_panel_esp32_architecture_bootstraps_st7789_over_spi) {
    size_t len = 0;
    char *src = read_tinyclj_idf_display_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "spi_bus_initialize"),
                                 "expected SPI bus bootstrap for ST7789 panel");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "esp_lcd_new_panel_io_spi"),
                                 "expected SPI panel IO creation");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "esp_lcd_new_panel_st7789"),
                                 "expected ST7789 panel creation");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "VG_PIN_TFT_SCLK"),
                                 "expected board-profile SPI pin wiring");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "VG_PIN_TFT_MOSI"),
                                 "expected board-profile MOSI pin wiring");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "VG_PIN_TFT_CS"),
                                 "expected board-profile CS pin wiring");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "VG_PIN_TFT_DC"),
                                 "expected board-profile DC pin wiring");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "VG_PIN_TFT_RST"),
                                 "expected board-profile RST pin wiring");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "VG_TFT_SPI_HZ"),
                                 "expected board-profile SPI frequency");

    CLJ_FREE(src);
}

TEST(test_panel_esp32_architecture_initializes_panel_before_exposing_it) {
    size_t len = 0;
    char *src = read_tinyclj_idf_display_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "vg_panel_reset(&display->panel.panel)"),
                                 "expected panel reset during display bootstrap");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "vg_panel_init(&display->panel.panel)"),
                                 "expected panel init during display bootstrap");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "vg_panel_set_display_enabled(&display->panel.panel, true)"),
                                 "expected display enable during display bootstrap");

    CLJ_FREE(src);
}

TEST(test_panel_esp32_architecture_bootstraps_display_from_app_main_for_tiny_fx) {
    size_t len = 0;
    char *src = read_tinyclj_idf_main_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "#include \"tinyclj_idf_display.h\""),
                                 "expected app_main to know the display bootstrap header");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "tinyclj_idf_display_bootstrap()"),
                                 "expected app_main to bootstrap the ESP display for tiny-fx");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "TINY_FX_ENABLED"),
                                 "expected display bootstrap to stay gated behind tiny-fx builds");

    CLJ_FREE(src);
}

TEST(test_panel_esp32_architecture_keeps_sound_sources_outside_tiny_fx_bundle) {
    size_t len = 0;
    char *src = read_tinyclj_component_cmakelists_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "set(TINYCLJ_SOUND_SRCS"),
                                 "expected dedicated source list for sound runtime");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "\"${TINYCLJ_SRC_DIR}/sound_tick_scheduler.c\""),
                                 "expected sound scheduler in the sound source list");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "\"${TINYCLJ_SRC_DIR}/sound_engine.c\""),
                                 "expected sound engine in the sound source list");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "\"${TINYCLJ_SRC_DIR}/sound_backend_esp32.c\""),
                                 "expected sound backend in the sound source list");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "if(TINYCLJ_SOUND_ENABLED)"),
                                 "expected sound sources to be gated by TINYCLJ_SOUND_ENABLED");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "list(APPEND TINYCLJ_SRCS ${TINYCLJ_SOUND_SRCS})"),
                                 "expected sound source bundle to be appended independently");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "\"${TINYCLJ_SRC_DIR}/rendered_state_snapshot.c\""),
                                 "expected rendered snapshot to remain in tiny-fx-specific sources");

    CLJ_FREE(src);
}

TEST(test_panel_esp32_architecture_enables_sound_for_tiny_clj_product_builds) {
    size_t len = 0;
    char *src = read_tinyclj_idf_compile_flags_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "set(TINYCLJ_SOUND_ENABLED ON CACHE BOOL"),
                                 "expected product compile flags to keep sound enabled");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "idf_build_set_property(COMPILE_DEFINITIONS \"TINYCLJ_SOUND_ENABLED=1\" APPEND)"),
                                 "expected compile definitions to export TINYCLJ_SOUND_ENABLED=1");

    CLJ_FREE(src);
}

TEST(test_panel_esp32_architecture_renderer_uses_shared_render_driver) {
    size_t len = 0;
    char *src = read_tinyclj_idf_renderer_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "#include \"render_driver.h\""),
                                 "expected ESP renderer wiring to include shared render driver API");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "vg_render_driver_start"),
                                 "expected renderer lifecycle start callback to call vg_render_driver_start");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "tiny_renderer_lifecycle_set_callbacks"),
                                 "expected renderer module to register lifecycle callbacks");

    CLJ_FREE(src);
}

TEST(test_panel_esp32_architecture_renderer_exposes_tuning_baselines) {
    size_t len = 0;
    char *src = read_tinyclj_idf_renderer_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "#define TINYCLJ_RENDER_STRIPE_ROWS 30"),
                                 "expected ESP renderer to default stripe rows to calibrated baseline (30)");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "TINYCLJ_RENDER_THREAD_STACK_BYTES"),
                                 "expected ESP renderer to expose a configurable render-task stack size");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "TINYCLJ_RENDER_THREAD_PRIORITY"),
                                 "expected ESP renderer to expose a configurable render-task priority");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "__attribute__((aligned(4)))"),
                                 "expected stripe pixel buffer to stay DMA-aligned");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "stack_high_water_mark_bytes"),
                                 "expected stop callback to report render-task stack high-water mark");

    CLJ_FREE(src);
}

TEST(test_panel_esp32_architecture_run_registers_renderer_lifecycle) {
    size_t len = 0;
    char *src = read_tinyclj_idf_run_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "#include \"tinyclj_idf_renderer.h\""),
                                 "expected run entry to include the renderer wiring header");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "tinyclj_idf_renderer_init"),
                                 "expected run entry to initialize renderer wiring before REPL loop");

    CLJ_FREE(src);
}

TEST(test_panel_esp32_architecture_display_configures_orientation) {
    size_t len = 0;
    char *src = read_tinyclj_idf_display_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "vg_panel_set_orientation"),
                                 "expected display bootstrap to configure panel orientation");

    CLJ_FREE(src);
}

TEST(test_panel_esp32_architecture_slot_tracker_uses_subjective_c_thread_primitives) {
    size_t len = 0;
    char *src = read_scene_header_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "#include \"thread.h\""),
                                 "expected slot tracker to include subjective-c threading API");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "SubjectiveCMutex *"),
                                 "expected slot tracker to store SubjectiveCMutex");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "pthread_mutex_t"),
                             "slot tracker should no longer expose pthread mutexes");

    CLJ_FREE(src);
}
