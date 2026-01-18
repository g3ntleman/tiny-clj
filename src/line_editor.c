#include "line_editor.h"
#include "memory.h"  // For RELEASE
#include "value.h"  // For make_string, fixnum, CljString
#include "builtins.h"  // For nth2
#include "strings.h"  // For to_cstring and string functions
#include <sys/types.h>
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

struct LineEditor {
    StringBuffer buffer;
    uint16_t cursor_pos;
    bool line_ready;
    GetCharFunc get_char;
    PutCharFunc put_char;
    PutStringFunc put_string;
    void *ctx;
    
    // ANSI escape sequence buffer
    char escape_buffer[4];
    uint8_t escape_pos;
    bool in_escape_sequence;
    
    // History support using CljVector
    CljVector *history;        // CljVector for history
    int16_t history_index;     // 0 = temp entry, >0 = older entries, -1 = not browsing
    bool history_has_temp;     // true if a temporary entry is appended at end
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

// Generic cursor movement functions
static void move_cursor_right(LineEditor *editor, int steps) {
    for (int i = 0; i < steps; i++) {
        editor->put_string(editor->ctx, ESC_RIGHT);
    }
}

static void move_cursor_left(LineEditor *editor, int steps) {
    for (int i = 0; i < steps; i++) {
        editor->put_string(editor->ctx, ESC_LEFT);
    }
}

// Inline helper functions (better than macros)
static inline bool editor_is_valid(const LineEditor *editor) {
    return editor != NULL;
}

static void editor_move_cursor_to_start(LineEditor *editor) {
    if (!editor) return;
    for (uint16_t i = 0; i < editor->cursor_pos; i++) {
        editor->put_string(editor->ctx, ESC_LEFT);
    }
    editor->cursor_pos = 0;
}

static void editor_redraw_from_start(LineEditor *editor) {
    if (!editor) return;
    editor_move_cursor_to_start(editor);
    editor->put_string(editor->ctx, ESC_CLEAR);
    editor->put_string(editor->ctx, editor->buffer.str ? editor->buffer.str->data : "");
    editor->cursor_pos = editor->buffer.length;
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
    editor_redraw_from_start(editor);
}

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

static int handle_ansi_escape_sequence(LineEditor *editor, const char *input, int len) {
    if (!is_ansi_escape_sequence(input, len)) {
        return 0;
    }
    
    char command = input[2];
    switch (command) {
        case 'A': // Up arrow - navigate history backwards
            {
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
                editor->cursor_pos++;
                editor->put_string(editor->ctx, ESC_RIGHT);
            }
            return 3;
        case 'D': // Left arrow
            if (editor->cursor_pos > 0) {
                editor->cursor_pos--;
                editor->put_string(editor->ctx, ESC_LEFT);
            }
            return 3;
        case 'H': // Home
            editor->cursor_pos = 0;
            editor->put_string(editor->ctx, ESC_HOME);
            return 3;
        case 'F': // End
            editor->cursor_pos = editor->buffer.length;
            editor->put_string(editor->ctx, ESC_HOME);
            move_cursor_right(editor, editor->buffer.length);
            return 3;
        case 'K': // Clear line from cursor
            editor->put_string(editor->ctx, ESC_CLEAR);
            return 3;
        case '3': // Delete key
            if (len >= 4 && input[3] == '~') {
                if (editor->cursor_pos < editor->buffer.length) {
                    buffer_delete_char(&editor->buffer, editor->cursor_pos);
                    // Redraw from cursor position
                    editor->put_string(editor->ctx, ESC_CLEAR);
                    for (uint16_t i = editor->cursor_pos; i < editor->buffer.length; i++) {
                        editor->put_char(editor->ctx, editor->buffer.str->data[i]);
                    }
                    // Move cursor back to position
                    move_cursor_left(editor, editor->buffer.length - editor->cursor_pos);
                }
                return 4; // Consumed 4 bytes
            }
            return 3;
    }
    
    return 3; // Default: consume 3 bytes for unknown escape sequences
}

// Line editing operations
static void insert_character(LineEditor *editor, char c) {
    if (!buffer_insert_char(&editor->buffer, editor->cursor_pos, c)) {
        editor_ring_bell(editor);
        return;
    }
    editor->cursor_pos++;
    
    // Display the character
    editor->put_char(editor->ctx, c);
    
    // Redraw remaining characters
    for (uint16_t i = editor->cursor_pos; i < editor->buffer.length; i++) {
        editor->put_char(editor->ctx, editor->buffer.str->data[i]);
    }
    
    // Move cursor back to correct position
    for (uint16_t i = editor->cursor_pos; i < editor->buffer.length; i++) {
        editor->put_string(editor->ctx, ESC_LEFT);
    }
}

static void backspace_character(LineEditor *editor) {
    if (editor->cursor_pos > 0) {
        // Move cursor left
        editor->cursor_pos--;
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
        move_cursor_left(editor, editor->buffer.length - editor->cursor_pos);
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
    editor->cursor_pos = 0;
    editor->line_ready = false;
    editor->get_char = get_char;
    editor->put_char = put_char;
    editor->put_string = put_string;
    editor->ctx = ctx;
    
    // Initialize escape sequence handling
    editor->escape_buffer[0] = '\0';
    editor->escape_pos = 0;
    editor->in_escape_sequence = false;
    
    // Initialize history support with transient vector for efficient in-place operations
    CljVector *persistent_vec = make_vector(50, CLJ_VECTOR);  // Start with persistent vector
    editor->history = vector_transient(persistent_vec);      // Convert to transient for efficient operations
    RELEASE(persistent_vec);  // Release the persistent version
    editor->history_index = -1;  // Not browsing
    editor->history_has_temp = false;
    
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
        while (editor->escape_pos < sizeof(editor->escape_buffer)) {
            int c = editor->get_char(editor->ctx);
            if (c == -1) {
                // EOF during escape sequence, reset
                editor->in_escape_sequence = false;
                editor->escape_pos = 0;
                return LINE_EDITOR_EOF;
            }
            
            editor->escape_buffer[editor->escape_pos++] = (char)c;
            if (editor->escape_pos < sizeof(editor->escape_buffer)) {
                editor->escape_buffer[editor->escape_pos] = '\0';
            }
            
            // Check if we have a complete escape sequence
            if (editor->escape_pos >= 3 && editor->escape_buffer[1] == '[') {
                if (editor->escape_buffer[2] != '3') {
                    editor->in_escape_sequence = false;
                    editor->escape_pos = 0;
                    handle_ansi_escape_sequence(editor, editor->escape_buffer, 3);
                    return LINE_EDITOR_SUCCESS;
                }
            }
            
            // Check for longer escape sequences (like \033[3~ for delete)
            if (editor->escape_pos >= 4 && editor->escape_buffer[1] == '[' && 
                editor->escape_buffer[2] == '3' && editor->escape_buffer[3] == '~') {
                editor->in_escape_sequence = false;
                editor->escape_pos = 0;
                
                // Handle delete key - don't display escape sequence
                if (editor->cursor_pos < editor->buffer.length) {
                    if (editor->history_has_temp && editor->history_index > 0) {
                        history_exit(editor);
                    }
                    buffer_delete_char(&editor->buffer, editor->cursor_pos);
                    // Redraw from cursor position
                    editor->put_string(editor->ctx, ESC_CLEAR);
                    for (uint16_t i = editor->cursor_pos; i < editor->buffer.length; i++) {
                        editor->put_char(editor->ctx, editor->buffer.str->data[i]);
                    }
                    // Move cursor back to position
                    move_cursor_left(editor, editor->buffer.length - editor->cursor_pos);
                }
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
        return LINE_EDITOR_EOF;
    }
    
    // Handle newline (submit line)
    if (c == '\n' || c == '\r') {
        if (editor->buffer.length > 0) {
            // Ensure temp history entry does not leak into persisted history.
            history_exit(editor);
            editor->line_ready = true;
            editor->put_char(editor->ctx, '\n');
            return LINE_EDITOR_LINE_READY;
        }
        return LINE_EDITOR_SUCCESS;
    }
    
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

void line_editor_clear(LineEditor *editor) {
    if (!editor_is_valid(editor)) return;
    history_exit(editor);
    buffer_clear(&editor->buffer);
    editor->cursor_pos = 0;
    editor->line_ready = false;
}

void line_editor_reset(LineEditor *editor) {
    line_editor_clear(editor);
}

void line_editor_add_to_history(LineEditor *editor, const char *line) {
    if (!editor || !line) return;
    
    // Check if this line is identical to the last history entry
    // Convert transient to persistent temporarily for checking
    CljVector *history_vec = editor->history;
    CljVector *temp_persistent = NULL;
    if (history_vec && TAG(history_vec) == CLJ_VECTOR_TRANSIENT) {
        temp_persistent = (CljVector*)vector_persistent(history_vec);
        if (temp_persistent) {
            int count = vector_count(temp_persistent);
            if (count > 0) {
                CljString *last_line = line_editor_get_history_line(editor, count - 1);
                if (last_line) {
                    const char *str_data = clj_string_data(last_line);
                    if (strcmp(line, str_data) == 0) {
                        // Duplicate of last entry - skip adding
                        RELEASE(last_line);
                        RELEASE(temp_persistent);
                        return;
                    }
                    RELEASE(last_line);
                }
            }
            RELEASE(temp_persistent);
        }
    } else {
        int count = vector_count(history_vec);
        if (count > 0) {
            CljString *last_line = line_editor_get_history_line(editor, count - 1);
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
    }
    
    // Create string object and add to history vector using transient conj
    // Use vector_conj_inplace to avoid AUTORELEASE (editor->history is a transient vector)
    CljObject *line_obj = (CljObject*)make_string(line);
    if (line_obj) {
        vector_conj_inplace((CljVector**)&editor->history, line_obj);
        // line_obj is now retained by the vector, we can release our reference
        RELEASE(line_obj);
    }
}

CljString* line_editor_get_history_line(LineEditor *editor, int index) {
    if (!editor) return NULL;
    
    // Convert transient vector to persistent if needed
    CljVector *history_vec = editor->history;
    ID elem = vector_nth(history_vec, index);
    // Retain element for return value (caller will release it)
    return elem ? (CljString*)RETAIN(elem) : NULL;
}

int line_editor_get_history_size(const LineEditor *editor) {
    return editor ? vector_count(editor->history) : 0;
}

CljVector* line_editor_get_history_vector(LineEditor *editor) {
    if (!editor) return empty_vector();
    
    CljVector *history_vec = editor->history;
    if (!history_vec) return empty_vector();
    
    int n = vector_count(history_vec);
    if (!n) return empty_vector();
    
    // Return transient vector directly - history_save_to_file handles conversion
    return history_vec;
}

void line_editor_clear_history(LineEditor *editor) {
    if (!editor) return;
    history_exit(editor);
    RELEASE(editor->history);
    CljVector *persistent_vec = make_vector(50, CLJ_VECTOR);
    editor->history = vector_transient(persistent_vec);
    RELEASE(persistent_vec);
    editor->history_index = -1;
    editor->history_has_temp = false;
}

void line_editor_set_history_from_vector(LineEditor *editor, CljVector *vec) {
    if (!editor || !vec || TAG(vec) != CLJ_VECTOR) return;
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
extern bool history_save_to_file(CljVector *vec, const char *path);
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
    // history_save_to_file handles transient to persistent conversion
    return history_save_to_file((CljVector*)vec, path);
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

void line_editor_reset(LineEditor *editor) {
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

CljVector* line_editor_get_history_vector(LineEditor *editor) {
    (void)editor;
    return empty_vector();
}

void line_editor_set_history_from_vector(LineEditor *editor, CljVector *vec) {
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
