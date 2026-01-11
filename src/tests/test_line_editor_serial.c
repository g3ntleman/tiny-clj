/*
 * Tests for LineEditor running over a byte stream (UART-style).
 *
 * Goal: Ensure line editing + in-memory history works without relying on a TTY.
 */

#include "tests_common.h"
#include "../line_editor.h"

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

static ID run_editor_until_ready(LineEditor *ed, int max_steps) {
    for (int i = 0; i < max_steps; i++) {
        int r = line_editor_process_input(ed);
        if (r == LINE_EDITOR_LINE_READY) return fixnum(1);
        if (r == LINE_EDITOR_EOF || r == LINE_EDITOR_ERROR) return NULL;
    }
    return NULL;
}

TEST(test_line_editor_serial_basic_line) {
    const unsigned char input[] = { 'h','i','\n' };
    FakeStream s = { .in = input, .in_len = (int)sizeof(input), .in_pos = 0, .out_len = 0 };

    LineEditor *ed = line_editor_new(fake_get_char, fake_put_char, fake_put_string, &s);
    TEST_ASSERT_NOT_NULL(ed);

    TEST_ASSERT_NOT_NULL(run_editor_until_ready(ed, 32));

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

    TEST_ASSERT_NOT_NULL(run_editor_until_ready(ed, 64));

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

    TEST_ASSERT_NOT_NULL(run_editor_until_ready(ed, 64));

    LineEditorState st;
    TEST_ASSERT_EQUAL_INT(0, line_editor_get_state(ed, &st));
    TEST_ASSERT_TRUE(st.line_ready);
    TEST_ASSERT_EQUAL_STRING("prev", st.buffer);

    line_editor_free(ed);
}

