// Tests für History-Persistenz (Vector<String>) via to-string/Parser
#include "tests_common.h"
#include "../vector.h"

// Vorwärtsdeklarationen aus repl.c
extern bool history_save_to_file(CljVector *vec, const char *path);
extern CljVector *history_load_from_file(const char *path);
extern CljObject *history_trim_last_n(CljObject *vec, int limit);

static const char *tmp_hist_path = "/tmp/tiny_clj_history_test.edn";

TEST(test_history_roundtrip_basic) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Erzeuge Vector aus Strings
  CljObject *vec = eval_string("[\"a\" \"b\" \"c\"]", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(vec);
  TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, vec->type);

  // Speichern
  bool ok = history_save_to_file((CljVector*)vec, tmp_hist_path);
  TEST_ASSERT_TRUE(ok);

  // Laden
  CljVector *loaded = history_load_from_file(tmp_hist_path);
  TEST_ASSERT_NOT_NULL(loaded);
  TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, ((CljObject*)loaded)->type);

  // Vergleiche Count und Werte
  CljObject *c = eval_string("(count [\"a\" \"b\" \"c\"])", g_test_eval_state);
  TEST_ASSERT_TRUE(is_fixnum((CljValue)c));
  CljVector *v = as_vector(loaded);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_INT(as_fixnum((CljValue)c), vector_count(v));
  ID elem0 = vector_nth(v, 0);
  ID elem1 = vector_nth(v, 1);
  ID elem2 = vector_nth(v, 2);
  TEST_ASSERT_TRUE(elem0 && TAG(elem0) == CLJ_STRING);
  TEST_ASSERT_TRUE(elem1 && TAG(elem1) == CLJ_STRING);
  TEST_ASSERT_TRUE(elem2 && TAG(elem2) == CLJ_STRING);
  RELEASE(elem0);
  RELEASE(elem1);
  RELEASE(elem2);
}

TEST(test_history_trim_to_50) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Baue Vector mit 75 Strings über String-Parsing
  // Der Parser unterstützt jetzt Vektoren mit mehr als 64 Elementen
  CljObject *vec = eval_string(
      "[\"0\" \"1\" \"2\" \"3\" \"4\" \"5\" \"6\" \"7\" \"8\" \"9\" \"10\" "
      "\"11\" \"12\" \"13\" \"14\" \"15\" \"16\" \"17\" \"18\" \"19\" \"20\" "
      "\"21\" \"22\" \"23\" \"24\" \"25\" \"26\" \"27\" \"28\" \"29\" \"30\" "
      "\"31\" \"32\" \"33\" \"34\" \"35\" \"36\" \"37\" \"38\" \"39\" \"40\" "
      "\"41\" \"42\" \"43\" \"44\" \"45\" \"46\" \"47\" \"48\" \"49\" \"50\" "
      "\"51\" \"52\" \"53\" \"54\" \"55\" \"56\" \"57\" \"58\" \"59\" \"60\" "
      "\"61\" \"62\" \"63\" \"64\" \"65\" \"66\" \"67\" \"68\" \"69\" \"70\" "
      "\"71\" \"72\" \"73\" \"74\"]",
      g_test_eval_state);
  TEST_ASSERT_NOT_NULL(vec);
  TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, vec->type);
  CljVector *v = as_vector(vec);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_INT(75, vector_count(v));

  // RETAIN vec to keep it alive while trimmed references its elements
  RETAIN(vec);
  CljObject *trimmed = history_trim_last_n(vec, 50);
  TEST_ASSERT_NOT_NULL(trimmed);
  // RETAIN trimmed to keep it alive outside of autorelease pool
  RETAIN(trimmed);
  CljVector *tv = as_vector(trimmed);
  TEST_ASSERT_NOT_NULL(tv);
  TEST_ASSERT_EQUAL_INT(50, vector_count(tv));

  // Speichern und Laden, weiterhin 50
  bool ok = history_save_to_file((CljVector*)trimmed, tmp_hist_path);
  TEST_ASSERT_TRUE(ok);
  CljVector *loaded = history_load_from_file(tmp_hist_path);
  TEST_ASSERT_NOT_NULL(loaded);
  // RETAIN loaded to keep it alive outside of autorelease pool
  RETAIN(loaded);
  CljVector *lv = as_vector(loaded);
  TEST_ASSERT_NOT_NULL(lv);
  TEST_ASSERT_EQUAL_INT(50, vector_count(lv));

  RELEASE(trimmed);
  RELEASE(loaded);
  RELEASE(vec);
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
  CljVector *loaded = history_load_from_file(tmp_hist_path);
  TEST_ASSERT_NOT_NULL(loaded);
  // RETAIN loaded to keep it alive outside of autorelease pool
  RETAIN((CljObject*)loaded);
  TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, ((CljObject*)loaded)->type);

  // Verify vector structure
  CljVector *v = as_vector(loaded);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_INT(1, vector_count(v));

  // Verify first element is a string
  ID elem0 = vector_nth(v, 0);
  TEST_ASSERT_NOT_NULL(elem0);
  TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(elem0));

  // Verify string content using to_string
  const char *str_content = to_string((CljObject*)elem0);
  RELEASE(elem0);
  TEST_ASSERT_NOT_NULL(str_content);
  TEST_ASSERT_EQUAL_STRING("(list 1 1.0 \"1\" \"one\")", str_content);
  free((void *)str_content);

  // Cleanup
  RELEASE(loaded);
  unlink(tmp_hist_path);
}

// Test that reproduces the crash in history_load_from_file
// This test is written without the TEST macro to avoid the automatic
// WITH_AUTORELEASE_POOL The crash happens when the pool in
// history_load_from_file is popped, but the strings are still referenced by the
// vector
static void test_history_load_from_file_crash_reproduction_body(void) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create test file with history format (vector of strings)
  const char *history_content = "[\"test1\" \"test2\" \"test3\"]\n";
  FILE *fp = fopen(tmp_hist_path, "w");
  TEST_ASSERT_NOT_NULL(fp);
  size_t n = fwrite(history_content, 1, strlen(history_content), fp);
  fclose(fp);
  TEST_ASSERT_TRUE(n > 0);

  // Call history_load_from_file - this should crash with ASAN
  // The problem: strings from slurp are in autorelease pool inside
  // history_load_from_file, but they're being retained by the new vector. When
  // the pool is popped at the end of history_load_from_file, the strings are
  // released, but they're still referenced by the vector. The vector is
  // returned with AUTORELEASE, but the strings inside are already freed.
  CljVector *loaded = history_load_from_file(tmp_hist_path);

  // If we get here, the crash didn't happen (or was caught)
  // But the memory management is still wrong
  if (loaded) {
    RETAIN((CljObject*)loaded);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, ((CljObject*)loaded)->type);

    CljVector *v = as_vector(loaded);
    TEST_ASSERT_NOT_NULL(v);
    int count = vector_count(v);
    TEST_ASSERT_EQUAL_INT(3, count);

    // Try to access the strings - this might crash if they were freed
    // The strings should be dangling pointers at this point
    for (int i = 0; i < count; i++) {
      // This might crash if the strings were freed
      ID elem = vector_nth(v, i);
      TEST_ASSERT_NOT_NULL(elem);
      TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(elem));
      RELEASE(elem);
    }

    RELEASE(loaded);
  }

  unlink(tmp_hist_path);
}

void test_history_load_from_file_crash_reproduction(void) {
  // Enable verbose memory debugging
  set_memory_verbose_mode(true);
  enable_memory_debug_output();

  // Use WITH_AUTORELEASE_POOL to match the REPL scenario
  // The crash happens when history_load_from_file's pool is popped,
  // but the strings are still referenced by the vector
  WITH_AUTORELEASE_POOL(
      { test_history_load_from_file_crash_reproduction_body(); });

  // Disable verbose mode after test
  set_memory_verbose_mode(false);
}

static void register_test_history_load_from_file_crash_reproduction(void)
    __attribute__((constructor));
static void register_test_history_load_from_file_crash_reproduction(void) {
  char *filename = test_extract_filename_from_path(__FILE__);
  if (filename) {
    test_registry_add_with_group("test_history_load_from_file_crash_reproduction",
                                 test_history_load_from_file_crash_reproduction,
                                 filename);
    free(filename);
  }
}

TEST(test_pr_str_escapes_quotes) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create a string with quotes inside
  const char *test_input = "(list 1 1.0 \"1\" \"one\")";
  CljObject *str = (CljObject *)make_string(test_input);
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

  free((void *)result);
  RELEASE(str);
}

TEST(test_history_save_escapes_quotes) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create a vector with a string containing quotes
  const char *test_input = "(list 1 1.0 \"1\" \"one\")";
  CljObject *str = (CljObject *)make_string(test_input);
  TEST_ASSERT_NOT_NULL(str);

  CljVector *vec = make_vector(1, CLJ_VECTOR);
  vec = vector_conj(vec, (ID)str);

  // Save to file
  bool ok = history_save_to_file(vec, tmp_hist_path);
  TEST_ASSERT_TRUE(ok);

  // Read file content
  FILE *fp = fopen(tmp_hist_path, "r");
  TEST_ASSERT_NOT_NULL(fp);
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  char *buf = (char *)malloc((size_t)sz + 1);
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
  // Don't release str separately - it's owned by vec and will be released with vec
  RELEASE((CljObject*)vec);
  unlink(tmp_hist_path);
}

/*
 * Unit Tests for File I/O Functions (slurp)
 *
 * Test-First implementation for slurp function.
 */

#include "../strings.h"
#include "../file_utils.h"
#include "tests_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Forward declarations
int load_clojure_core(EvalState *st);

// Helper function to create a temporary test file
static char *create_test_file(const char *content) {
  char template[] = "/tmp/tiny_clj_test_XXXXXX";
  int fd = mkstemp(template);
  if (fd == -1)
    return NULL;

  FILE *fp = fdopen(fd, "w");
  if (!fp) {
    close(fd);
    unlink(template);
    return NULL;
  }

  if (content) {
    fputs(content, fp);
  }
  fclose(fp);

  char *path = malloc(strlen(template) + 1);
  strcpy(path, template);
  return path;
}

// Helper function to delete test file
static void cleanup_test_file(const char *path) {
  if (path) {
    unlink(path);
    free((void *)path);
  }
}

// ============================================================================
// SLURP TESTS
// ============================================================================

TEST(test_slurp_reads_file) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create test file with content
  char *test_file = create_test_file("Hello, World!\nThis is a test.");
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
  TEST_ASSERT_EQUAL_STRING("Hello, World!\nThis is a test.",
                           clj_string_data(str));

  // Cleanup
  cleanup_test_file(test_file);
}

TEST(test_slurp_returns_string) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create test file
  char *test_file = create_test_file("Test content");
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
  char *test_file = create_test_file("");
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
  // This should throw an exception
  TRY {
    (void)eval_string("(slurp \"/nonexistent/file/that/does/not/exist.txt\")",
                        g_test_eval_state);
    // Should not reach here - exception should be thrown
    TEST_FAIL_MESSAGE("slurp should throw exception for nonexistent file");
  } CATCH(ex) {
    // Expected: exception should be thrown
    TEST_PASS();
    return;
  } END_TRY
}

TEST(test_slurp_multiline_content) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create test file with multiline content
  const char *content = "Line 1\nLine 2\nLine 3\n";
  char *test_file = create_test_file(content);
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
  char *test_file = create_test_file(NULL); // Create empty file
  TEST_ASSERT_NOT_NULL(test_file);

  // Write content to file using spit
  char expr[512];
  snprintf(expr, sizeof(expr), "(spit \"%s\" \"Hello from spit!\")", test_file);
  (void)eval_string(expr,
                    g_test_eval_state); // spit returns nil (Clojure-compatible)

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
  char *test_file = create_test_file("Initial content");
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
  // Verify old content was overwritten (content is "New content", not "Initial
  // content")
  TEST_ASSERT_TRUE(strcmp(clj_string_data(str), "Initial content") != 0);

  // Cleanup
  cleanup_test_file(test_file);
}

TEST(test_spit_multiline_content) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create temporary test file
  char *test_file = create_test_file(NULL);
  TEST_ASSERT_NOT_NULL(test_file);

  // Write multiline content
  const char *content = "Line 1\nLine 2\nLine 3\n";
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
  char *test_file = create_test_file("Some content");
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
  char *test_file = create_test_file(NULL);
  TEST_ASSERT_NOT_NULL(test_file);

  const char *original_content = "Roundtrip test content\nWith multiple lines";

  // Write with spit
  char expr[512];
  snprintf(expr, sizeof(expr), "(spit \"%s\" \"%s\")", test_file,
           original_content);
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

// Test to verify the hypothesis: strings from value_by_parsing_expr are in
// inner pool and get freed when inner pool is popped, even if they're retained
// by vector_conj
TEST(test_parse_vector_strings_inner_pool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Enable verbose memory debugging
  set_memory_verbose_mode(true);
  enable_memory_debug_output();

  // Test: parse a vector with strings inside eval_string (which creates inner
  // pool)
  WITH_AUTORELEASE_POOL({
    // This simulates what happens in history_load_from_file
    // eval_string creates an inner pool, parse_vector adds strings to that pool
    // When inner pool is popped, strings are freed even if retained by vector
    char expr[256];
    snprintf(expr, sizeof(expr), "[\"test1\" \"test2\" \"test3\"]");

    // eval_string creates inner pool
    CljObject *result = eval_string(expr, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_VECTOR);

    CljVector *v = as_vector(result);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(3, vector_count(v));

    // Verify strings are valid (should still be valid after inner pool is popped)
    const char *expected[3];
    expected[0] = "test1";
    expected[1] = "test2";
    expected[2] = "test3";
    ID nth_args[2];
    nth_args[0] = result;
    for (int i = 0; i < 3; i++) {
        nth_args[1] = fixnum(i);
        ID elem = nth2(nth_args, 2);
        TEST_ASSERT_TRUE(TAG(elem) == CLJ_STRING);
        ID expected_str = make_string(expected[i]);
        TEST_ASSERT_TRUE(clj_equal(elem, expected_str));
        RELEASE(elem);
        RELEASE(expected_str);
    }
  });
}

// Test to verify the actual history_load_from_file scenario
// This simulates exactly what happens in history_load_from_file:
// 1. eval_string creates inner pool
// 2. value_by_parsing_expr adds objects to inner pool
// 3. When inner pool is popped, objects are freed even if retained by vector
TEST(test_history_load_from_file_scenario) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create temporary history file with test content
  const char *test_content = "[\"test1\" \"test2\" \"test3\"]\n";
  FILE *fp = fopen(tmp_hist_path, "w");
  TEST_ASSERT_NOT_NULL(fp);
  fprintf(fp, "%s", test_content);
  fclose(fp);

  // Enable verbose memory debugging
  set_memory_verbose_mode(true);
  enable_memory_debug_output();

  // Simulate exactly what history_load_from_file does:
  // 1. Call eval_string (creates inner pool)
  // 2. Parse the result (adds objects to inner pool)
  // 3. Create new vector with parsed strings
  // 4. When inner pool is popped, strings should still be valid
    WITH_AUTORELEASE_POOL({
        TRY {
            char expr[512];
            snprintf(expr, sizeof(expr), "(slurp \"%s\")", tmp_hist_path);
            ID slurp_result = eval_string(expr, g_test_eval_state);
            
            if (!slurp_result || TAG(slurp_result) != CLJ_STRING) {
    TEST_FAIL_MESSAGE("slurp should return a string");
            } else {
    // Copy string buffer to avoid dependency on inner pool
    CljString *content = as_clj_string((CljObject *)slurp_result);
    const char *buf = clj_string_data(content);
    size_t buf_len = strlen(buf);
    char *buf_copy = (char *)malloc(buf_len + 1);
    TEST_ASSERT_NOT_NULL(buf_copy);
    memcpy(buf_copy, buf, buf_len + 1);

    // Parse in our own pool to avoid dependency on inner pool
    Reader rd;
    reader_init(&rd, buf_copy);
    ID form = value_by_parsing_expr(&rd, g_test_eval_state);

    // RETAIN form to keep it alive after pool is popped
    if (form && !IS_IMMEDIATE(form)) {
      RETAIN((CljObject *)form);
    }

    free(buf_copy);

    // Process form after pool is popped (simulate what happens after
    // WITH_AUTORELEASE_POOL)
    if (form && !IS_IMMEDIATE(form) && TAG(form) == CLJ_VECTOR) {
      CljVector *v = as_vector((CljObject *)form);
      int count = vector_count(v);
      bool all_strings = count > 0;
      for (int i = 0; i < count && all_strings; i++) {
        ID elem = vector_nth(v, i);
        if (elem && TAG(elem) != CLJ_STRING) {
          all_strings = false;
        }
        RELEASE(elem);
      }
      if (all_strings) {
        CljVector* new_vec = make_vector(count, CLJ_VECTOR);
        for (int i = 0; i < count; i++) {
          ID elem = vector_nth(v, i);
          new_vec = vector_conj(new_vec, elem);
          RELEASE(elem);
        }

                        // Verify strings are valid using nth2 (Clojure-compatible API)
                        const char *expected[3];
                        expected[0] = "test1";
                        expected[1] = "test2";
                        expected[2] = "test3";
                        ID nth_args2[2];
                        nth_args2[0] = new_vec;
                        for (int i = 0; i < 3; i++) {
                            nth_args2[1] = fixnum(i);
                            ID elem = nth2(nth_args2, 2);
                            TEST_ASSERT_TRUE(TAG(elem) == CLJ_STRING);
                            ID expected_str = make_string(expected[i]);
                            TEST_ASSERT_TRUE(clj_equal(elem, expected_str));
                            RELEASE(elem);
                            RELEASE(expected_str);
                        }

        RELEASE(new_vec);
      }

      // RELEASE form after we've copied its contents to the new vector
      RELEASE((CljObject *)form);
    } else if (form && !IS_IMMEDIATE(form)) {
      // RELEASE form if it's not a vector
      RELEASE((CljObject *)form);
    }
            }
        } CATCH(ex) {
            TEST_FAIL_MESSAGE("Exception during test");
        } END_TRY
    });

// Cleanup
unlink(tmp_hist_path);
}
