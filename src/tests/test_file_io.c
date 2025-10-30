/*
 * Unit Tests for File I/O Functions (slurp)
 * 
 * Test-First implementation for slurp function.
 */

#include "tests_common.h"
#include "../strings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Forward declarations
int load_clojure_core(EvalState *st);

// Helper function to create a temporary test file
static char* create_test_file(const char* content) {
    char template[] = "/tmp/tiny_clj_test_XXXXXX";
    int fd = mkstemp(template);
    if (fd == -1) return NULL;
    
    FILE* fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(template);
        return NULL;
    }
    
    if (content) {
        fputs(content, fp);
    }
    fclose(fp);
    
    char* path = malloc(strlen(template) + 1);
    strcpy(path, template);
    return path;
}

// Helper function to delete test file
static void cleanup_test_file(const char* path) {
    if (path) {
        unlink(path);
        free((void*)path);
    }
}

// ============================================================================
// SLURP TESTS
// ============================================================================

TEST(test_slurp_reads_file) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create test file with content
    char* test_file = create_test_file("Hello, World!\nThis is a test.");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test slurp with file path as string
    char expr[256];
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *result = eval_string(expr, st);
    
    // Verify result is a string
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_STRING));
    
    // Verify content
    CljString *str = as_clj_string(result);
    TEST_ASSERT_EQUAL_STRING("Hello, World!\nThis is a test.", clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

TEST(test_slurp_returns_string) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create test file
    char* test_file = create_test_file("Test content");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test slurp returns string type
    char expr[256];
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *result = eval_string(expr, st);
    
    // Verify return type
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, result->type);
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

TEST(test_slurp_empty_file) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create empty test file
    char* test_file = create_test_file("");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test slurp on empty file
    char expr[256];
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *result = eval_string(expr, st);
    
    // Verify result is empty string
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_STRING));
    
    CljString *str = as_clj_string(result);
    TEST_ASSERT_EQUAL_INT(0, string_length(str));
    TEST_ASSERT_EQUAL_STRING("", clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

TEST(test_slurp_nonexistent_file) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Test slurp with non-existent file
    // This should throw an exception or return nil
    // Note: Depending on implementation, this might throw exception
    // or return nil. For now, we test that it doesn't crash.
    // The actual behavior will be verified after implementation.
    (void)eval_string("(slurp \"/nonexistent/file/that/does/not/exist.txt\")", st);
    
    evalstate_free(st);
}

TEST(test_slurp_multiline_content) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create test file with multiline content
    const char* content = "Line 1\nLine 2\nLine 3\n";
    char* test_file = create_test_file(content);
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test slurp with multiline content
    char expr[256];
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *result = eval_string(expr, st);
    
    // Verify result contains all lines
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_STRING));
    
    CljString *str = as_clj_string(result);
    TEST_ASSERT_EQUAL_STRING(content, clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

// ============================================================================
// SPIT TESTS
// ============================================================================

TEST(test_spit_writes_file) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create temporary test file path
    char* test_file = create_test_file(NULL);  // Create empty file
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Write content to file using spit
    char expr[512];
    snprintf(expr, sizeof(expr), "(spit \"%s\" \"Hello from spit!\")", test_file);
    (void)eval_string(expr, st);  // spit returns nil (Clojure-compatible)
    
    // Read file back to verify content
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *read_result = eval_string(expr, st);
    
    TEST_ASSERT_NOT_NULL(read_result);
    TEST_ASSERT_TRUE(is_type(read_result, CLJ_STRING));
    
    CljString *str = as_clj_string(read_result);
    TEST_ASSERT_EQUAL_STRING("Hello from spit!", clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

TEST(test_spit_overwrites_file) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create test file with initial content
    char* test_file = create_test_file("Initial content");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Overwrite with new content
    char expr[512];
    snprintf(expr, sizeof(expr), "(spit \"%s\" \"New content\")", test_file);
    (void)eval_string(expr, st);
    
    // Read file back to verify it was overwritten
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *read_result = eval_string(expr, st);
    
    TEST_ASSERT_NOT_NULL(read_result);
    TEST_ASSERT_TRUE(is_type(read_result, CLJ_STRING));
    
    CljString *str = as_clj_string(read_result);
    TEST_ASSERT_EQUAL_STRING("New content", clj_string_data(str));
    // Verify old content was overwritten (content is "New content", not "Initial content")
    TEST_ASSERT_TRUE(strcmp(clj_string_data(str), "Initial content") != 0);
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

TEST(test_spit_multiline_content) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create temporary test file
    char* test_file = create_test_file(NULL);
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Write multiline content
    const char* content = "Line 1\nLine 2\nLine 3\n";
    char expr[512];
    snprintf(expr, sizeof(expr), "(spit \"%s\" \"%s\")", test_file, content);
    (void)eval_string(expr, st);
    
    // Read back and verify
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *read_result = eval_string(expr, st);
    
    TEST_ASSERT_NOT_NULL(read_result);
    TEST_ASSERT_TRUE(is_type(read_result, CLJ_STRING));
    
    CljString *str = as_clj_string(read_result);
    TEST_ASSERT_EQUAL_STRING(content, clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

TEST(test_spit_empty_string) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create temporary test file
    char* test_file = create_test_file("Some content");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Write empty string
    char expr[512];
    snprintf(expr, sizeof(expr), "(spit \"%s\" \"\")", test_file);
    (void)eval_string(expr, st);
    
    // Read back and verify it's empty
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *read_result = eval_string(expr, st);
    
    TEST_ASSERT_NOT_NULL(read_result);
    TEST_ASSERT_TRUE(is_type(read_result, CLJ_STRING));
    
    CljString *str = as_clj_string(read_result);
    TEST_ASSERT_EQUAL_INT(0, string_length(str));
    TEST_ASSERT_EQUAL_STRING("", clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

TEST(test_spit_slurp_roundtrip) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create temporary test file
    char* test_file = create_test_file(NULL);
    TEST_ASSERT_NOT_NULL(test_file);
    
    const char* original_content = "Roundtrip test content\nWith multiple lines";
    
    // Write with spit
    char expr[512];
    snprintf(expr, sizeof(expr), "(spit \"%s\" \"%s\")", test_file, original_content);
    (void)eval_string(expr, st);
    
    // Read back with slurp
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *read_result = eval_string(expr, st);
    
    TEST_ASSERT_NOT_NULL(read_result);
    TEST_ASSERT_TRUE(is_type(read_result, CLJ_STRING));
    
    CljString *str = as_clj_string(read_result);
    TEST_ASSERT_EQUAL_STRING(original_content, clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

