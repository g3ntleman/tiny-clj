#include "line_editor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global line editor instance
static LineEditor *global_editor = NULL;

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
};

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
        case 'A': // Up arrow - could be used for history
            return 3; // Consumed 3 bytes
        case 'B': // Down arrow
            return 3;
        case 'C': // Right arrow
            if (editor->cursor_pos < editor->length) {
                editor->cursor_pos++;
                // Move cursor right visually
                editor->put_string("\033[C");
            }
            return 3;
        case 'D': // Left arrow
            if (editor->cursor_pos > 0) {
                editor->cursor_pos--;
                // Move cursor left visually
                editor->put_string("\033[D");
            }
            return 3;
        case 'H': // Home
            editor->cursor_pos = 0;
            // Move cursor to beginning of line
            editor->put_string("\033[1G");
            return 3;
        case 'F': // End
            editor->cursor_pos = editor->length;
            // Move cursor to end of line
            editor->put_string("\033[1G");
            for (int i = 0; i < editor->length; i++) {
                editor->put_string("\033[C");
            }
            return 3;
        case 'K': // Clear line from cursor
            editor->put_string("\033[K");
            return 3;
        case '3': // Delete key
            if (len >= 4 && input[3] == '~') {
                if (editor->cursor_pos < editor->length) {
                    // Delete character at cursor position
                    for (int i = editor->cursor_pos; i < editor->length - 1; i++) {
                        editor->buffer[i] = editor->buffer[i + 1];
                    }
                    editor->length--;
                    // Redraw from cursor position
                    editor->put_string("\033[K");
                    for (int i = editor->cursor_pos; i < editor->length; i++) {
                        editor->put_char(editor->buffer[i]);
                    }
                    // Move cursor back to position
                    for (int i = editor->cursor_pos; i < editor->length; i++) {
                        editor->put_string("\033[D");
                    }
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
    for (int i = editor->length; i > editor->cursor_pos; i--) {
        editor->buffer[i] = editor->buffer[i - 1];
    }
    
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
        editor->put_string("\033[D");
    }
}

static void backspace_character(LineEditor *editor) {
    if (editor->cursor_pos > 0) {
        // Move cursor left
        editor->cursor_pos--;
        editor->put_string("\033[D");
        
        // Delete character
        for (int i = editor->cursor_pos; i < editor->length - 1; i++) {
            editor->buffer[i] = editor->buffer[i + 1];
        }
        editor->length--;
        
        // Clear from cursor to end of line
        editor->put_string("\033[K");
        
        // Redraw remaining characters
        for (int i = editor->cursor_pos; i < editor->length; i++) {
            editor->put_char(editor->buffer[i]);
        }
        
        // Move cursor back to position
        for (int i = editor->cursor_pos; i < editor->length; i++) {
            editor->put_string("\033[D");
        }
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
    
    return editor;
}

void line_editor_free(LineEditor *editor) {
    if (editor) {
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
                    for (int i = editor->cursor_pos; i < editor->length - 1; i++) {
                        editor->buffer[i] = editor->buffer[i + 1];
                    }
                    editor->length--;
                    // Redraw from cursor position
                    editor->put_string("\033[K");
                    for (int i = editor->cursor_pos; i < editor->length; i++) {
                        editor->put_char(editor->buffer[i]);
                    }
                    // Move cursor back to position
                    for (int i = editor->cursor_pos; i < editor->length; i++) {
                        editor->put_string("\033[D");
                    }
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
    
    // Handle newline (submit line)
    if (c == '\n' || c == '\r') {
        if (editor->length > 0) {
            editor->buffer[editor->length] = '\0';
            editor->line_ready = true;
            editor->put_char('\n');
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

const char* line_editor_get_buffer(const LineEditor *editor) {
    if (!editor) return NULL;
    return editor->buffer;
}

int line_editor_get_cursor_pos(const LineEditor *editor) {
    if (!editor) return -1;
    return editor->cursor_pos;
}

int line_editor_get_length(const LineEditor *editor) {
    if (!editor) return -1;
    return editor->length;
}

bool line_editor_is_line_ready(const LineEditor *editor) {
    if (!editor) return false;
    return editor->line_ready;
}

void line_editor_clear(LineEditor *editor) {
    if (!editor) return;
    editor->buffer[0] = '\0';
    editor->cursor_pos = 0;
    editor->length = 0;
    editor->line_ready = false;
}

void line_editor_reset(LineEditor *editor) {
    line_editor_clear(editor);
}

void line_editor_add_to_history(LineEditor *editor, const char *line) {
    (void)editor;
    (void)line;
    // Stub - not implemented yet
}

const char* line_editor_get_history_line(LineEditor *editor, int index) {
    (void)editor;
    (void)index;
    return NULL; // Stub - not implemented yet
}

int line_editor_get_history_size(const LineEditor *editor) {
    (void)editor;
    return 0; // Stub - not implemented yet
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

#endif // ENABLE_LINE_EDITING
