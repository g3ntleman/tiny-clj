/*
 * Tests for LineEditor running over a byte stream (UART-style).
 *
 * Goal: Ensure line editing + in-memory history works without relying on a TTY.
 */

#include "tests_common.h"
#include "../line_editor.h"

// Anchor a symbol from tests_common.h so include-cleaner sees it as used.
static void *g_tests_common_anchor __attribute__((unused)) = (void*)&g_test_eval_state;

typedef struct {
    const unsigned char *in;
    int in_len;
    int in_pos;
    char out[2048];
    int out_len;
} FakeStream;

static int fake_get_char(void *ctx) {
    FakeStream *s = (FakeStream*)ctx;
    if (!s) return LINE_EDITOR_EOF;
    if (s->in_pos >= s->in_len) return LINE_EDITOR_GETCHAR_NO_INPUT;
    return (int)s->in[s->in_pos++];
}

static void fake_put_char(void *ctx, char c) {
    FakeStream *s = (FakeStream*)ctx;
    if (!s) return;
    if (s->out_len + 1 >= (int)sizeof(s->out)) return;
    s->out[s->out_len++] = c;
    s->out[s->out_len] = '\0';
}

static void fake_put_string(void *ctx, const char *str) {
    FakeStream *s = (FakeStream*)ctx;
    if (!s || !str) return;
    while (*str) {
        fake_put_char(ctx, *str++);
    }
}

static bool run_editor_until_ready(LineEditor *ed, int max_steps) {
    for (int i = 0; i < max_steps; i++) {
        int r = line_editor_process_input(ed);
        if (r == LINE_EDITOR_LINE_READY) return true;
        if (r == LINE_EDITOR_EOF || r == LINE_EDITOR_ERROR) return false;
    }
    return false;
}

TEST(test_line_editor_serial_basic_line) {
    const unsigned char input[] = { 'h','i','\n' };
    FakeStream s = { .in = input, .in_len = (int)sizeof(input), .in_pos = 0, .out_len = 0 };

    LineEditor *ed = line_editor_new(fake_get_char, fake_put_char, fake_put_string, &s);
    TEST_ASSERT_NOT_NULL(ed);

    TEST_ASSERT_TRUE(run_editor_until_ready(ed, 32));

    LineEditorState st;
    TEST_ASSERT_EQUAL_INT(0, line_editor_get_state(ed, &st));
    TEST_ASSERT_TRUE(st.line_ready);
    TEST_ASSERT_EQUAL_INT(2, st.length);
    TEST_ASSERT_EQUAL_STRING("hi", st.buffer);

    line_editor_free(ed);
}

TEST(test_line_editor_serial_backspace) {
    // Type: a b <backspace> c \n  => "ac"
    const unsigned char input[] = { 'a','b', 0x7f, 'c','\n' };
    FakeStream s = { .in = input, .in_len = (int)sizeof(input), .in_pos = 0, .out_len = 0 };

    LineEditor *ed = line_editor_new(fake_get_char, fake_put_char, fake_put_string, &s);
    TEST_ASSERT_NOT_NULL(ed);

    TEST_ASSERT_TRUE(run_editor_until_ready(ed, 64));

    LineEditorState st;
    TEST_ASSERT_EQUAL_INT(0, line_editor_get_state(ed, &st));
    TEST_ASSERT_TRUE(st.line_ready);
    TEST_ASSERT_EQUAL_STRING("ac", st.buffer);

    line_editor_free(ed);
}

TEST(test_line_editor_serial_history_up_arrow) {
    // Add history entry, then press Up Arrow (ESC [ A) and Enter.
    const unsigned char input[] = { 0x1b, '[', 'A', '\n' };
    FakeStream s = { .in = input, .in_len = (int)sizeof(input), .in_pos = 0, .out_len = 0 };

    LineEditor *ed = line_editor_new(fake_get_char, fake_put_char, fake_put_string, &s);
    TEST_ASSERT_NOT_NULL(ed);

    line_editor_add_to_history(ed, "prev");

    TEST_ASSERT_TRUE(run_editor_until_ready(ed, 64));

    LineEditorState st;
    TEST_ASSERT_EQUAL_INT(0, line_editor_get_state(ed, &st));
    TEST_ASSERT_TRUE(st.line_ready);
    TEST_ASSERT_EQUAL_STRING("prev", st.buffer);

    line_editor_free(ed);
}

TEST(test_line_editor_serial_large_paste_uses_dynamic_growth) {
    // Paste a line longer than LineEditorState.buffer[512] and ensure the full
    // buffer is still available via line_editor_get_buffer_cstr().
    const int n = 600;
    unsigned char *input = (unsigned char*)malloc((size_t)n + 1);
    TEST_ASSERT_NOT_NULL(input);
    for (int i = 0; i < n; i++) input[i] = 'x';
    input[n] = '\n';

    FakeStream s = { .in = input, .in_len = n + 1, .in_pos = 0, .out_len = 0 };
    LineEditor *ed = line_editor_new(fake_get_char, fake_put_char, fake_put_string, &s);
    TEST_ASSERT_NOT_NULL(ed);

    TEST_ASSERT_TRUE(run_editor_until_ready(ed, n + 64));

    size_t len = 0;
    const char *buf = line_editor_get_buffer_cstr(ed, &len);
    TEST_ASSERT_EQUAL_UINT((unsigned int)n, (unsigned int)len);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('x', buf[n - 1]);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[n]);

    free(input);
    line_editor_free(ed);
}

TEST(test_line_editor_serial_history_edit_then_down_then_up_preserves_edit) {
    // History navigation should preserve edits while browsing, similar to linenoise.
    // Steps:
    // - history: ["one", "two"]
    // - Up -> "two"
    // - type '!' -> "two!"
    // - Down -> temp entry
    // - Up -> should return "two!" (edited)
    // - Enter -> line_ready with "two!"
    const unsigned char input[] = {
        0x1b, '[', 'A',  // Up
        '!',
        0x1b, '[', 'B',  // Down
        0x1b, '[', 'A',  // Up
        '\n'
    };
    FakeStream s = { .in = input, .in_len = (int)sizeof(input), .in_pos = 0, .out_len = 0 };

    LineEditor *ed = line_editor_new(fake_get_char, fake_put_char, fake_put_string, &s);
    TEST_ASSERT_NOT_NULL(ed);
    line_editor_add_to_history(ed, "one");
    line_editor_add_to_history(ed, "two");

    TEST_ASSERT_TRUE(run_editor_until_ready(ed, 256));

    size_t len = 0;
    const char *buf = line_editor_get_buffer_cstr(ed, &len);
    TEST_ASSERT_EQUAL_UINT(4u, (unsigned int)len);
    TEST_ASSERT_EQUAL_STRING("two!", buf);

    line_editor_free(ed);
}

TEST(test_line_editor_serial_history_multiline_preserved) {
    // History entries may contain newlines (multi-form REPL input).
    // We preserve formatting; display correctness is handled by multi-line clear/redraw.
    const unsigned char input[] = { 0x1b, '[', 'A', '\n' }; // Up + Enter
    FakeStream s = { .in = input, .in_len = (int)sizeof(input), .in_pos = 0, .out_len = 0 };

    LineEditor *ed = line_editor_new(fake_get_char, fake_put_char, fake_put_string, &s);
    TEST_ASSERT_NOT_NULL(ed);

    // Store a multi-line entry; it should be preserved.
    line_editor_add_to_history(ed, "(+ 1 2)\n(+ 3 4)");

    TEST_ASSERT_TRUE(run_editor_until_ready(ed, 128));

    size_t len = 0;
    const char *buf = line_editor_get_buffer_cstr(ed, &len);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_TRUE_MESSAGE(strchr(buf, '\n') != NULL, "Expected history recall buffer to preserve newlines");
    TEST_ASSERT_TRUE_MESSAGE(strstr(buf, "(+ 1 2)") != NULL, "Expected first form to be present");
    TEST_ASSERT_TRUE_MESSAGE(strstr(buf, "(+ 3 4)") != NULL, "Expected second form to be present");

    line_editor_free(ed);
}
