#include "minunit.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>

// Mock platform state
static char mock_input_buffer[1024];
static int mock_input_pos = 0;
static char mock_output_buffer[1024];
static int mock_output_pos = 0;
static bool mock_eof_reached = false;

// Mock platform functions
static int mock_platform_get_char(void) {
    if (mock_eof_reached || mock_input_pos >= (int)strlen(mock_input_buffer)) {
        return -1; // EOF
    }
    return mock_input_buffer[mock_input_pos++];
}

static void mock_platform_put_char(char c) {
    if (mock_output_pos < (int)sizeof(mock_output_buffer) - 1) {
        mock_output_buffer[mock_output_pos++] = c;
        mock_output_buffer[mock_output_pos] = '\0';
    }
}

static void mock_platform_put_string(const char *s) {
    while (*s && mock_output_pos < (int)sizeof(mock_output_buffer) - 1) {
        mock_output_buffer[mock_output_pos++] = *s++;
        mock_output_buffer[mock_output_pos] = '\0';
    }
}

// Test setup/teardown
static void setup_mock_platform(const char *input) {
    strncpy(mock_input_buffer, input, sizeof(mock_input_buffer) - 1);
    mock_input_buffer[sizeof(mock_input_buffer) - 1] = '\0';
    mock_input_pos = 0;
    mock_output_pos = 0;
    mock_output_buffer[0] = '\0';
    mock_eof_reached = false;
}

static void set_mock_eof(bool eof) {
    mock_eof_reached = eof;
}

// ============================================================================
// PLATFORM ABSTRACTION TESTS
// ============================================================================

static char *test_platform_get_char_basic(void) {
    setup_mock_platform("abc");
    
    mu_assert("Should get 'a'", mock_platform_get_char() == 'a');
    mu_assert("Should get 'b'", mock_platform_get_char() == 'b');
    mu_assert("Should get 'c'", mock_platform_get_char() == 'c');
    mu_assert("Should get EOF", mock_platform_get_char() == -1);
    
    return NULL;
}

static char *test_platform_get_char_eof(void) {
    setup_mock_platform("");
    set_mock_eof(true);
    
    mu_assert("Should get EOF immediately", mock_platform_get_char() == -1);
    
    return NULL;
}

static char *test_platform_put_char_basic(void) {
    setup_mock_platform("");
    
    mock_platform_put_char('a');
    mock_platform_put_char('b');
    mock_platform_put_char('c');
    
    mu_assert("Should have 'abc' in output", strcmp(mock_output_buffer, "abc") == 0);
    
    return NULL;
}

static char *test_platform_put_string_basic(void) {
    setup_mock_platform("");
    
    mock_platform_put_string("hello");
    mock_platform_put_string(" world");
    
    mu_assert("Should have 'hello world' in output", strcmp(mock_output_buffer, "hello world") == 0);
    
    return NULL;
}

static char *test_platform_ansi_sequences(void) {
    setup_mock_platform("");
    
    // Test ANSI escape sequences
    mock_platform_put_string("\033[D"); // Left arrow
    mock_platform_put_string("\033[C"); // Right arrow
    mock_platform_put_string("\033[A"); // Up arrow
    mock_platform_put_string("\033[B"); // Down arrow
    mock_platform_put_string("\033[K"); // Clear line
    
    mu_assert("Should have ANSI sequences", strlen(mock_output_buffer) > 0);
    mu_assert("Should contain escape sequences", strstr(mock_output_buffer, "\033") != NULL);
    
    return NULL;
}

// ============================================================================
// EDGE CASE TESTS
// ============================================================================

static char *test_platform_buffer_overflow(void) {
    setup_mock_platform("");
    
    // Fill buffer to near capacity
    for (int i = 0; i < 1000; i++) {
        mock_platform_put_char('x');
    }
    
    mu_assert("Should handle buffer overflow gracefully", mock_output_pos < (int)sizeof(mock_output_buffer));
    
    return NULL;
}

static char *test_platform_empty_input(void) {
    setup_mock_platform("");
    
    mu_assert("Should get EOF for empty input", mock_platform_get_char() == -1);
    
    return NULL;
}

static char *test_platform_special_characters(void) {
    setup_mock_platform("\033[A\033[B\033[C\033[D\b\b\b");
    
    // Test escape sequences
    mu_assert("Should get escape character", mock_platform_get_char() == '\033');
    mu_assert("Should get '['", mock_platform_get_char() == '[');
    mu_assert("Should get 'A'", mock_platform_get_char() == 'A');
    
    // Test next escape sequence
    mu_assert("Should get next escape character", mock_platform_get_char() == '\033');
    mu_assert("Should get '['", mock_platform_get_char() == '[');
    mu_assert("Should get 'B'", mock_platform_get_char() == 'B');
    
    // Test next escape sequence
    mu_assert("Should get next escape character", mock_platform_get_char() == '\033');
    mu_assert("Should get '['", mock_platform_get_char() == '[');
    mu_assert("Should get 'C'", mock_platform_get_char() == 'C');
    
    // Test next escape sequence
    mu_assert("Should get next escape character", mock_platform_get_char() == '\033');
    mu_assert("Should get '['", mock_platform_get_char() == '[');
    mu_assert("Should get 'D'", mock_platform_get_char() == 'D');
    
    // Test backspace
    mu_assert("Should get backspace", mock_platform_get_char() == '\b');
    
    return NULL;
}

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

static char *test_platform_echo_input(void) {
    setup_mock_platform("hello");
    
    // Echo input to output
    int c;
    while ((c = mock_platform_get_char()) != -1) {
        mock_platform_put_char(c);
    }
    
    mu_assert("Should echo input to output", strcmp(mock_output_buffer, "hello") == 0);
    
    return NULL;
}

static char *test_platform_line_processing(void) {
    setup_mock_platform("line1\nline2\nline3");
    
    char line_buffer[256];
    int line_pos = 0;
    int c;
    
    // Process first line
    while ((c = mock_platform_get_char()) != -1 && c != '\n') {
        line_buffer[line_pos++] = c;
    }
    line_buffer[line_pos] = '\0';
    
    mu_assert("Should read first line", strcmp(line_buffer, "line1") == 0);
    
    // Reset for second line
    line_pos = 0;
    while ((c = mock_platform_get_char()) != -1 && c != '\n') {
        line_buffer[line_pos++] = c;
    }
    line_buffer[line_pos] = '\0';
    
    mu_assert("Should read second line", strcmp(line_buffer, "line2") == 0);
    
    return NULL;
}

// ============================================================================
// TEST SUITE RUNNER
// ============================================================================

char *run_platform_mock_tests(void) {
    mu_run_test(test_platform_get_char_basic);
    mu_run_test(test_platform_get_char_eof);
    mu_run_test(test_platform_put_char_basic);
    mu_run_test(test_platform_put_string_basic);
    mu_run_test(test_platform_ansi_sequences);
    mu_run_test(test_platform_buffer_overflow);
    mu_run_test(test_platform_empty_input);
    mu_run_test(test_platform_special_characters);
    mu_run_test(test_platform_echo_input);
    mu_run_test(test_platform_line_processing);
    
    return NULL;
}
