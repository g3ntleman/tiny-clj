#ifndef TINY_CLJ_LINE_EDITOR_H
#define TINY_CLJ_LINE_EDITOR_H

#include <stdbool.h>

// Forward declarations
typedef struct LineEditor LineEditor;

// Platform abstraction functions
typedef int (*GetCharFunc)(void);
typedef void (*PutCharFunc)(char);
typedef void (*PutStringFunc)(const char*);

// Return codes for line editor operations
#define LINE_EDITOR_SUCCESS    0
#define LINE_EDITOR_EOF       -1
#define LINE_EDITOR_ERROR     -2
#define LINE_EDITOR_LINE_READY 1

// Line editor API
LineEditor* line_editor_new(GetCharFunc get_char, PutCharFunc put_char, PutStringFunc put_string);
void line_editor_free(LineEditor *editor);

// Process input and return status
int line_editor_process_input(LineEditor *editor);

// Get current state
const char* line_editor_get_buffer(const LineEditor *editor);
int line_editor_get_cursor_pos(const LineEditor *editor);
int line_editor_get_length(const LineEditor *editor);
bool line_editor_is_line_ready(const LineEditor *editor);

// Reset editor state
void line_editor_clear(LineEditor *editor);
void line_editor_reset(LineEditor *editor);

// History support (optional)
void line_editor_add_to_history(LineEditor *editor, const char *line);
const char* line_editor_get_history_line(LineEditor *editor, int index);
int line_editor_get_history_size(const LineEditor *editor);

// Global line editor management
void set_line_editor(LineEditor *editor);
LineEditor* get_line_editor(void);
void cleanup_line_editor(void);

#endif // TINY_CLJ_LINE_EDITOR_H
