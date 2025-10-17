#include "line_editor.h"
#include "vector.h"
#include "string.h"
#include "memory.h"
#include "memory_profiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global line editor instance
static LineEditor *global_editor = NULL;

// ANSI escape sequence constants
static const char ESC_RIGHT[] = "\033[C";
static const char ESC_LEFT[] = "\033[D";
static const char ESC_CLEAR[] = "\033[K";
static const char ESC_HOME[] = "\033[1G";


#ifdef ENABLE_LINE_EDITING

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
    
    // History support using CljPersistentVector
    CljObject *history;        // CljPersistentVector für History
    int history_index;         // Current position in history (-1 = new line)
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
            if (editor->history_index == -1) {
                // First time going up - save current line and go to last history item
                strncpy(editor->temp_buffer, editor->buffer, sizeof(editor->temp_buffer) - 1);
                editor->temp_buffer[sizeof(editor->temp_buffer) - 1] = '\0';
                editor->history_index = line_editor_get_history_size(editor) - 1;
            } else if (editor->history_index > 0) {
                editor->history_index--;
            } else {
                // Already at beginning of history - ring bell
                editor->put_char('\a');  // Bell character
                return 3;
            }
            
            if (editor->history_index >= 0) {
                const char *history_line = line_editor_get_history_line(editor, editor->history_index);
                if (history_line) {
                    // Move cursor to beginning of current input and clear to end of line
                    for (int i = 0; i < editor->cursor_pos; i++) {
                        editor->put_string(ESC_LEFT);
                    }
                    editor->put_string(ESC_CLEAR);
                    strncpy(editor->buffer, history_line, sizeof(editor->buffer) - 1);
                    editor->buffer[sizeof(editor->buffer) - 1] = '\0';
                    editor->length = strlen(editor->buffer);
                    editor->cursor_pos = editor->length;
                    editor->put_string(editor->buffer);
                }
            }
            return 3; // Consumed 3 bytes
        case 'B': // Down arrow - navigate history forwards
            if (editor->history_index >= 0) {
                editor->history_index++;
                if (editor->history_index >= line_editor_get_history_size(editor)) {
                    // Past end of history - restore temp buffer
                    editor->history_index = -1;
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
                    const char *history_line = line_editor_get_history_line(editor, editor->history_index);
                    if (history_line) {
                        // Move cursor to beginning of current input and clear to end of line
                        for (int i = 0; i < editor->cursor_pos; i++) {
                            editor->put_string(ESC_LEFT);
                        }
                        editor->put_string(ESC_CLEAR);
                        strncpy(editor->buffer, history_line, sizeof(editor->buffer) - 1);
                        editor->buffer[sizeof(editor->buffer) - 1] = '\0';
                        editor->length = strlen(editor->buffer);
                        editor->cursor_pos = editor->length;
                        editor->put_string(editor->buffer);
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
    CljValue persistent_vec = make_vector_v(50, 0);  // Start with persistent vector
    editor->history = transient(persistent_vec);      // Convert to transient for efficient operations
    RELEASE((CljObject*)persistent_vec);  // Release the persistent version
    editor->history_index = -1;  // -1 means we're on a new line
    editor->temp_buffer[0] = '\0';
    
    return editor;
}

void line_editor_free(LineEditor *editor) {
    if (editor) {
        // Release history vector (automatically frees all contained strings)
        if (editor->history) {
            RELEASE(editor->history);
        }
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
            editor->history_index = -1;
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
    if (!editor || !line || !editor->history) return;
    
    // Check if this line is identical to the last history entry
    CljPersistentVector *vec = as_vector(editor->history);
    if (vec && vec->count > 0) {
        const char *last_line = line_editor_get_history_line(editor, vec->count - 1);
        if (last_line && strcmp(line, last_line) == 0) {
            // Duplicate of last entry - skip adding
            return;
        }
    }
    
    // Create string object and add to history vector using transient conj
    CljObject *line_obj = make_string(line);
    if (line_obj) {
        editor->history = conj_v(editor->history, line_obj);
        // line_obj is now retained by the vector, we can release our reference
        RELEASE(line_obj);
    }
}

const char* line_editor_get_history_line(LineEditor *editor, int index) {
    if (!editor || !editor->history) return NULL;
    
    CljPersistentVector *vec = as_vector(editor->history);
    if (!vec || index < 0 || index >= vec->count) return NULL;
    
    CljObject *line_obj = vec->data[index];
    if (!line_obj || !is_type(line_obj, CLJ_STRING)) return NULL;
    
    return (const char*)line_obj->as.data;
}

int line_editor_get_history_size(const LineEditor *editor) {
    if (!editor || !editor->history) return 0;
    
    CljPersistentVector *vec = as_vector(editor->history);
    return vec ? vec->count : 0;
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
        editor->history_index = -1;  // Reset to new line mode
    }
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

const char* line_editor_get_history_line(LineEditor *editor, int index) {
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
