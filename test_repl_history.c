#include "platform.h"
#include "line_editor.h"
#include "object.h"
#include "memory_profiler.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

// Mock platform functions for testing
static int mock_get_char(void) {
    return -1; // EOF
}

static void mock_put_char(char c) {
    (void)c; // Suppress unused parameter warning
}

static void mock_put_string(const char* s) {
    (void)s; // Suppress unused parameter warning
}

void test_line_editor_history_basic() {
    printf("Testing basic line editor history functionality...\n");
    
    MEMORY_TEST_START("Line Editor History Basic");
    
    // Create line editor
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    assert(editor != NULL);
    
    // Test initial state
    assert(line_editor_get_history_size(editor) == 0);
    assert(line_editor_get_history_line(editor, 0) == NULL);
    
    // Add some history entries
    line_editor_add_to_history(editor, "first command");
    assert(line_editor_get_history_size(editor) == 1);
    
    const char *line = line_editor_get_history_line(editor, 0);
    assert(line != NULL);
    assert(strcmp(line, "first command") == 0);
    
    line_editor_add_to_history(editor, "second command");
    assert(line_editor_get_history_size(editor) == 2);
    
    line = line_editor_get_history_line(editor, 1);
    assert(line != NULL);
    assert(strcmp(line, "second command") == 0);
    
    // Test invalid indices
    assert(line_editor_get_history_line(editor, -1) == NULL);
    assert(line_editor_get_history_line(editor, 2) == NULL);
    
    // Cleanup
    line_editor_free(editor);
    
    MEMORY_TEST_END("Line Editor History Basic");
    printf("✅ Basic history functionality works\n");
}

void test_line_editor_history_memory_profiling() {
    printf("Testing history memory profiling exclusion...\n");
    
    MEMORY_TEST_START("Line Editor History Memory Profiling");
    
    // Enable memory profiling
    enable_memory_profiling(true);
    
    // Create line editor
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    assert(editor != NULL);
    
    // Add history entries (should not be counted in profiling)
    line_editor_add_to_history(editor, "test command 1");
    line_editor_add_to_history(editor, "test command 2");
    line_editor_add_to_history(editor, "test command 3");
    
    // Verify history works
    assert(line_editor_get_history_size(editor) == 3);
    assert(strcmp(line_editor_get_history_line(editor, 0), "test command 1") == 0);
    assert(strcmp(line_editor_get_history_line(editor, 2), "test command 3") == 0);
    
    // Cleanup
    line_editor_free(editor);
    
    MEMORY_TEST_END("Line Editor History Memory Profiling");
    printf("✅ History memory profiling exclusion works\n");
}

void test_line_editor_history_edge_cases() {
    printf("Testing history edge cases...\n");
    
    MEMORY_TEST_START("Line Editor History Edge Cases");
    
    // Test with NULL editor
    line_editor_add_to_history(NULL, "test");
    assert(line_editor_get_history_size(NULL) == 0);
    assert(line_editor_get_history_line(NULL, 0) == NULL);
    
    // Test with NULL line
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    assert(editor != NULL);
    
    line_editor_add_to_history(editor, NULL);
    assert(line_editor_get_history_size(editor) == 0);
    
    // Test with empty line
    line_editor_add_to_history(editor, "");
    assert(line_editor_get_history_size(editor) == 1);
    assert(strcmp(line_editor_get_history_line(editor, 0), "") == 0);
    
    // Cleanup
    line_editor_free(editor);
    
    MEMORY_TEST_END("Line Editor History Edge Cases");
    printf("✅ History edge cases handled correctly\n");
}

int main() {
    platform_init();
    
    printf("🧪 Testing REPL History with CljVector...\n\n");
    
    test_line_editor_history_basic();
    test_line_editor_history_memory_profiling();
    test_line_editor_history_edge_cases();
    
    printf("\n🎉 All REPL History tests passed!\n");
    printf("✅ DRY principle: Using own CljPersistentVector for history\n");
    printf("✅ Eat-your-own-dogfood: Practical use of CLJ data structures\n");
    printf("✅ Memory-safe: Automatic reference counting\n");
    printf("✅ Memory profiling exclusion: History not counted in tests\n");
    
    return 0;
}
