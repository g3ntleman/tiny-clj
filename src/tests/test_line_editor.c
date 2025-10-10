#include "minunit.h"
#include "line_editor.h"
#include <string.h>
#include <stdio.h>

// Mock platform implementation for testing
static char mock_input_buffer[1024];
static int mock_input_pos = 0;
static char mock_output_buffer[1024];
static int mock_output_pos = 0;

// Mock platform functions
static int mock_get_char(void) {
    if (mock_input_pos >= (int)strlen(mock_input_buffer)) {
        return -1; // EOF
    }
    return mock_input_buffer[mock_input_pos++];
}

static void mock_put_char(char c) {
    if (mock_output_pos < (int)sizeof(mock_output_buffer) - 1) {
        mock_output_buffer[mock_output_pos++] = c;
        mock_output_buffer[mock_output_pos] = '\0';
    }
}

static void mock_put_string(const char *s) {
    while (*s) {
        mock_put_char(*s++);
    }
}

// Test setup/teardown
static void setup_mock_input(const char *input) {
    strncpy(mock_input_buffer, input, sizeof(mock_input_buffer) - 1);
    mock_input_buffer[sizeof(mock_input_buffer) - 1] = '\0';
    mock_input_pos = 0;
    mock_output_pos = 0;
    mock_output_buffer[0] = '\0';
}

// ============================================================================
// CURSOR MOVEMENT TESTS
// ============================================================================

static char *test_cursor_movement_left_right(void) {
    setup_mock_input("abc");
    
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    mu_assert("Editor should be created", editor != NULL);
    
    // Type 'abc'
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    
    mu_assert("Should have typed 'abc'", strcmp(line_editor_get_buffer(editor), "abc") == 0);
    mu_assert("Cursor should be at end", line_editor_get_cursor_pos(editor) == 3);
    
    // Move left twice
    setup_mock_input("\033[D"); // First left arrow
    line_editor_process_input(editor);
    
    setup_mock_input("\033[D"); // Second left arrow
    line_editor_process_input(editor);
    
    mu_assert("Cursor should be at position 1", line_editor_get_cursor_pos(editor) == 1);
    
    // Move right once
    setup_mock_input("\033[C"); // Right arrow
    line_editor_process_input(editor);
    
    mu_assert("Cursor should be at position 2", line_editor_get_cursor_pos(editor) == 2);
    
    line_editor_free(editor);
    return NULL;
}

static char *test_cursor_movement_up_down(void) {
    setup_mock_input("line1\nline2");
    
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    mu_assert("Editor should be created", editor != NULL);
    
    // Type first line and submit
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    
    // Should have submitted first line
    mu_assert("Should have submitted line1", strcmp(line_editor_get_buffer(editor), "line1") == 0);
    
    // Test up/down arrows (history navigation)
    setup_mock_input("\033[A"); // Up arrow
    line_editor_process_input(editor);
    
    setup_mock_input("\033[B"); // Down arrow  
    line_editor_process_input(editor);
    
    line_editor_free(editor);
    return NULL;
}

// ============================================================================
// EDITING OPERATIONS TESTS
// ============================================================================

static char *test_backspace_character(void) {
    setup_mock_input("abc\b");
    
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    mu_assert("Editor should be created", editor != NULL);
    
    // Type 'abc'
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    
    mu_assert("Should have typed 'abc'", strcmp(line_editor_get_buffer(editor), "abc") == 0);
    
    // Backspace
    line_editor_process_input(editor);
    
    mu_assert("Should have deleted 'c'", strcmp(line_editor_get_buffer(editor), "ab") == 0);
    mu_assert("Cursor should be at end", line_editor_get_cursor_pos(editor) == 2);
    
    line_editor_free(editor);
    return NULL;
}

static char *test_delete_character(void) {
    setup_mock_input("abc\033[3~"); // Delete key
    
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    mu_assert("Editor should be created", editor != NULL);
    
    // Type 'abc'
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    
    // Move cursor to beginning
    setup_mock_input("\033[H"); // Home
    line_editor_process_input(editor);
    
    // Delete first character
    setup_mock_input("\033[3~"); // Delete
    line_editor_process_input(editor);
    
    mu_assert("Should have deleted 'a'", strcmp(line_editor_get_buffer(editor), "bc") == 0);
    
    line_editor_free(editor);
    return NULL;
}

static char *test_insert_character(void) {
    setup_mock_input("ac");
    
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    mu_assert("Editor should be created", editor != NULL);
    
    // Type 'ac'
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    
    // Move cursor to position 1 (between 'a' and 'c')
    setup_mock_input("\033[D"); // Left arrow
    line_editor_process_input(editor);
    
    // Insert 'b'
    setup_mock_input("b");
    line_editor_process_input(editor);
    
    mu_assert("Should have inserted 'b'", strcmp(line_editor_get_buffer(editor), "abc") == 0);
    
    line_editor_free(editor);
    return NULL;
}

// ============================================================================
// ANSI ESCAPE SEQUENCE TESTS
// ============================================================================

static char *test_ansi_escape_parsing(void) {
    setup_mock_input("\033[A\033[B\033[C\033[D");
    
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    mu_assert("Editor should be created", editor != NULL);
    
    // Process escape sequences
    line_editor_process_input(editor); // Up
    line_editor_process_input(editor); // Down
    line_editor_process_input(editor); // Right
    line_editor_process_input(editor); // Left
    
    // Should not have added any characters to buffer
    mu_assert("Buffer should be empty", strlen(line_editor_get_buffer(editor)) == 0);
    
    line_editor_free(editor);
    return NULL;
}

// ============================================================================
// EOF HANDLING TESTS
// ============================================================================

static char *test_eof_handling(void) {
    setup_mock_input(""); // Empty input = EOF
    
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    mu_assert("Editor should be created", editor != NULL);
    
    // Process EOF
    int result = line_editor_process_input(editor);
    
    mu_assert("Should return EOF indicator", result == LINE_EDITOR_EOF);
    
    line_editor_free(editor);
    return NULL;
}

static char *test_eof_with_partial_input(void) {
    setup_mock_input("abc"); // Partial input, then EOF
    
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    mu_assert("Editor should be created", editor != NULL);
    
    // Type partial input
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    
    mu_assert("Should have partial input", strcmp(line_editor_get_buffer(editor), "abc") == 0);
    
    // Simulate EOF
    setup_mock_input("");
    int result = line_editor_process_input(editor);
    
    mu_assert("Should return EOF with partial input", result == LINE_EDITOR_EOF);
    mu_assert("Should preserve partial input", strcmp(line_editor_get_buffer(editor), "abc") == 0);
    
    line_editor_free(editor);
    return NULL;
}

// ============================================================================
// LINE REDRAW TESTS
// ============================================================================

static char *test_line_redraw_after_backspace(void) {
    setup_mock_input("hello\b");
    
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    mu_assert("Editor should be created", editor != NULL);
    
    // Type 'hello'
    for (int i = 0; i < 5; i++) {
        line_editor_process_input(editor);
    }
    
    // Backspace
    line_editor_process_input(editor);
    
    mu_assert("Should have 'hell' after backspace", strcmp(line_editor_get_buffer(editor), "hell") == 0);
    
    // Check that redraw was called (output buffer should contain redraw commands)
    mu_assert("Should have redraw output", strlen(mock_output_buffer) > 0);
    
    line_editor_free(editor);
    return NULL;
}

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

static char *test_complete_line_editing_sequence(void) {
    setup_mock_input("hello\033[D\033[D\033[D\033[D\033[D\b\bworld");
    
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    mu_assert("Editor should be created", editor != NULL);
    
    // Type 'hello'
    for (int i = 0; i < 5; i++) {
        line_editor_process_input(editor);
    }
    
    // Move to beginning
    for (int i = 0; i < 5; i++) {
        setup_mock_input("\033[D");
        line_editor_process_input(editor);
    }
    
    // Delete 'h' and 'e'
    setup_mock_input("\b\b");
    line_editor_process_input(editor);
    line_editor_process_input(editor);
    
    // Type 'world'
    setup_mock_input("world");
    for (int i = 0; i < 5; i++) {
        line_editor_process_input(editor);
    }
    
    mu_assert("Should have 'worldllo'", strcmp(line_editor_get_buffer(editor), "worldllo") == 0);
    
    line_editor_free(editor);
    return NULL;
}

// ============================================================================
// TEST SUITE RUNNER
// ============================================================================

char *run_line_editor_tests(void) {
    mu_run_test(test_cursor_movement_left_right);
    mu_run_test(test_cursor_movement_up_down);
    mu_run_test(test_backspace_character);
    mu_run_test(test_delete_character);
    mu_run_test(test_insert_character);
    mu_run_test(test_ansi_escape_parsing);
    mu_run_test(test_eof_handling);
    mu_run_test(test_eof_with_partial_input);
    mu_run_test(test_line_redraw_after_backspace);
    mu_run_test(test_complete_line_editing_sequence);
    
    return NULL;
}
