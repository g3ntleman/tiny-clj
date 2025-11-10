// Tests für History-Persistenz (Vector<String>) via to-string/Parser
#include "tests_common.h"

// Vorwärtsdeklarationen aus repl.c
extern bool history_save_to_file(CljObject *vec, const char *path);
extern CljObject* history_load_from_file(const char *path);
extern CljObject* history_trim_last_n(CljObject *vec, int limit);

static const char *tmp_hist_path = "/tmp/tiny_clj_history_test.edn";

TEST(test_history_roundtrip_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Erzeuge Vector aus Strings
    CljObject *vec = eval_string("[\"a\" \"b\" \"c\"]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, vec->type);

    // Speichern
    bool ok = history_save_to_file(vec, tmp_hist_path);
    TEST_ASSERT_TRUE(ok);

    // Laden
    CljObject *loaded = history_load_from_file(tmp_hist_path);
    TEST_ASSERT_NOT_NULL(loaded);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, loaded->type);

    // Vergleiche Count und Werte
    CljObject *c = eval_string("(count [\"a\" \"b\" \"c\"])", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c));
    CljPersistentVector *v = as_vector(loaded);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(as_fixnum((CljValue)c), v->count);
    TEST_ASSERT_TRUE(v->data[0] && TAG(v->data[0]) == CLJ_STRING);
    TEST_ASSERT_TRUE(v->data[1] && TAG(v->data[1]) == CLJ_STRING);
    TEST_ASSERT_TRUE(v->data[2] && TAG(v->data[2]) == CLJ_STRING);

}

TEST(test_history_trim_to_50) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Baue Vector mit 75 Strings über String-Parsing
    // Der Parser unterstützt jetzt Vektoren mit mehr als 64 Elementen
    CljObject *vec = eval_string("[\"0\" \"1\" \"2\" \"3\" \"4\" \"5\" \"6\" \"7\" \"8\" \"9\" \"10\" \"11\" \"12\" \"13\" \"14\" \"15\" \"16\" \"17\" \"18\" \"19\" \"20\" \"21\" \"22\" \"23\" \"24\" \"25\" \"26\" \"27\" \"28\" \"29\" \"30\" \"31\" \"32\" \"33\" \"34\" \"35\" \"36\" \"37\" \"38\" \"39\" \"40\" \"41\" \"42\" \"43\" \"44\" \"45\" \"46\" \"47\" \"48\" \"49\" \"50\" \"51\" \"52\" \"53\" \"54\" \"55\" \"56\" \"57\" \"58\" \"59\" \"60\" \"61\" \"62\" \"63\" \"64\" \"65\" \"66\" \"67\" \"68\" \"69\" \"70\" \"71\" \"72\" \"73\" \"74\"]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, vec->type);
    CljPersistentVector *v = as_vector(vec);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(75, v->count);

    CljObject *trimmed = history_trim_last_n(vec, 50);
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

    RELEASE(trimmed);
    RELEASE(loaded);
}

TEST(test_history_load_current_format) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create test file with exact current history format
    const char *history_content = "[\"(list 1 1.0 \\\"1\\\" \\\"one\\\")\"]\n";
    FILE *fp = fopen(tmp_hist_path, "w");
    TEST_ASSERT_NOT_NULL(fp);
    size_t n = fwrite(history_content, 1, strlen(history_content), fp);
    fclose(fp);
    TEST_ASSERT_TRUE(n > 0);
    
    // Load history from file
    CljObject *loaded = history_load_from_file(tmp_hist_path);
    TEST_ASSERT_NOT_NULL(loaded);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, loaded->type);
    
    // Verify vector structure
    CljPersistentVector *v = as_vector(loaded);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(1, v->count);
    
    // Verify first element is a string
    TEST_ASSERT_NOT_NULL(v->data[0]);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(v->data[0]));
    
    // Verify string content using to_string
    const char *str_content = to_string(v->data[0]);
    TEST_ASSERT_NOT_NULL(str_content);
    TEST_ASSERT_EQUAL_STRING("(list 1 1.0 \"1\" \"one\")", str_content);
    free((void*)str_content);
    
    // Cleanup
    unlink(tmp_hist_path);
}

TEST(test_pr_str_escapes_quotes) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a string with quotes inside
    const char *test_input = "(list 1 1.0 \"1\" \"one\")";
    CljObject *str = (CljObject*)make_string(test_input);
    TEST_ASSERT_NOT_NULL(str);
    
    // Test pr_str on the string
    const char *result = pr_str(str);
    TEST_ASSERT_NOT_NULL(result);
    
    // Verify that quotes are escaped
    // Expected: "(list 1 1.0 \"1\" \"one\")"
    // The quotes inside should be escaped as \"
    TEST_ASSERT_EQUAL_STRING("\"(list 1 1.0 \\\"1\\\" \\\"one\\\")\"", result);
    
    // Verify that the result contains escaped quotes
    const char *escaped_quote = strstr(result, "\\\"");
    TEST_ASSERT_NOT_NULL(escaped_quote);
    
    free((void*)result);
    RELEASE(str);
}

TEST(test_history_save_escapes_quotes) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a vector with a string containing quotes
    const char *test_input = "(list 1 1.0 \"1\" \"one\")";
    CljObject *str = (CljObject*)make_string(test_input);
    TEST_ASSERT_NOT_NULL(str);
    
    CljObject *vec = (CljObject*)make_vector(1, 0);
    CljPersistentVector *v = as_vector(vec);
    v->data[0] = (ID)str;
    v->count = 1;
    
    // Save to file
    bool ok = history_save_to_file(vec, tmp_hist_path);
    TEST_ASSERT_TRUE(ok);
    
    // Read file content
    FILE *fp = fopen(tmp_hist_path, "r");
    TEST_ASSERT_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = (char*)malloc((size_t)sz + 1);
    TEST_ASSERT_NOT_NULL(buf);
    size_t n = fread(buf, 1, (size_t)sz, fp);
    buf[n] = '\0';
    fclose(fp);
    
    // Verify that quotes are escaped in the file
    // Expected: ["(list 1 1.0 \"1\" \"one\")"]
    // The quotes inside should be escaped as \"
    const char *escaped_quote = strstr(buf, "\\\"");
    TEST_ASSERT_NOT_NULL(escaped_quote);
    
    // Verify the exact format
    TEST_ASSERT_EQUAL_STRING("[\"(list 1 1.0 \\\"1\\\" \\\"one\\\")\"]\n", buf);
    
    free(buf);
    RELEASE(str);
    RELEASE(vec);
    unlink(tmp_hist_path);
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
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create test file with content
    char* test_file = create_test_file("Hello, World!\nThis is a test.");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test slurp with file path as string
    char expr[256];
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *result = eval_string(expr, g_test_eval_state);
    
    // Verify result is a string
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_STRING);
    
    // Verify content
    CljString *str = as_clj_string(result);
    TEST_ASSERT_EQUAL_STRING("Hello, World!\nThis is a test.", clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
}

TEST(test_slurp_returns_string) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create test file
    char* test_file = create_test_file("Test content");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test slurp returns string type
    char expr[256];
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *result = eval_string(expr, g_test_eval_state);
    
    // Verify return type
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, result->type);
    
    // Cleanup
    cleanup_test_file(test_file);
}

TEST(test_slurp_empty_file) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create empty test file
    char* test_file = create_test_file("");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test slurp on empty file
    char expr[256];
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *result = eval_string(expr, g_test_eval_state);
    
    // Verify result is empty string
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_STRING);
    
    CljString *str = as_clj_string(result);
    TEST_ASSERT_EQUAL_INT(0, string_length(str));
    TEST_ASSERT_EQUAL_STRING("", clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
}

TEST(test_slurp_nonexistent_file) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test slurp with non-existent file
    // This should throw an exception or return nil
    // Note: Depending on implementation, this might throw exception
    // or return nil. For now, we test that it doesn't crash.
    // The actual behavior will be verified after implementation.
    (void)eval_string("(slurp \"/nonexistent/file/that/does/not/exist.txt\")", g_test_eval_state);
    
}

TEST(test_slurp_multiline_content) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create test file with multiline content
    const char* content = "Line 1\nLine 2\nLine 3\n";
    char* test_file = create_test_file(content);
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Test slurp with multiline content
    char expr[256];
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *result = eval_string(expr, g_test_eval_state);
    
    // Verify result contains all lines
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_STRING);
    
    CljString *str = as_clj_string(result);
    TEST_ASSERT_EQUAL_STRING(content, clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
}

// ============================================================================
// SPIT TESTS
// ============================================================================

TEST(test_spit_writes_file) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create temporary test file path
    char* test_file = create_test_file(NULL);  // Create empty file
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Write content to file using spit
    char expr[512];
    snprintf(expr, sizeof(expr), "(spit \"%s\" \"Hello from spit!\")", test_file);
    (void)eval_string(expr, g_test_eval_state);  // spit returns nil (Clojure-compatible)
    
    // Read file back to verify content
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *read_result = eval_string(expr, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(read_result);
    TEST_ASSERT_TRUE(read_result && TAG(read_result) == CLJ_STRING);
    
    CljString *str = as_clj_string(read_result);
    TEST_ASSERT_EQUAL_STRING("Hello from spit!", clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
}

TEST(test_spit_overwrites_file) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create test file with initial content
    char* test_file = create_test_file("Initial content");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Overwrite with new content
    char expr[512];
    snprintf(expr, sizeof(expr), "(spit \"%s\" \"New content\")", test_file);
    (void)eval_string(expr, g_test_eval_state);
    
    // Read file back to verify it was overwritten
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *read_result = eval_string(expr, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(read_result);
    TEST_ASSERT_TRUE(read_result && TAG(read_result) == CLJ_STRING);
    
    CljString *str = as_clj_string(read_result);
    TEST_ASSERT_EQUAL_STRING("New content", clj_string_data(str));
    // Verify old content was overwritten (content is "New content", not "Initial content")
    TEST_ASSERT_TRUE(strcmp(clj_string_data(str), "Initial content") != 0);
    
    // Cleanup
    cleanup_test_file(test_file);
}

TEST(test_spit_multiline_content) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create temporary test file
    char* test_file = create_test_file(NULL);
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Write multiline content
    const char* content = "Line 1\nLine 2\nLine 3\n";
    char expr[512];
    snprintf(expr, sizeof(expr), "(spit \"%s\" \"%s\")", test_file, content);
    (void)eval_string(expr, g_test_eval_state);
    
    // Read back and verify
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *read_result = eval_string(expr, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(read_result);
    TEST_ASSERT_TRUE(read_result && TAG(read_result) == CLJ_STRING);
    
    CljString *str = as_clj_string(read_result);
    TEST_ASSERT_EQUAL_STRING(content, clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
}

TEST(test_spit_empty_string) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create temporary test file
    char* test_file = create_test_file("Some content");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Write empty string
    char expr[512];
    snprintf(expr, sizeof(expr), "(spit \"%s\" \"\")", test_file);
    (void)eval_string(expr, g_test_eval_state);
    
    // Read back and verify it's empty
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *read_result = eval_string(expr, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(read_result);
    TEST_ASSERT_TRUE(read_result && TAG(read_result) == CLJ_STRING);
    
    CljString *str = as_clj_string(read_result);
    TEST_ASSERT_EQUAL_INT(0, string_length(str));
    TEST_ASSERT_EQUAL_STRING("", clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
}

TEST(test_spit_slurp_roundtrip) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create temporary test file
    char* test_file = create_test_file(NULL);
    TEST_ASSERT_NOT_NULL(test_file);
    
    const char* original_content = "Roundtrip test content\nWith multiple lines";
    
    // Write with spit
    char expr[512];
    snprintf(expr, sizeof(expr), "(spit \"%s\" \"%s\")", test_file, original_content);
    (void)eval_string(expr, g_test_eval_state);
    
    // Read back with slurp
    snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
    CljObject *read_result = eval_string(expr, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(read_result);
    TEST_ASSERT_TRUE(read_result && TAG(read_result) == CLJ_STRING);
    
    CljString *str = as_clj_string(read_result);
    TEST_ASSERT_EQUAL_STRING(original_content, clj_string_data(str));
    
    // Cleanup
    cleanup_test_file(test_file);
}

