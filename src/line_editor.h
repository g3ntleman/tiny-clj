#ifndef TINY_CLJ_LINE_EDITOR_H
#define TINY_CLJ_LINE_EDITOR_H

#include <stdbool.h>
#include <stddef.h>
#include "object.h"
#include "vector.h"
#include "strings.h"  // For CljString

// Forward declarations
typedef struct LineEditor LineEditor;

// Platform abstraction functions
typedef int (*GetCharFunc)(void *ctx);
typedef void (*PutCharFunc)(void *ctx, char);
typedef void (*PutStringFunc)(void *ctx, const char*);

// Line editor state structure for reduced API
typedef struct {
    char buffer[512];
    int cursor_pos;
    int length;
    bool line_ready;
} LineEditorState;

// Return codes for line editor operations
// NOTE:
// - These are return values of line_editor_process_input().
// - "No input available" is signaled by GetCharFunc returning LINE_EDITOR_GETCHAR_NO_INPUT.
typedef enum {
    // Generic error (e.g. feature compiled out, invalid state)
    LINE_EDITOR_ERROR = -3,
    LINE_EDITOR_EOF = -1,
    LINE_EDITOR_SUCCESS = 0,
    LINE_EDITOR_LINE_READY = 1,
} LineEditorResult;

// Return value contract for GetCharFunc:
// - >= 0: a byte/character
// - -1: EOF
// - -2: no input available (non-blocking)
#define LINE_EDITOR_GETCHAR_NO_INPUT (-2)

// Line editor API
LineEditor* line_editor_new(GetCharFunc get_char, PutCharFunc put_char, PutStringFunc put_string, void *ctx);
void line_editor_free(LineEditor *editor);

// Process input and return status
int line_editor_process_input(LineEditor *editor);

// Get current state (reduced API)
int line_editor_get_state(const LineEditor *editor, LineEditorState *state);

// Get current editable buffer (may be larger than LineEditorState.buffer).
// The returned pointer is owned by the editor and remains valid until the editor
// is reset/cleared/freed.
const char* line_editor_get_buffer_cstr(const LineEditor *editor, size_t *len);

// Reset editor state
void line_editor_clear(LineEditor *editor);
void line_editor_reset(LineEditor *editor);

// History support (optional)
void line_editor_add_to_history(LineEditor *editor, const char *line);
CljString* line_editor_get_history_line(LineEditor *editor, int index);  // Returns retained CljString (caller must RELEASE)
int line_editor_get_history_size(const LineEditor *editor);

// History bulk operations
CljVector* line_editor_get_history_vector(LineEditor *editor); // returns persistent vector (rc=1)
void line_editor_set_history_from_vector(LineEditor *editor, CljVector *vec);
void line_editor_clear_history(LineEditor *editor);

// Global line editor management
void set_line_editor(LineEditor *editor);
LineEditor* get_line_editor(void);
void cleanup_line_editor(void);

// Reset history index (used after exceptions)
void line_editor_reset_history_index(LineEditor *editor);

#endif // TINY_CLJ_LINE_EDITOR_H
