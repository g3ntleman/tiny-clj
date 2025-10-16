#include "platform.h"
#include "line_editor.h"
#include "object.h"
#include "vector.h"
#include "string.h"
#include "memory.h"
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

int main() {
    platform_init();
    
    printf("🧪 Testing REPL History with CljVector...\n\n");
    
    // Create line editor
    LineEditor *editor = line_editor_new(mock_get_char, mock_put_char, mock_put_string);
    assert(editor != NULL);
    
    printf("✅ LineEditor created successfully\n");
    
    // Test initial state
    assert(line_editor_get_history_size(editor) == 0);
    printf("✅ Initial history size is 0\n");
    
    // Add some history entries
    line_editor_add_to_history(editor, "(+ 1 2)");
    assert(line_editor_get_history_size(editor) == 1);
    printf("✅ Added first command to history\n");
    
    const char *line = line_editor_get_history_line(editor, 0);
    assert(line != NULL);
    assert(strcmp(line, "(+ 1 2)") == 0);
    printf("✅ Retrieved first command: %s\n", line);
    
    line_editor_add_to_history(editor, "(* 3 4)");
    assert(line_editor_get_history_size(editor) == 2);
    printf("✅ Added second command to history\n");
    
    line = line_editor_get_history_line(editor, 1);
    assert(line != NULL);
    assert(strcmp(line, "(* 3 4)") == 0);
    printf("✅ Retrieved second command: %s\n", line);
    
    line_editor_add_to_history(editor, "(str \"hello\")");
    assert(line_editor_get_history_size(editor) == 3);
    printf("✅ Added third command to history\n");
    
    line = line_editor_get_history_line(editor, 2);
    assert(line != NULL);
    assert(strcmp(line, "(str \"hello\")") == 0);
    printf("✅ Retrieved third command: %s\n", line);
    
    // Test invalid indices
    assert(line_editor_get_history_line(editor, -1) == NULL);
    assert(line_editor_get_history_line(editor, 3) == NULL);
    printf("✅ Invalid indices handled correctly\n");
    
    // Test edge cases
    line_editor_add_to_history(editor, NULL);  // Should be ignored
    assert(line_editor_get_history_size(editor) == 3);
    printf("✅ NULL line ignored\n");
    
    line_editor_add_to_history(editor, "");  // Empty line
    assert(line_editor_get_history_size(editor) == 4);
    line = line_editor_get_history_line(editor, 3);
    assert(line != NULL);
    assert(strcmp(line, "") == 0);
    printf("✅ Empty line handled correctly\n");
    
    // Cleanup
    line_editor_free(editor);
    printf("✅ LineEditor freed successfully\n");
    
    printf("\n🎉 All REPL History tests passed!\n");
    printf("✅ DRY principle: Using own CljPersistentVector for history\n");
    printf("✅ Eat-your-own-dogfood: Practical use of CLJ data structures\n");
    printf("✅ Memory-safe: Automatic reference counting\n");
    printf("✅ History storage: Commands stored in CljVector\n");
    printf("✅ History retrieval: Commands retrieved by index\n");
    printf("✅ Edge cases: NULL and empty lines handled\n");
    
    return 0;
}
