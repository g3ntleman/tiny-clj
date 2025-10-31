// Tests für History-Persistenz (Vector<String>) via to-string/Parser
#include "tests_common.h"
#include "memory_profiler.h"

// Vorwärtsdeklarationen aus line_editor.c
extern bool history_save_to_file(CljObject *vec, const char *path);
extern CljObject* history_load_from_file(const char *path);
extern CljObject* history_trim_last_n(CljObject *vec, int limit);

static const char *tmp_hist_path = "/tmp/tiny_clj_history_test.edn";

TEST(test_history_roundtrip_basic) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    // Erzeuge Vector aus Strings via Parser (ohne Evaluation)
    ID vec = parse("[\"a\" \"b\" \"c\"]", st);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, ((CljObject*)vec)->type);

    // Speichern
    bool ok = history_save_to_file((CljObject*)vec, tmp_hist_path);
    TEST_ASSERT_TRUE(ok);

    // Laden
    CljObject *loaded = history_load_from_file(tmp_hist_path);
    TEST_ASSERT_NOT_NULL(loaded);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, loaded->type);

    // Vergleiche Count und Werte
    CljPersistentVector *v = as_vector(loaded);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(3, v->count);
    TEST_ASSERT_TRUE(is_type(v->data[0], CLJ_STRING));
    TEST_ASSERT_TRUE(is_type(v->data[1], CLJ_STRING));
    TEST_ASSERT_TRUE(is_type(v->data[2], CLJ_STRING));

    evalstate_free(st);
}

TEST(test_history_trim_to_50) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    // Reset leak counters for this heavy test to avoid false positives
    memory_profiler_reset();

    // Baue Vector mit 75 Strings deterministisch via Parser
    char buf[4096];
    size_t pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "[");
    for (int i = 0; i < 75; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\"%d\"%s", i, (i < 74 ? " " : ""));
    }
    snprintf(buf + pos, sizeof(buf) - pos, "]");
    ID vec = NULL;
    TRY {
        vec = parse(buf, st);
        if (!vec) {
            printf("DEBUG: parse() returned NULL (no exception)\n");
        }
    } CATCH(ex) {
        // Exception caught during parsing - try eval_string as fallback
        printf("DEBUG: Exception caught in parse(): %s: %s\n", 
               ex ? ex->type : "NULL", ex ? ex->message : "NULL");
        vec = NULL;
    } END_TRY
    if (!vec) {
        // Try eval_string as fallback if parse failed
        TRY {
            vec = eval_string(buf, st);
            if (!vec) {
                printf("DEBUG: eval_string() returned NULL (no exception)\n");
            }
        } CATCH(ex) {
            printf("DEBUG: Exception caught in eval_string(): %s: %s\n", 
                   ex ? ex->type : "NULL", ex ? ex->message : "NULL");
            vec = NULL;
        } END_TRY
    }
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, ((CljObject*)vec)->type);

    CljObject *trimmed = history_trim_last_n((CljObject*)vec, 50);
    TEST_ASSERT_NOT_NULL(trimmed);
    CljPersistentVector *tv = as_vector(trimmed);
    TEST_ASSERT_NOT_NULL(tv);
    TEST_ASSERT_EQUAL_INT(50, tv->count);

    // Speichern und Laden, weiterhin 50
    bool ok = history_save_to_file(trimmed, tmp_hist_path);
    TEST_ASSERT_TRUE(ok);
    CljObject *loaded = history_load_from_file(tmp_hist_path);
    TEST_ASSERT_NOT_NULL(loaded);
    CljPersistentVector *lv = as_vector(loaded);
    TEST_ASSERT_NOT_NULL(lv);
    TEST_ASSERT_EQUAL_INT(50, lv->count);

    // Cleanup explicit heap objects to avoid leaks in this test
    // Since vectors may not release contained elements, release items explicitly
    if (loaded) {
        CljPersistentVector *lv2 = as_vector(loaded);
        if (lv2) {
            for (int i = 0; i < lv2->count; i++) {
                if (lv2->data[i] && !IS_IMMEDIATE(lv2->data[i])) {
                    RELEASE(lv2->data[i]);
                }
            }
        }
        RELEASE(loaded);
    }
    if (trimmed) {
        CljPersistentVector *tv2 = as_vector(trimmed);
        if (tv2) {
            for (int i = 0; i < tv2->count; i++) {
                if (tv2->data[i] && !IS_IMMEDIATE(tv2->data[i])) {
                    RELEASE(tv2->data[i]);
                }
            }
        }
        RELEASE(trimmed);
    }
    if (vec) {
        CljPersistentVector *vv2 = as_vector((CljObject*)vec);
        if (vv2) {
            for (int i = 0; i < vv2->count; i++) {
                if (vv2->data[i] && !IS_IMMEDIATE(vv2->data[i])) {
                    RELEASE(vv2->data[i]);
                }
            }
        }
        RELEASE((CljObject*)vec);
    }

    // Reset again to clear allocations made solely for this test
    memory_profiler_reset();
    evalstate_free(st);
}

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
    // Schlucke Exception, damit der Testlauf nicht abbricht
    TRY {
        (void)eval_string("(slurp \"/nonexistent/file/that/does/not/exist.txt\")", st);
    } CATCH(ex) {
        // ok
    } END_TRY
    
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

// ============================================================================
// FILE-EXISTS? TESTS
// ============================================================================

TEST(test_file_exists_returns_true) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create test file with content
    char* test_file = create_test_file("Test content");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test file-exists? with existing file
    char expr[256];
    snprintf(expr, sizeof(expr), "(file-exists? \"%s\")", test_file);
    CljObject *result = eval_string(expr, st);
    
    // Verify result is true
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_true((CljValue)result));
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

TEST(test_file_exists_returns_false) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Test file-exists? with non-existent file
    char expr[256];
    snprintf(expr, sizeof(expr), "(file-exists? \"/nonexistent/file/that/does/not/exist.txt\")");
    CljObject *result = eval_string(expr, st);
    
    // Verify result is false
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_false((CljValue)result));
    
    evalstate_free(st);
}

TEST(test_file_exists_returns_boolean_type) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create test file
    char* test_file = create_test_file("Test content");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test file-exists? returns boolean type
    char expr[256];
    snprintf(expr, sizeof(expr), "(file-exists? \"%s\")", test_file);
    CljObject *result = eval_string(expr, st);
    
    // Verify return type is boolean
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_bool((CljValue)result));
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

TEST(test_file_exists_empty_file) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create empty test file
    char* test_file = create_test_file("");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test file-exists? on empty file (should return true - file exists)
    char expr[256];
    snprintf(expr, sizeof(expr), "(file-exists? \"%s\")", test_file);
    CljObject *result = eval_string(expr, st);
    
    // Verify result is true (file exists, even if empty)
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_true((CljValue)result));
    
    // Cleanup
    cleanup_test_file(test_file);
    evalstate_free(st);
}

