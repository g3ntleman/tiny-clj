#ifndef TEST_BREAKOUT_HELPERS_H
#define TEST_BREAKOUT_HELPERS_H

#include "tests_common.h"

static char *breakout_read_text_file(const char *path, size_t *out_len) {
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

static char *read_breakout_source(const char *basename, size_t *out_len) {
    char path1[256];
    char path2[256];
    char path3[256];
    test_snprintf(path1, sizeof(path1), "libs/tiny-breakout/%s", basename);
    test_snprintf(path2, sizeof(path2), "../libs/tiny-breakout/%s", basename);
    test_snprintf(path3, sizeof(path3), "../../libs/tiny-breakout/%s", basename);
    const char *candidates[] = {path1, path2, path3};
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return breakout_read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate libs/tiny-breakout source");
    return NULL;
}

static char *read_deployment_source(size_t *out_len) __attribute__((unused));
static char *read_deployment_source(size_t *out_len) {
    const char *candidates[] = {
        "libs/tiny-clj/deployment.clj",
        "../libs/tiny-clj/deployment.clj",
        "../../libs/tiny-clj/deployment.clj"
    };
    for (unsigned int i = 0; i < (unsigned int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        FILE *probe = fopen(candidates[i], "rb");
        if (!probe) continue;
        fclose(probe);
        return breakout_read_text_file(candidates[i], out_len);
    }
    TEST_FAIL_MESSAGE("failed to locate libs/tiny-clj/deployment.clj");
    return NULL;
}

#endif
