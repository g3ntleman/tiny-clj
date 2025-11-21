#include "line_editor.h"
#include "memory.h"  // For RELEASE
#include "value.h"  // For make_string, fixnum, CljString
#include "builtins.h"  // For nth2
#include "strings.h"  // For to_string
#include "strings.h"  // For string functions
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>  // For UINT_MAX

#ifdef ENABLE_LINE_EDITING
// Global line editor instance
static LineEditor *global_editor = NULL;

// ANSI escape sequence constants
static const char ESC_RIGHT[] = "\033[C";
static const char ESC_LEFT[] = "\033[D";
static const char ESC_CLEAR[] = "\033[K";
static const char ESC_HOME[] = "\033[1G";

struct LineEditor {
    char buffer[512];
    int cursor_pos;
    int length;
    bool line_ready;
    GetCharFunc get_char;
    PutCharFunc put_char;
    PutStringFunc put_string;
    
    // ANSI escape sequence buffer
    char escape_buffer[8];
    int escape_pos;
    bool in_escape_sequence;
    
    // History support using CljVector
    CljVector *history;        // CljVector für History
    unsigned int history_index;  // Current position in history (UINT_MAX = new line)
    char temp_buffer[512];     // Backup of current line when browsing history
};

// Generic cursor movement functions
static void move_cursor_right(LineEditor *editor, int steps) {
    for (int i = 0; i < steps; i++) {
        editor->put_string(ESC_RIGHT);
    }
}

static void move_cursor_left(LineEditor *editor, int steps) {
    for (int i = 0; i < steps; i++) {
        editor->put_string(ESC_LEFT);
    }
}

// Generic buffer shift functions
static void shift_buffer_left(LineEditor *editor, int start, int count) {
    for (int i = start; i < editor->length - count; i++) {
        editor->buffer[i] = editor->buffer[i + count];
    }
}

static void shift_buffer_right(LineEditor *editor, int start, int count) {
    for (int i = editor->length; i > start; i--) {
        editor->buffer[i] = editor->buffer[i - count];
    }
}

// Inline helper functions (better than macros)
static inline bool editor_is_valid(const LineEditor *editor) {
    return editor != NULL;
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
                int history_size = line_editor_get_history_size(editor);
                if (history_size == 0) {
                    // No history available - ring bell
                    editor->put_char('\a');
                    return 3;
                }
                if (editor->history_index == UINT_MAX) {
                    // First time going up - save current line and go to last history item
                    strncpy(editor->temp_buffer, editor->buffer, sizeof(editor->temp_buffer) - 1);
                    editor->temp_buffer[sizeof(editor->temp_buffer) - 1] = '\0';
                    editor->history_index = history_size - 1;
                } else if (editor->history_index > 0) {
                    editor->history_index--;
                } else {
                    // Already at beginning of history - ring bell
                    editor->put_char('\a');  // Bell character
                    return 3;
                }
                
                if (editor->history_index != UINT_MAX) {
                    CljString *history_line = line_editor_get_history_line(editor, editor->history_index);
                    if (history_line) {
                        // Move cursor to beginning of current input and clear to end of line
                        for (int i = 0; i < editor->cursor_pos; i++) {
                            editor->put_string(ESC_LEFT);
                        }
                        editor->put_string(ESC_CLEAR);
                        const char *str_data = clj_string_data(history_line);
                        strncpy(editor->buffer, str_data, sizeof(editor->buffer) - 1);
                        editor->buffer[sizeof(editor->buffer) - 1] = '\0';
                        editor->length = strlen(editor->buffer);
                        editor->cursor_pos = editor->length;
                        editor->put_string(editor->buffer);
                        // Release the retained CljString
                        RELEASE((CljObject*)history_line);
                    } else {
                        // History line not found - reset to new line
                        editor->history_index = UINT_MAX;
                    }
                }
                return 3; // Consumed 3 bytes
            }
        case 'B': // Down arrow - navigate history forwards
            if (editor->history_index != UINT_MAX) {
                editor->history_index++;
                if (editor->history_index >= (unsigned int)line_editor_get_history_size(editor)) {
                    // Past end of history - restore temp buffer
                    editor->history_index = UINT_MAX;
                    // Move cursor to beginning of current input and clear to end of line
                    for (int i = 0; i < editor->cursor_pos; i++) {
                        editor->put_string(ESC_LEFT);
                    }
                    editor->put_string(ESC_CLEAR);
                    strncpy(editor->buffer, editor->temp_buffer, sizeof(editor->buffer) - 1);
                    editor->buffer[sizeof(editor->buffer) - 1] = '\0';
                    editor->length = strlen(editor->buffer);
                    editor->cursor_pos = editor->length;
                    editor->put_string(editor->buffer);
                } else {
                    // Load next history item
                    CljString *history_line = line_editor_get_history_line(editor, editor->history_index);
                    if (history_line) {
                        // Move cursor to beginning of current input and clear to end of line
                        for (int i = 0; i < editor->cursor_pos; i++) {
                            editor->put_string(ESC_LEFT);
                        }
                        editor->put_string(ESC_CLEAR);
                        const char *str_data = clj_string_data(history_line);
                        strncpy(editor->buffer, str_data, sizeof(editor->buffer) - 1);
                        editor->buffer[sizeof(editor->buffer) - 1] = '\0';
                        editor->length = strlen(editor->buffer);
                        editor->cursor_pos = editor->length;
                        editor->put_string(editor->buffer);
                        // Release the retained CljString
                        RELEASE((CljObject*)history_line);
                    }
                }
            } else {
                // Not in history mode - ring bell
                editor->put_char('\a');  // Bell character
            }
            return 3;
        case 'C': // Right arrow
            if (editor->cursor_pos < editor->length) {
                editor->cursor_pos++;
                editor->put_string(ESC_RIGHT);
            }
            return 3;
        case 'D': // Left arrow
            if (editor->cursor_pos > 0) {
                editor->cursor_pos--;
                editor->put_string(ESC_LEFT);
            }
            return 3;
        case 'H': // Home
            editor->cursor_pos = 0;
            editor->put_string(ESC_HOME);
            return 3;
        case 'F': // End
            editor->cursor_pos = editor->length;
            editor->put_string(ESC_HOME);
            move_cursor_right(editor, editor->length);
            return 3;
        case 'K': // Clear line from cursor
            editor->put_string(ESC_CLEAR);
            return 3;
        case '3': // Delete key
            if (len >= 4 && input[3] == '~') {
                if (editor->cursor_pos < editor->length) {
                    // Delete character at cursor position
                    shift_buffer_left(editor, editor->cursor_pos, 1);
                    editor->length--;
                    // Redraw from cursor position
                    editor->put_string(ESC_CLEAR);
                    for (int i = editor->cursor_pos; i < editor->length; i++) {
                        editor->put_char(editor->buffer[i]);
                    }
                    // Move cursor back to position
                    move_cursor_left(editor, editor->length - editor->cursor_pos);
                }
                return 4; // Consumed 4 bytes
            }
            return 3;
    }
    
    return 3; // Default: consume 3 bytes for unknown escape sequences
}

// Line editing operations
static void insert_character(LineEditor *editor, char c) {
    if (editor->length >= (int)sizeof(editor->buffer) - 1) {
        return; // Buffer full
    }
    
    // Shift characters right to make space
    shift_buffer_right(editor, editor->cursor_pos, 1);
    
    // Insert character
    editor->buffer[editor->cursor_pos] = c;
    editor->cursor_pos++;
    editor->length++;
    
    // Display the character
    editor->put_char(c);
    
    // Redraw remaining characters
    for (int i = editor->cursor_pos; i < editor->length; i++) {
        editor->put_char(editor->buffer[i]);
    }
    
    // Move cursor back to correct position
    for (int i = editor->cursor_pos; i < editor->length; i++) {
        editor->put_string(ESC_LEFT);
    }
}

static void backspace_character(LineEditor *editor) {
    if (editor->cursor_pos > 0) {
        // Move cursor left
        editor->cursor_pos--;
        editor->put_string(ESC_LEFT);
        
        // Delete character
        shift_buffer_left(editor, editor->cursor_pos, 1);
        editor->length--;
        
        // Clear from cursor to end of line
        editor->put_string(ESC_CLEAR);
        
        // Redraw remaining characters
        for (int i = editor->cursor_pos; i < editor->length; i++) {
            editor->put_char(editor->buffer[i]);
        }
        
        // Move cursor back to position
        move_cursor_left(editor, editor->length - editor->cursor_pos);
    }
}

LineEditor* line_editor_new(GetCharFunc get_char, PutCharFunc put_char, PutStringFunc put_string) {
    LineEditor *editor = malloc(sizeof(LineEditor));
    if (!editor) return NULL;
    
    editor->buffer[0] = '\0';
    editor->cursor_pos = 0;
    editor->length = 0;
    editor->line_ready = false;
    editor->get_char = get_char;
    editor->put_char = put_char;
    editor->put_string = put_string;
    
    // Initialize escape sequence handling
    editor->escape_buffer[0] = '\0';
    editor->escape_pos = 0;
    editor->in_escape_sequence = false;
    
    // Initialize history support with transient vector for efficient in-place operations
    CljVector *persistent_vec = make_vector(50, CLJ_VECTOR);  // Start with persistent vector
    editor->history = vector_transient(persistent_vec);      // Convert to transient for efficient operations
    RELEASE(persistent_vec);  // Release the persistent version
    editor->history_index = UINT_MAX;  // UINT_MAX means we're on a new line
    editor->temp_buffer[0] = '\0';
    
    return editor;
}

void line_editor_free(LineEditor *editor) {
    if (editor) {
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
        while (editor->escape_pos < 8) {
            int c = editor->get_char();
            if (c == -1) {
                // EOF during escape sequence, reset
                editor->in_escape_sequence = false;
                editor->escape_pos = 0;
                return LINE_EDITOR_EOF;
            }
            
            editor->escape_buffer[editor->escape_pos++] = c;
            editor->escape_buffer[editor->escape_pos] = '\0';
            
            // Check if we have a complete escape sequence
            if (editor->escape_pos >= 3 && editor->escape_buffer[1] == '[') {
                editor->in_escape_sequence = false;
                editor->escape_pos = 0;
                
                // Handle escape sequence - don't display it
                handle_ansi_escape_sequence(editor, editor->escape_buffer, 3);
                return LINE_EDITOR_SUCCESS;
            }
            
            // Check for longer escape sequences (like \033[3~ for delete)
            if (editor->escape_pos >= 4 && editor->escape_buffer[1] == '[' && 
                editor->escape_buffer[2] == '3' && editor->escape_buffer[3] == '~') {
                editor->in_escape_sequence = false;
                editor->escape_pos = 0;
                
                // Handle delete key - don't display escape sequence
                if (editor->cursor_pos < editor->length) {
                    // Delete character at cursor position
                    shift_buffer_left(editor, editor->cursor_pos, 1);
                    editor->length--;
                    // Redraw from cursor position
                    editor->put_string(ESC_CLEAR);
                    for (int i = editor->cursor_pos; i < editor->length; i++) {
                        editor->put_char(editor->buffer[i]);
                    }
                    // Move cursor back to position
                    move_cursor_left(editor, editor->length - editor->cursor_pos);
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
    int c = editor->get_char();
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
        if (editor->length > 0) {
            editor->buffer[editor->length] = '\0';
            editor->line_ready = true;
            editor->put_char('\n');
            // Reset history index when submitting a line
            editor->history_index = UINT_MAX;
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
    
    strncpy(state->buffer, editor->buffer, sizeof(state->buffer) - 1);
    state->buffer[sizeof(state->buffer) - 1] = '\0';
    state->cursor_pos = editor->cursor_pos;
    state->length = editor->length;
    state->line_ready = editor->line_ready;
    
    return LINE_EDITOR_SUCCESS;
}

void line_editor_clear(LineEditor *editor) {
    if (!editor_is_valid(editor)) return;
    editor->buffer[0] = '\0';
    editor->cursor_pos = 0;
    editor->length = 0;
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
                        RELEASE((CljObject*)last_line);
                        RELEASE(temp_persistent);
                        return;
                    }
                    RELEASE((CljObject*)last_line);
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
                    RELEASE((CljObject*)last_line);
                    return;
                }
                RELEASE((CljObject*)last_line);
            }
        }
    }
    
    // Create string object and add to history vector using transient conj
    CljObject *line_obj = (CljObject*)make_string(line);
    if (line_obj) {
        editor->history = clj_conj(editor->history, line_obj);
        // line_obj is now retained by the vector, we can release our reference
        RELEASE(line_obj);
    }
}

CljString* line_editor_get_history_line(LineEditor *editor, int index) {
    if (!editor) return NULL;
    
    // Convert transient vector to persistent if needed
    CljVector *history_vec = editor->history;
    return vector_nth(history_vec, index);
}

int line_editor_get_history_size(const LineEditor *editor) {
    return editor ? vector_count(editor->history) : 0;
}

CljVector* line_editor_get_history_vector(LineEditor *editor) {
    if (!editor) return empty_vector();
    
    // Convert transient vector to persistent if needed
    CljVector *history_vec = editor->history;
    if (history_vec && TAG(history_vec) == CLJ_VECTOR_TRANSIENT) {
        history_vec = (CljVector*)vector_persistent(history_vec);
        if (!history_vec) return empty_vector();
    }
    
    int n = vector_count(history_vec);
    if (!n) return empty_vector();
    
    CljVector *out = make_vector(n, CLJ_VECTOR);
    ID nth_args[2];
    nth_args[0] = history_vec;
    for (int i = 0; i < n; i++) {
        nth_args[1] = fixnum(i);
        ID elem = nth2(nth_args, 2);
        if (elem) {
            out = vector_conj(out, elem);
            RELEASE(elem);
        }
    }
    return out;
}

void line_editor_clear_history(LineEditor *editor) {
    if (!editor) return;
    RELEASE(editor->history);
    CljVector *persistent_vec = make_vector(50, CLJ_VECTOR);
    editor->history = vector_transient(persistent_vec);
    RELEASE(persistent_vec);
    editor->history_index = UINT_MAX;
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
            const char *plain = to_string(elem);
            if (plain) {
                line_editor_add_to_history(editor, plain);
                free((void*)plain);
            }
            RELEASE(elem);
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
        editor->history_index = UINT_MAX;  // Reset to new line mode
    }
}

// Optional: Default-Persistenzpfad (~/.tiny-clj/history.edn)
static void build_default_history_path(char *out, size_t out_sz) {
    const char *home = getenv("HOME") ? getenv("HOME") : ".";
    snprintf(out, out_sz, "%s/.tiny-clj", home);
    // Ensure directory exists
    mkdir(out, 0700);
    snprintf(out, out_sz, "%s/.tiny-clj/history.edn", home);
}

// Externe Persistenz-Funktionen (in repl.c definiert)
extern CljObject* history_trim_last_n(CljObject *vec, int limit);
extern bool history_save_to_file(CljObject *vec, const char *path);
extern CljObject* history_load_from_file(const char *path);

// Laden der History an Default-Pfad, Ergebnis als persistent Vector<String>
CljObject* line_editor_history_load_default(void) {
    char path[512];
    build_default_history_path(path, sizeof(path));
    return history_load_from_file(path);
}

// Speichern der History an Default-Pfad; akzeptiert Vector<String> (persistent/transient ok)
bool line_editor_history_save_default(CljObject *vec) {
    char path[512];
    build_default_history_path(path, sizeof(path));
    // Falls transient, in persistent umwandeln
    if (vec && vec->type == CLJ_VECTOR_TRANSIENT) {
        vec = (CljObject*)vector_persistent((CljVector*)vec);
    }
    return history_save_to_file(vec, path);
}

#else
// Stub implementations when line editing is disabled
LineEditor* line_editor_new(GetCharFunc get_char, PutCharFunc put_char, PutStringFunc put_string) {
    (void)get_char; (void)put_char; (void)put_string;
    return NULL;
}

void line_editor_free(LineEditor *editor) {
    (void)editor;
}

int line_editor_process_input(LineEditor *editor) {
    (void)editor;
    return LINE_EDITOR_ERROR;
}

const char* line_editor_get_buffer(const LineEditor *editor) {
    (void)editor;
    return NULL;
}

int line_editor_get_cursor_pos(const LineEditor *editor) {
    (void)editor;
    return -1;
}

int line_editor_get_length(const LineEditor *editor) {
    (void)editor;
    return -1;
}

bool line_editor_is_line_ready(const LineEditor *editor) {
    (void)editor;
    return false;
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

#endif // ENABLE_LINE_EDITING
