#include "line_editor.h"
#include "memory.h"  // For RELEASE
#include "value.h"  // For make_string, fixnum, CljString
#include "builtins.h"  // For nth2
#include "strings.h"  // For to_cstring and string functions
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(LINE_EDITING_ENABLED) && LINE_EDITING_ENABLED
// Global line editor instance
static LineEditor *global_editor = NULL;

typedef struct {
    CljString *str;          // backing storage (rc-managed)
    uint16_t length;         // current content length (<= capacity)
    uint16_t capacity;       // max content length (excluding null terminator)
} StringBuffer;

#ifndef LINE_EDITOR_BUFFER_INITIAL_CAP
#define LINE_EDITOR_BUFFER_INITIAL_CAP 128
#endif

#ifndef LINE_EDITOR_BUFFER_MAX_CAP
#define LINE_EDITOR_BUFFER_MAX_CAP 4096
#endif

// ANSI escape sequence constants
static const char ESC_RIGHT[] = "\033[C";
static const char ESC_LEFT[] = "\033[D";
static const char ESC_CLEAR[] = "\033[K";
static const char ESC_HOME[] = "\033[1G";
static const char ESC_CLEAR_TO_END[] = "\033[J";
static const char ESC_CURSOR_POS_FMT[] = "\033[%uG";
static const char ESC_CURSOR_UP_FMT[] = "\033[%uA";
static const char ESC_CURSOR_DOWN_FMT[] = "\033[%uB";

#ifndef LINE_EDITOR_ESCAPE_BUFFER_SIZE
#define LINE_EDITOR_ESCAPE_BUFFER_SIZE 8
#endif

struct LineEditor {
    StringBuffer buffer;
    uint16_t cursor_pos;
    uint16_t last_rendered_pos;
    uint16_t last_rendered_row;
    bool line_ready;
    GetCharFunc get_char;
    PutCharFunc put_char;
    PutStringFunc put_string;
    void *ctx;
    
    // ANSI escape sequence buffer
    char escape_buffer[LINE_EDITOR_ESCAPE_BUFFER_SIZE];
    uint8_t escape_pos;
    bool in_escape_sequence;
    bool last_was_cr;
    
    // History support using persistent vector
    CljPersistentVector *history;
    int16_t history_index;     // 0 = temp entry, >0 = older entries, -1 = not browsing
    bool history_has_temp;     // true if a temporary entry is appended at end
    uint16_t rendered_rows;         // max terminal rows kept for the current buffer
    uint16_t rendered_content_rows; // actual rows rendered for the current buffer content

    // Prompt that should be re-drawn when we clear/redraw multi-line history entries.
    char prompt[96];
    uint16_t prompt_len;
};

static bool buffer_init(StringBuffer *buf, uint16_t initial_cap) {
    if (!buf) return false;
    if (initial_cap == 0) initial_cap = 1; // capacity is "max content length"
    if (initial_cap > LINE_EDITOR_BUFFER_MAX_CAP) initial_cap = LINE_EDITOR_BUFFER_MAX_CAP;
    buf->str = make_string_buffer(initial_cap);
    if (!buf->str) return false;
    buf->capacity = initial_cap;
    buf->length = 0;
    buf->str->data[0] = '\0';
    return true;
}

static void buffer_free(StringBuffer *buf) {
    if (!buf) return;
    if (buf->str) {
        RELEASE(buf->str);
    }
    buf->str = NULL;
    buf->length = 0;
    buf->capacity = 0;
}

static void buffer_clear(StringBuffer *buf) {
    if (!buf || !buf->str) return;
    buf->length = 0;
    buf->str->data[0] = '\0';
}

static bool buffer_ensure_capacity(StringBuffer *buf, uint16_t needed_len) {
    if (!buf || !buf->str) return false;
    // needed_len is content length (excluding '\0')
    if (needed_len <= buf->capacity) return true;
    if (needed_len > LINE_EDITOR_BUFFER_MAX_CAP) {
        return false;
    }
    uint16_t new_cap = buf->capacity;
    while (new_cap < needed_len) {
        uint32_t grown = (uint32_t)new_cap * 2u;
        if (grown > LINE_EDITOR_BUFFER_MAX_CAP) {
            new_cap = (uint16_t)LINE_EDITOR_BUFFER_MAX_CAP;
            break;
        }
        new_cap = (uint16_t)grown;
    }
    if (new_cap < needed_len) return false;

    CljString *new_str = make_string_buffer(new_cap);
    if (!new_str) return false;
    if (buf->length > 0) {
        memcpy(new_str->data, buf->str->data, buf->length);
    }
    new_str->data[buf->length] = '\0';

    RELEASE(buf->str);
    buf->str = new_str;
    buf->capacity = new_cap;
    return true;
}

static bool buffer_set_content(StringBuffer *buf, const char *str) {
    if (!buf || !buf->str) return false;
    if (!str) str = "";
    size_t slen = strlen(str);
    if (slen > LINE_EDITOR_BUFFER_MAX_CAP) {
        slen = LINE_EDITOR_BUFFER_MAX_CAP - 1;
    }
    if (!buffer_ensure_capacity(buf, (uint16_t)slen)) return false;
    memcpy(buf->str->data, str, slen);
    buf->str->data[slen] = '\0';
    buf->length = (uint16_t)slen;
    return true;
}

static bool buffer_insert_char(StringBuffer *buf, uint16_t pos, char c) {
    if (!buf || !buf->str) return false;
    if (pos > buf->length) pos = buf->length;
    uint16_t new_len = (uint16_t)(buf->length + 1);
    if (!buffer_ensure_capacity(buf, new_len)) return false;
    memmove(buf->str->data + pos + 1, buf->str->data + pos, (size_t)(buf->length - pos));
    buf->str->data[pos] = c;
    buf->length = new_len;
    buf->str->data[new_len] = '\0';
    return true;
}

static void buffer_delete_char(StringBuffer *buf, uint16_t pos) {
    if (!buf || !buf->str) return;
    if (pos >= buf->length) return;
    memmove(buf->str->data + pos, buf->str->data + pos + 1, (size_t)(buf->length - pos - 1));
    buf->length--;
    buf->str->data[buf->length] = '\0';
}

// Generic cursor movement function
static void move_cursor_horizontal(LineEditor *editor, int steps, const char *escape_seq) {
    for (int i = 0; i < steps; i++) {
        editor->put_string(editor->ctx, escape_seq);
    }
}

// Inline helper functions (better than macros)
static inline bool editor_is_valid(const LineEditor *editor) {
    return editor != NULL;
}

static uint16_t buffer_row_count(const StringBuffer *buf) {
    if (!buf || !buf->str) return 1;
    uint16_t rows = 1;
    for (uint16_t i = 0; i < buf->length; i++) {
        if (buf->str->data[i] == '\n') rows++;
    }
    return rows;
}

static bool buffer_has_newline(const StringBuffer *buf) {
    if (!buf || !buf->str) return false;
    for (uint16_t i = 0; i < buf->length; i++) {
        if (buf->str->data[i] == '\n') return true;
    }
    return false;
}

static void editor_compute_row_col(const LineEditor *editor, uint16_t pos,
                                   uint16_t *row_out, uint16_t *col_out) {
    uint16_t row = 0;
    uint16_t col = (uint16_t)(editor ? editor->prompt_len + 1 : 1);
    if (!editor || !editor->buffer.str) {
        if (row_out) *row_out = 0;
        if (col_out) *col_out = 1;
        return;
    }
    if (pos > editor->buffer.length) pos = editor->buffer.length;
    const char *s = editor->buffer.str->data;
    for (uint16_t i = 0; i < pos; i++) {
        if (s[i] == '\n') {
            row++;
            col = (uint16_t)(editor->prompt_len + 1);
        } else {
            col++;
        }
    }
    if (row_out) *row_out = row;
    if (col_out) *col_out = col;
}

static uint16_t editor_find_pos_for_row_col(const LineEditor *editor, uint16_t target_row, uint16_t desired_col) {
    if (!editor || !editor->buffer.str) return 0;
    const char *s = editor->buffer.str->data;
    uint16_t row = 0;
    uint16_t col = (uint16_t)(editor->prompt_len + 1);
    uint16_t best_pos = 0;

    for (uint16_t pos = 0; pos <= editor->buffer.length; pos++) {
        if (row == target_row) {
            best_pos = pos;
            if (col >= desired_col) return pos;
        }
        if (pos == editor->buffer.length) break;
        if (s[pos] == '\n') {
            row++;
            col = (uint16_t)(editor->prompt_len + 1);
        } else {
            col++;
        }
    }
    return best_pos;
}

static void editor_move_cursor_abs(LineEditor *editor, uint16_t row, uint16_t col, uint16_t current_row) {
    if (!editor) return;
    char seq[16];
    if (current_row > row) {
        snprintf(seq, sizeof(seq), ESC_CURSOR_UP_FMT, (unsigned int)(current_row - row));
        editor->put_string(editor->ctx, seq);
    } else if (row > current_row) {
        snprintf(seq, sizeof(seq), ESC_CURSOR_DOWN_FMT, (unsigned int)(row - current_row));
        editor->put_string(editor->ctx, seq);
    }
    snprintf(seq, sizeof(seq), ESC_CURSOR_POS_FMT, (unsigned int)col);
    editor->put_string(editor->ctx, seq);
}

static void editor_move_cursor_to_pos(LineEditor *editor, uint16_t target_pos) {
    if (!editor) return;
    uint16_t current_row = 0;
    uint16_t current_col = 1;
    uint16_t target_row = 0;
    uint16_t target_col = 1;
    editor_compute_row_col(editor, editor->cursor_pos, &current_row, &current_col);
    editor_compute_row_col(editor, target_pos, &target_row, &target_col);
    if (current_row == target_row && current_col == target_col) {
        editor->cursor_pos = target_pos;
        editor->last_rendered_pos = target_pos;
        return;
    }
    editor_move_cursor_abs(editor, target_row, target_col, current_row);
    editor->cursor_pos = target_pos;
    editor->last_rendered_pos = target_pos;
    editor_compute_row_col(editor, target_pos, &editor->last_rendered_row, NULL);
}

static void editor_redraw_with_cursor(LineEditor *editor, uint16_t target_pos);

static void editor_move_cursor_or_redraw(LineEditor *editor,
                                         uint16_t new_pos,
                                         const char *escape_seq,
                                         uint16_t extra_right) {
    if (!editor) return;
    editor->cursor_pos = new_pos;
    if (buffer_has_newline(&editor->buffer)) {
        editor_redraw_with_cursor(editor, editor->cursor_pos);
        return;
    }
    if (escape_seq) {
        editor->put_string(editor->ctx, escape_seq);
    }
    if (extra_right > 0) {
        move_cursor_horizontal(editor, extra_right, ESC_RIGHT);
    }
    editor->last_rendered_pos = editor->cursor_pos;
    editor->last_rendered_row = 0;
}

static void editor_redraw_with_cursor(LineEditor *editor, uint16_t target_pos) {
    if (!editor) return;
    uint16_t prev_rows = editor->rendered_rows;
    uint16_t prev_content_rows = editor->rendered_content_rows;
    uint16_t current_row = editor->last_rendered_row;
    uint16_t new_rows = buffer_row_count(&editor->buffer);
    if (prev_content_rows != new_rows && prev_content_rows > 0) {
        // Buffer row count changed: keep the last known cursor row on screen.
        // Only clamp if it somehow exceeds the previous content height.
        if (current_row >= prev_content_rows) {
            current_row = (uint16_t)(prev_content_rows - 1);
        }
    }
    if (prev_rows > 0 && current_row >= prev_rows) {
        current_row = (uint16_t)(prev_rows - 1);
    }
    // Clear previously rendered rows (best-effort, assumes no scrollback).
    if (current_row > 0) {
        char seq[16];
        snprintf(seq, sizeof(seq), ESC_CURSOR_UP_FMT, (unsigned int)current_row);
        editor->put_string(editor->ctx, seq);
    }
    editor->put_string(editor->ctx, "\r");
    editor->put_string(editor->ctx, ESC_CLEAR_TO_END);

    // Reprint prompt + buffer (buffer may include newlines).
    if (editor->prompt[0] != '\0') {
        editor->put_string(editor->ctx, editor->prompt);
    }
    if (editor->buffer.str) {
        const char *s = editor->buffer.str->data;
        for (uint16_t i = 0; i < editor->buffer.length; i++) {
            char c = s[i];
            editor->put_char(editor->ctx, c);
            if (c == '\n' && editor->prompt_len > 0) {
                // Indent continuation lines under the prompt.
                for (uint16_t k = 0; k < editor->prompt_len; k++) {
                    editor->put_char(editor->ctx, ' ');
                }
            }
        }
    }
    editor->put_string(editor->ctx, ESC_CLEAR);
    editor->rendered_rows = (prev_rows > new_rows) ? prev_rows : new_rows;
    editor->rendered_content_rows = new_rows;

    uint16_t end_row = 0;
    uint16_t end_col = 1;
    uint16_t target_row = 0;
    uint16_t target_col = 1;
    editor_compute_row_col(editor, editor->buffer.length, &end_row, &end_col);
    editor_compute_row_col(editor, target_pos, &target_row, &target_col);
    editor_move_cursor_abs(editor, target_row, target_col, end_row);
    editor->cursor_pos = target_pos;
    editor->last_rendered_pos = target_pos;
    editor->last_rendered_row = target_row;
}

static void editor_ring_bell(LineEditor *editor) {
    if (!editor) return;
    editor->put_char(editor->ctx, '\a');
}

static void history_exit(LineEditor *editor) {
    if (!editor) return;
    if (editor->history_has_temp) {
        vector_pop_inplace(&editor->history); // releases last element
        editor->history_has_temp = false;
    }
    editor->history_index = -1;
}

static void history_ensure_temp(LineEditor *editor) {
    if (!editor || editor->history_has_temp) return;
    // Append current buffer as a temporary entry (editable during history navigation).
    CljString *tmp = make_string((editor->buffer.str) ? editor->buffer.str->data : "");
    if (tmp) {
        vector_conj_inplace(&editor->history, (ID)tmp);
        RELEASE(tmp);
        editor->history_has_temp = true;
        editor->history_index = 0;
    }
}

static int history_size(LineEditor *editor) {
    return editor ? vector_count(editor->history) : 0;
}

static int history_current_vector_index(LineEditor *editor) {
    if (!editor || !editor->history_has_temp) return -1;
    int n = history_size(editor);
    if (n <= 0) return -1;
    int idx = (n - 1) - (int)editor->history_index;
    if (idx < 0 || idx >= n) return -1;
    return idx;
}

static void history_save_current_entry(LineEditor *editor) {
    if (!editor || !editor->history_has_temp) return;
    int idx = history_current_vector_index(editor);
    if (idx < 0) return;
    CljString *s = make_string((editor->buffer.str) ? editor->buffer.str->data : "");
    if (!s) return;
    vector_assoc_inplace(&editor->history, (unsigned int)idx, (ID)s);
    RELEASE(s);
}

static void history_load_vector_index(LineEditor *editor, int idx) {
    if (!editor) return;
    CljString *line = line_editor_get_history_line(editor, idx);
    if (!line) return;
    buffer_set_content(&editor->buffer, clj_string_data(line));
    RELEASE(line);
    editor_redraw_with_cursor(editor, editor->buffer.length);
}

// NOTE: History entries may include newlines (multi-form REPL input).
// The editor redraw logic must handle multi-line clearing when recalling history.

static void history_begin_from_current(LineEditor *editor) {
    if (!editor) return;
    int n = history_size(editor);
    if (n == 0) {
        editor_ring_bell(editor);
        return;
    }
    history_ensure_temp(editor);
    n = history_size(editor);
    if (n <= 1) {
        // Only temp exists.
        editor_ring_bell(editor);
        return;
    }
    history_save_current_entry(editor);
    editor->history_index = 1; // newest real entry
    int idx = history_current_vector_index(editor);
    if (idx >= 0) {
        history_load_vector_index(editor, idx);
    }
}

// ANSI escape sequence handling
static bool is_ansi_escape_sequence(const char *input, int len) {
    return len >= 3 && input[0] == '\033' && input[1] == '[';
}

static bool is_csi_final_char(char c) {
    return (c >= '@' && c <= '~');
}

static void parse_csi_params(const char *input, int len, int *param1, int *param2) {
    int p1 = 0;
    int p2 = 0;
    int count = 0;
    bool in_number = false;
    int current = 0;

    if (len < 3) {
        if (param1) *param1 = 0;
        if (param2) *param2 = 0;
        return;
    }

    for (int i = 2; i < len - 1; i++) {
        char c = input[i];
        if (c >= '0' && c <= '9') {
            current = current * 10 + (c - '0');
            in_number = true;
            continue;
        }
        if (c == ';') {
            if (in_number) {
                if (count == 0) p1 = current;
                if (count == 1) p2 = current;
                count++;
                current = 0;
                in_number = false;
            }
            continue;
        }
        break;
    }

    if (in_number) {
        if (count == 0) p1 = current;
        if (count == 1) p2 = current;
        count++;
    }

    if (param1) *param1 = p1;
    if (param2) *param2 = p2;
}

static void insert_newline(LineEditor *editor) {
    if (!editor) return;
    if (!buffer_insert_char(&editor->buffer, editor->cursor_pos, '\n')) {
        editor_ring_bell(editor);
        return;
    }
    editor->cursor_pos++;
    editor_redraw_with_cursor(editor, editor->cursor_pos);
}

static int handle_ansi_escape_sequence(LineEditor *editor, const char *input, int len) {
    if (!is_ansi_escape_sequence(input, len)) {
        return 0;
    }

    char command = input[len - 1];
    int p1 = 0;
    int p2 = 0;
    parse_csi_params(input, len, &p1, &p2);

    switch (command) {
        case 'A': // Up arrow - navigate history backwards
            {
                if (buffer_has_newline(&editor->buffer)) {
                    uint16_t current_row = 0;
                    uint16_t current_col = 1;
                    editor_compute_row_col(editor, editor->cursor_pos, &current_row, &current_col);
                    if (current_row > 0) {
                        uint16_t target_pos = editor_find_pos_for_row_col(editor, (uint16_t)(current_row - 1), current_col);
                        editor_move_cursor_to_pos(editor, target_pos);
                        return 3;
                    }
                }
                int n = history_size(editor);
                if (n == 0) {
                    editor_ring_bell(editor);
                    return 3;
                }

                if (!editor->history_has_temp) {
                    history_begin_from_current(editor);
                    return 3;
                }

                history_save_current_entry(editor);
                if ((int)editor->history_index + 1 >= n) {
                    editor_ring_bell(editor);
                    return 3;
                }
                editor->history_index++;

                int idx = history_current_vector_index(editor);
                if (idx >= 0) {
                    history_load_vector_index(editor, idx);
                }
                return 3;
            }
        case 'B': // Down arrow - navigate history forwards
            if (buffer_has_newline(&editor->buffer)) {
                uint16_t current_row = 0;
                uint16_t current_col = 1;
                uint16_t end_row = 0;
                editor_compute_row_col(editor, editor->cursor_pos, &current_row, &current_col);
                editor_compute_row_col(editor, editor->buffer.length, &end_row, NULL);
                if (editor->buffer.length > 0 && editor->buffer.str &&
                    editor->buffer.str->data[editor->buffer.length - 1] == '\n' &&
                    end_row > 0) {
                    end_row--;
                }
                if (current_row < end_row) {
                    uint16_t target_pos = editor_find_pos_for_row_col(editor, (uint16_t)(current_row + 1), current_col);
                    editor_move_cursor_to_pos(editor, target_pos);
                    return 3;
                }
            }
            if (!editor->history_has_temp) {
                editor_ring_bell(editor);
                return 3;
            }

            history_save_current_entry(editor);
            if (editor->history_index == 0) {
                editor_ring_bell(editor);
                return 3;
            }
            editor->history_index--;
            {
                int idx = history_current_vector_index(editor);
                if (idx >= 0) {
                    history_load_vector_index(editor, idx);
                }
            }
            return 3;
        case 'C': // Right arrow
            if (editor->cursor_pos < editor->buffer.length) {
                editor_move_cursor_or_redraw(editor,
                                             (uint16_t)(editor->cursor_pos + 1),
                                             ESC_RIGHT,
                                             0);
            }
            return 3;
        case 'D': // Left arrow
            if (editor->cursor_pos > 0) {
                editor_move_cursor_or_redraw(editor,
                                             (uint16_t)(editor->cursor_pos - 1),
                                             ESC_LEFT,
                                             0);
            }
            return 3;
        case 'H': // Home
            editor_move_cursor_or_redraw(editor, 0, ESC_HOME, 0);
            return 3;
        case 'F': // End
            editor_move_cursor_or_redraw(editor,
                                         editor->buffer.length,
                                         ESC_HOME,
                                         editor->buffer.length);
            return 3;
        case 'K': // Clear line from cursor
            editor->put_string(editor->ctx, ESC_CLEAR);
            return 3;
        case '~': // Function keys like delete
            if (p1 == 3) {
                if (editor->cursor_pos < editor->buffer.length) {
                    if (editor->history_has_temp && editor->history_index > 0) {
                        history_exit(editor);
                    }
                    buffer_delete_char(&editor->buffer, editor->cursor_pos);
                    if (buffer_has_newline(&editor->buffer)) {
                        editor_redraw_with_cursor(editor, editor->cursor_pos);
                    } else {
                        // Redraw from cursor position
                        editor->put_string(editor->ctx, ESC_CLEAR);
                        for (uint16_t i = editor->cursor_pos; i < editor->buffer.length; i++) {
                            editor->put_char(editor->ctx, editor->buffer.str->data[i]);
                        }
                        // Move cursor back to position
                        move_cursor_horizontal(editor, editor->buffer.length - editor->cursor_pos, ESC_LEFT);
                        editor->last_rendered_pos = editor->cursor_pos;
                    }
                }
                return len;
            }
            return len;
        case 'u': // CSI u keyboard protocol (e.g. Shift/Ctrl-Enter)
            if (p1 == 13 && (p2 == 2 || p2 == 5)) {
                insert_newline(editor);
                return len;
            }
            if (p1 == 10 && p2 == 5) {
                insert_newline(editor);
                return len;
            }
            return len;
    }
    
    return len;
}

// Line editing operations
static void insert_character(LineEditor *editor, char c) {
    if (!buffer_insert_char(&editor->buffer, editor->cursor_pos, c)) {
        editor_ring_bell(editor);
        return;
    }
    editor->cursor_pos++;

    if (buffer_has_newline(&editor->buffer)) {
        editor_redraw_with_cursor(editor, editor->cursor_pos);
        return;
    }

    // Display the character
    editor->put_char(editor->ctx, c);

    // Redraw remaining characters
    for (uint16_t i = editor->cursor_pos; i < editor->buffer.length; i++) {
        editor->put_char(editor->ctx, editor->buffer.str->data[i]);
    }

    // Move cursor back to correct position
    move_cursor_horizontal(editor, editor->buffer.length - editor->cursor_pos, ESC_LEFT);
    editor->last_rendered_pos = editor->cursor_pos;
}

static void backspace_character(LineEditor *editor) {
    if (editor->cursor_pos > 0) {
        // Move cursor left
        editor->cursor_pos--;
        if (buffer_has_newline(&editor->buffer)) {
            buffer_delete_char(&editor->buffer, editor->cursor_pos);
            editor_redraw_with_cursor(editor, editor->cursor_pos);
            return;
        }

        editor->put_string(editor->ctx, ESC_LEFT);

        // Delete character
        buffer_delete_char(&editor->buffer, editor->cursor_pos);

        // Clear from cursor to end of line
        editor->put_string(editor->ctx, ESC_CLEAR);

        // Redraw remaining characters
        for (uint16_t i = editor->cursor_pos; i < editor->buffer.length; i++) {
            editor->put_char(editor->ctx, editor->buffer.str->data[i]);
        }

        // Move cursor back to position
        move_cursor_horizontal(editor, editor->buffer.length - editor->cursor_pos, ESC_LEFT);
        editor->last_rendered_pos = editor->cursor_pos;
    }
}

LineEditor* line_editor_new(GetCharFunc get_char, PutCharFunc put_char, PutStringFunc put_string, void *ctx) {
    LineEditor *editor = malloc(sizeof(LineEditor));
    if (!editor) return NULL;
    
    memset(editor, 0, sizeof(LineEditor));
    if (!buffer_init(&editor->buffer, LINE_EDITOR_BUFFER_INITIAL_CAP)) {
        free(editor);
        return NULL;
    }
    editor->get_char = get_char;
    editor->put_char = put_char;
    editor->put_string = put_string;
    editor->ctx = ctx;
    
    // Initialize history support with persistent vector (updated via ASSIGN)
    editor->history = make_vector(50, false);
    editor->history_index = -1;  // Not browsing
    editor->history_has_temp = false;
    editor->rendered_rows = 1;
    editor->rendered_content_rows = 1;
    editor->last_rendered_row = 0;
    editor->prompt[0] = '\0';
    editor->prompt_len = 0;
    
    return editor;
}

void line_editor_free(LineEditor *editor) {
    if (editor) {
        history_exit(editor);
        buffer_free(&editor->buffer);
        // Release history vector (automatically frees all contained strings)
        RELEASE(editor->history);
        free(editor);
    }
}

int line_editor_process_input(LineEditor *editor) {
    if (!editor) return LINE_EDITOR_ERROR;
    
    // Try to read a complete escape sequence if we're in one
    if (editor->in_escape_sequence) {
        // Read remaining characters of escape sequence
        while (editor->escape_pos < sizeof(editor->escape_buffer) - 1) {
            int c = editor->get_char(editor->ctx);
            if (c == LINE_EDITOR_GETCHAR_NO_INPUT) {
                // No more bytes available yet; keep escape state and try again later.
                return LINE_EDITOR_SUCCESS;
            }
            if (c == -1) {
                // EOF during escape sequence, reset
                editor->in_escape_sequence = false;
                editor->escape_pos = 0;
                return LINE_EDITOR_EOF;
            }
            if (c == 4) {
                editor->in_escape_sequence = false;
                editor->escape_pos = 0;
                return LINE_EDITOR_EOF;
            }
            
            editor->escape_buffer[editor->escape_pos++] = (char)c;
            editor->escape_buffer[editor->escape_pos] = '\0';

            if (editor->escape_pos >= 2 && editor->escape_buffer[1] != '[') {
                editor->in_escape_sequence = false;
                editor->escape_pos = 0;
                return LINE_EDITOR_SUCCESS;
            }

            if (editor->escape_pos >= 3 &&
                editor->escape_buffer[1] == '[' &&
                is_csi_final_char(editor->escape_buffer[editor->escape_pos - 1])) {
                editor->in_escape_sequence = false;
                handle_ansi_escape_sequence(editor, editor->escape_buffer, editor->escape_pos);
                editor->escape_pos = 0;
                return LINE_EDITOR_SUCCESS;
            }
        }
        
        // Escape sequence too long, reset
        editor->in_escape_sequence = false;
        editor->escape_pos = 0;
        return LINE_EDITOR_SUCCESS;
    }
    
    // Read first character
    int c = editor->get_char(editor->ctx);
    if (c == -1) {
        return LINE_EDITOR_EOF;
    }
    if (c == -2) {
        // No input available (non-blocking mode)
        return LINE_EDITOR_SUCCESS; // Return success but no input processed
    }
    
    // Handle ANSI escape sequences
    if (c == '\033') {
        editor->in_escape_sequence = true;
        editor->escape_pos = 0;
        editor->escape_buffer[editor->escape_pos++] = c;
        editor->escape_buffer[editor->escape_pos] = '\0';
        // Continue processing to get the rest of the escape sequence
        return line_editor_process_input(editor);
    }
    
    // Handle Ctrl-D (EOF)
    if (c == 4) {
        editor->last_was_cr = false;
        return LINE_EDITOR_EOF;
    }
    
    // Handle carriage return (submit line)
    if (c == '\r') {
        editor->last_was_cr = true;
        if (editor->buffer.length > 0) {
            if (buffer_has_newline(&editor->buffer) && editor->cursor_pos != editor->buffer.length) {
                editor_move_cursor_to_pos(editor, editor->buffer.length);
            }
            // Ensure temp history entry does not leak into persisted history.
            history_exit(editor);
            editor->line_ready = true;
            editor->put_char(editor->ctx, '\n');
            return LINE_EDITOR_LINE_READY;
        }
        // Empty line: emit a blank line and ask the REPL to show a new prompt.
        editor->line_ready = true;
        editor->put_char(editor->ctx, '\n');
        return LINE_EDITOR_LINE_READY;
    }

    // Handle line feed (Ctrl+J / raw LF). Treat as insert-newline unless it follows CR.
    if (c == '\n') {
        if (editor->last_was_cr) {
            editor->last_was_cr = false;
            return LINE_EDITOR_SUCCESS;
        }
        editor->last_was_cr = false;
        insert_newline(editor);
        return LINE_EDITOR_SUCCESS;
    }
    
    editor->last_was_cr = false;

    // Handle backspace/delete
    if (c == '\b' || c == 127) {
        backspace_character(editor);
        return LINE_EDITOR_SUCCESS;
    }
    
    // Handle printable characters
    if (c >= 32 && c <= 126) {
        insert_character(editor, c);
        return LINE_EDITOR_SUCCESS;
    }
    
    // Handle other control characters (ignore)
    return LINE_EDITOR_SUCCESS;
}

int line_editor_get_state(const LineEditor *editor, LineEditorState *state) {
    if (!editor_is_valid(editor) || !state) return LINE_EDITOR_ERROR;
    
    const char *src = (editor->buffer.str) ? editor->buffer.str->data : "";
    strncpy(state->buffer, src, sizeof(state->buffer) - 1);
    state->buffer[sizeof(state->buffer) - 1] = '\0';
    state->cursor_pos = editor->cursor_pos;
    state->length = (int)editor->buffer.length;
    state->line_ready = editor->line_ready;
    
    return LINE_EDITOR_SUCCESS;
}

const char* line_editor_get_buffer_cstr(const LineEditor *editor, size_t *len) {
    if (!editor) return NULL;
    if (len) *len = (size_t)editor->buffer.length;
    return (editor->buffer.str) ? editor->buffer.str->data : "";
}

void line_editor_set_prompt(LineEditor *editor, const char *prompt) {
    if (!editor) return;
    if (!prompt) prompt = "";
    // Copy into fixed storage (prompt is short: "ns=> " or "ns... ").
    strncpy(editor->prompt, prompt, sizeof(editor->prompt) - 1);
    editor->prompt[sizeof(editor->prompt) - 1] = '\0';
    editor->prompt_len = (uint16_t)strlen(editor->prompt);
}

void line_editor_clear(LineEditor *editor) {
    if (!editor_is_valid(editor)) return;
    history_exit(editor);
    buffer_clear(&editor->buffer);
    editor->cursor_pos = 0;
    editor->line_ready = false;
    editor->rendered_rows = 1;
    editor->rendered_content_rows = 1;
    editor->last_was_cr = false;
    editor->last_rendered_pos = 0;
    editor->last_rendered_row = 0;
}

void line_editor_add_to_history(LineEditor *editor, const char *line) {
    if (!editor || !line) return;
    
    // Check if this line is identical to the last history entry
    CljPersistentVector *history_vec = editor->history;
    unsigned int count = history_vec ? vector_count(history_vec) : 0;
    if (count > 0) {
        CljString *last_line = line_editor_get_history_line(editor, (int)count - 1);
        if (last_line) {
            const char *str_data = clj_string_data(last_line);
            if (strcmp(line, str_data) == 0) {
                // Duplicate of last entry - skip adding
                RELEASE(last_line);
                return;
            }
            RELEASE(last_line);
        }
    }
    
    // Create string object and add to history vector (persistent, via in-place helper)
    CljObject *line_obj = (CljObject*)make_string(line);
    if (line_obj) {
        vector_conj_inplace(&editor->history, line_obj);
        // line_obj is now retained by the vector, we can release our reference
        RELEASE(line_obj);
    }
}

CljString* line_editor_get_history_line(LineEditor *editor, int index) {
    if (!editor) return NULL;

    CljPersistentVector *history_vec = editor->history;
    ID elem = vector_nth(history_vec, (unsigned int)index);
    // Retain element for return value (caller will release it)
    return (CljString*)RETAIN(elem);
}

int line_editor_get_history_size(const LineEditor *editor) {
    return editor ? (int)vector_count(editor->history) : 0;
}

CljPersistentVector* line_editor_get_history_vector(LineEditor *editor) {
    if (!editor) return empty_vector();

    CljPersistentVector *history_vec = editor->history;
    if (!history_vec) return empty_vector();

    unsigned int n = vector_count(history_vec);
    if (!n) return empty_vector();

    // Return retained persistent vector; caller must RELEASE/AUTORELEASE if needed.
    return (CljPersistentVector*)RETAIN(history_vec);
}

void line_editor_clear_history(LineEditor *editor) {
    if (!editor) return;
    history_exit(editor);
    ASSIGN(editor->history, make_vector(50, false));
    editor->history_index = -1;
    editor->history_has_temp = false;
}

void line_editor_set_history_from_vector(LineEditor *editor, CljPersistentVector *vec) {
    if (!editor || !vec) return;
    line_editor_clear_history(editor);
    int count = vector_count(vec);
    ID nth_args[2];
    nth_args[0] = vec;
    for (int i = 0; i < count; i++) {
        nth_args[1] = fixnum(i);
        ID elem = nth2(nth_args, 2);
        if (elem && TAG(elem) == CLJ_STRING) {
            // elem is already a CljString - use string_data directly (no to_string needed)
            // elem lifetime is tied to vector - no release needed
            CljString *str = (CljString*)elem;
            line_editor_add_to_history(editor, string_data(str));
        }
    }
}

// Global line editor management functions
void set_line_editor(LineEditor *editor) {
    global_editor = editor;
}

LineEditor* get_line_editor(void) {
    return global_editor;
}

void cleanup_line_editor(void) {
    if (global_editor) {
        line_editor_free(global_editor);
        global_editor = NULL;
    }
}

// Reset history index to new line mode (used after exceptions)
void line_editor_reset_history_index(LineEditor *editor) {
    if (editor) {
        history_exit(editor);
    }
}

// Optional: Default persistence path (~/.tiny-clj/history.edn)
static void build_default_history_path(char *out, size_t out_sz) {
    const char *home = getenv("HOME") ? getenv("HOME") : ".";
    snprintf(out, out_sz, "%s/.tiny-clj", home);
    // Ensure directory exists
    mkdir(out, 0700);
    snprintf(out, out_sz, "%s/.tiny-clj/history.edn", home);
}

// External persistence functions (defined in repl.c)
extern CljObject* history_trim_last_n(CljObject *vec, int limit);
extern bool history_save_to_file(CljPersistentVector *vec, const char *path);
extern CljObject* history_load_from_file(const char *path);

// Load history from default path; returns persistent Vector<String>
CljObject* line_editor_history_load_default(void) {
    char path[512];
    build_default_history_path(path, sizeof(path));
    return history_load_from_file(path);
}

// Save history to default path; accepts Vector<String> (persistent/transient ok)
bool line_editor_history_save_default(CljObject *vec) {
    char path[512];
    build_default_history_path(path, sizeof(path));
    return history_save_to_file(as_persistent_vector((ID)vec), path);
}

#else
// Stub implementations when line editing is disabled
LineEditor* line_editor_new(GetCharFunc get_char, PutCharFunc put_char, PutStringFunc put_string, void *ctx) {
    (void)get_char; (void)put_char; (void)put_string; (void)ctx;
    return NULL;
}

void line_editor_free(LineEditor *editor) {
    (void)editor;
}

int line_editor_process_input(LineEditor *editor) {
    (void)editor;
    return LINE_EDITOR_ERROR;
}

int line_editor_get_state(const LineEditor *editor, LineEditorState *state) {
    (void)editor; (void)state;
    return LINE_EDITOR_ERROR;
}

const char* line_editor_get_buffer_cstr(const LineEditor *editor, size_t *len) {
    (void)editor;
    if (len) *len = 0;
    return "";
}

void line_editor_clear(LineEditor *editor) {
    (void)editor;
}

void line_editor_add_to_history(LineEditor *editor, const char *line) {
    (void)editor; (void)line;
}

CljString* line_editor_get_history_line(LineEditor *editor, int index) {
    (void)editor; (void)index;
    return NULL;
}

int line_editor_get_history_size(const LineEditor *editor) {
    (void)editor;
    return 0;
}

CljPersistentVector* line_editor_get_history_vector(LineEditor *editor) {
    (void)editor;
    return empty_vector();
}

void line_editor_set_history_from_vector(LineEditor *editor, CljPersistentVector *vec) {
    (void)editor; (void)vec;
}

void line_editor_clear_history(LineEditor *editor) {
    (void)editor;
}

// Global line editor management functions (stub implementations)
void set_line_editor(LineEditor *editor) {
    (void)editor;
}

LineEditor* get_line_editor(void) {
    return NULL;
}

void cleanup_line_editor(void) {
    // Nothing to do when line editing is disabled
}

void line_editor_reset_history_index(LineEditor *editor) {
    (void)editor;
    // Nothing to do when line editing is disabled
}

#endif // LINE_EDITING_ENABLED
