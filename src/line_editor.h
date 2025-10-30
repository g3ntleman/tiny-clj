#ifndef TINY_CLJ_LINE_EDITOR_H
#define TINY_CLJ_LINE_EDITOR_H

#include <stdbool.h>
#include "object.h"

// Forward declarations
typedef struct LineEditor LineEditor;

// Platform abstraction functions
typedef int (*GetCharFunc)(void);
typedef void (*PutCharFunc)(char);
typedef void (*PutStringFunc)(const char*);

// Line editor state structure for reduced API
typedef struct {
    char buffer[512];
    int cursor_pos;
    int length;
    bool line_ready;
} LineEditorState;

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

// Get current state (reduced API)
int line_editor_get_state(const LineEditor *editor, LineEditorState *state);

// Reset editor state
void line_editor_clear(LineEditor *editor);
void line_editor_reset(LineEditor *editor);

// History support (optional)
void line_editor_add_to_history(LineEditor *editor, const char *line);
const char* line_editor_get_history_line(LineEditor *editor, int index);
int line_editor_get_history_size(const LineEditor *editor);

// History bulk operations
CljObject* line_editor_get_history_vector(LineEditor *editor); // returns persistent vector (rc=1)
void line_editor_set_history_from_vector(LineEditor *editor, CljObject *vec);
void line_editor_clear_history(LineEditor *editor);

// Global line editor management
void set_line_editor(LineEditor *editor);
LineEditor* get_line_editor(void);
void cleanup_line_editor(void);

// Reset history index (used after exceptions)
void line_editor_reset_history_index(LineEditor *editor);

#endif // TINY_CLJ_LINE_EDITOR_H
