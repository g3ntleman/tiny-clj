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
