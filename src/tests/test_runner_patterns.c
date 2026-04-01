#include "tests_common.h"

#include <sys/wait.h>
#include <unistd.h>

static FILE *open_unit_tests_pipe(const char *pattern) {
    const char *candidates[] = {
        "./build/unit-tests",
        "../build/unit-tests",
        "../../build/unit-tests",
    };

    char cmd[1024];
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (access(candidates[i], X_OK) != 0) {
            continue;
        }
        test_snprintf(cmd,
                      sizeof(cmd),
                      "TINYCLJ_RUNNER_QUIET_PROBE=1 %s --quiet --test '%s' 2>&1",
                      candidates[i],
                      pattern);
        return popen(cmd, "r");
    }
    return NULL;
}

static char *read_pipe_to_string(FILE *pipe, int *out_status) {
    if (!pipe) {
        return NULL;
    }

    char *buffer = CLJ_HOST_MALLOC(8192u);
    if (!buffer) {
        (void)pclose(pipe);
        return NULL;
    }

    size_t used = 0u;
    buffer[0] = '\0';
    while (!feof(pipe) && used + 1u < 8192u) {
        size_t nread = fread(buffer + used, 1u, 8192u - used - 1u, pipe);
        used += nread;
        buffer[used] = '\0';
        if (nread == 0u) {
            break;
        }
    }

    int status = pclose(pipe);
    if (out_status) {
        *out_status = status;
    }
    return buffer;
}

TEST(test_plain_group_pattern_matches_shared_group_name) {
    TEST_ASSERT_TRUE(test_name_matches_pattern(
        "shared_test_equal/equal_vectors",
        "test_equal/*"));
}

TEST(test_shared_group_pattern_matches_plain_group_name) {
    TEST_ASSERT_TRUE(test_name_matches_pattern(
        "test_equal/equal_vectors",
        "shared_test_equal/*"));
}

TEST(test_unrelated_group_pattern_does_not_match_shared_group_name) {
    TEST_ASSERT_FALSE(test_name_matches_pattern(
        "shared_test_equal/equal_vectors",
        "test_map/*"));
}

TEST(test_quiet_mode_subprocess_preserves_assertion_details) {
    FILE *pipe = open_unit_tests_pipe("test_runner_patterns/quiet_output_probe_failure");
    TEST_ASSERT_NOT_NULL_MESSAGE(pipe, "failed to locate unit-tests executable for quiet runner subprocess");

    int status = 0;
    char *output = read_pipe_to_string(pipe, &status);

    TEST_ASSERT_NOT_NULL_MESSAGE(output, "failed to capture quiet runner subprocess output");
    TEST_ASSERT_TRUE_MESSAGE(WIFEXITED(status), "quiet runner subprocess should exit normally");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, WEXITSTATUS(status), output);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(output, "test_runner_patterns/quiet_output_probe_failure:FAIL"), output);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(output, "quiet probe assertion message"), output);

    CLJ_HOST_FREE(output);
}

TEST(test_quiet_output_probe_failure) {
    if (!getenv("TINYCLJ_RUNNER_QUIET_PROBE")) {
        return;
    }
    TEST_ASSERT_TRUE_MESSAGE(false, "quiet probe assertion message");
}
