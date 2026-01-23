// Tests for history persistence (Vector<String>) via to-string/Parser
// Consolidated filesystem tests (fs_layer, tinyclj bindings, streaming)
#include "tests_common.h"
#include "../to_string.h"
#include "vector.h"
#include "../fs_layer.h"
#include "mini_format.h"
#include <unistd.h>

// Forward declarations from repl.c
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
  TEST_ASSERT_TRUE(TAG(elem0) == CLJ_STRING);
  TEST_ASSERT_TRUE(TAG(elem1) == CLJ_STRING);
  TEST_ASSERT_TRUE(TAG(elem2) == CLJ_STRING);
  RELEASE(elem0);
  RELEASE(elem1);
  RELEASE(elem2);
}

TEST(test_history_trim_to_50) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Build vector with 75 strings via string parsing
  // The parser now supports vectors with more than 64 elements
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
  RETAIN(loaded);
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
  CljString *str_content = to_string(elem0);
  RELEASE(elem0);
  TEST_ASSERT_NOT_NULL(str_content);
  TEST_ASSERT_EQUAL_STRING("(list 1 1.0 \"1\" \"one\")", string_data(str_content));

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
    RETAIN(loaded);
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
  // Use WITH_AUTORELEASE_POOL to match the REPL scenario
  // The crash happens when history_load_from_file's pool is popped,
  // but the strings are still referenced by the vector
  WITH_AUTORELEASE_POOL(
      { test_history_load_from_file_crash_reproduction_body(); });
}

static void register_test_history_load_from_file_crash_reproduction(void)
    __attribute__((constructor));
static void register_test_history_load_from_file_crash_reproduction(void) {
  char *filename = test_extract_filename_from_path(__FILE__);
  if (filename) {
    test_registry_add_with_group("test_history_load_from_file_crash_reproduction",
                                 test_history_load_from_file_crash_reproduction,
                                 filename);
    CLJ_FREE(filename);
  }
}

TEST(test_pr_str_escapes_quotes) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create a string with quotes inside
  const char *test_input = "(list 1 1.0 \"1\" \"one\")";
  CljObject *str = (CljObject *)make_string(test_input);
  TEST_ASSERT_NOT_NULL(str);

  // Test pr_str on the string
  CljString *result = pr_str(str);
  TEST_ASSERT_NOT_NULL(result);

  // Verify that quotes are escaped
  // Expected: "(list 1 1.0 \"1\" \"one\")"
  // The quotes inside should be escaped as \"
  TEST_ASSERT_EQUAL_STRING("\"(list 1 1.0 \\\"1\\\" \\\"one\\\")\"", string_data(result));

  // Verify that the result contains escaped quotes
  const char *escaped_quote = strstr(string_data(result), "\\\"");
  TEST_ASSERT_NOT_NULL(escaped_quote);

  RELEASE(str);
}

TEST(test_history_save_escapes_quotes) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create a vector with a string containing quotes
  const char *test_input = "(list 1 1.0 \"1\" \"one\")";
  CljObject *str = (CljObject *)make_string(test_input);
  TEST_ASSERT_NOT_NULL(str);

  CljVector *vec = make_vector(1, CLJ_VECTOR);
  vec = vector_conj(vec, str);

  // Save to file
  bool ok = history_save_to_file(vec, tmp_hist_path);
  TEST_ASSERT_TRUE(ok);

  // Read file content
  FILE *fp = fopen(tmp_hist_path, "r");
  TEST_ASSERT_NOT_NULL(fp);
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  char *buf = (char *)CLJ_MALLOC((size_t)sz + 1);
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

  CLJ_FREE(buf);
  // Don't release str separately - it's owned by vec and will be released with vec
  RELEASE(vec);
  unlink(tmp_hist_path);
}

/*
 * Unit Tests for File I/O Functions (slurp/spit)
 *
 * Test-First implementation for slurp and spit functions.
 */

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

  char *path = (char*)CLJ_MALLOC(strlen(template) + 1);
  strcpy(path, template);
  return path;
}

// Helper function to delete test file
static void cleanup_test_file(const char *path) {
  if (path) {
    unlink(path);
    CLJ_FREE((void *)path);
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
  test_snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
  CljObject *result = eval_string(expr, g_test_eval_state);

  // Verify result is a string
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);

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
  test_snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
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
  test_snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
  CljObject *result = eval_string(expr, g_test_eval_state);

  // Verify result is empty string
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);

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
  test_snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
  CljObject *result = eval_string(expr, g_test_eval_state);

  // Verify result contains all lines
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);

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
  test_snprintf(expr, sizeof(expr), "(spit \"%s\" \"Hello from spit!\")", test_file);
  (void)eval_string(expr,
                    g_test_eval_state); // spit returns nil (Clojure-compatible)

  // Read file back to verify content
  test_snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
  CljObject *read_result = eval_string(expr, g_test_eval_state);

  TEST_ASSERT_NOT_NULL(read_result);
  TEST_ASSERT_TRUE(TAG(read_result) == CLJ_STRING);

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
  test_snprintf(expr, sizeof(expr), "(spit \"%s\" \"New content\")", test_file);
  (void)eval_string(expr, g_test_eval_state);

  // Read file back to verify it was overwritten
  test_snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
  CljObject *read_result = eval_string(expr, g_test_eval_state);

  TEST_ASSERT_NOT_NULL(read_result);
  TEST_ASSERT_TRUE(TAG(read_result) == CLJ_STRING);

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
  test_snprintf(expr, sizeof(expr), "(spit \"%s\" \"%s\")", test_file, content);
  (void)eval_string(expr, g_test_eval_state);

  // Read back and verify
  test_snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
  CljObject *read_result = eval_string(expr, g_test_eval_state);

  TEST_ASSERT_NOT_NULL(read_result);
  TEST_ASSERT_TRUE(TAG(read_result) == CLJ_STRING);

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
  test_snprintf(expr, sizeof(expr), "(spit \"%s\" \"\")", test_file);
  (void)eval_string(expr, g_test_eval_state);

  // Read back and verify it's empty
  test_snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
  CljObject *read_result = eval_string(expr, g_test_eval_state);

  TEST_ASSERT_NOT_NULL(read_result);
  TEST_ASSERT_TRUE(TAG(read_result) == CLJ_STRING);

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
  test_snprintf(expr, sizeof(expr), "(spit \"%s\" \"%s\")", test_file, original_content);
  (void)eval_string(expr, g_test_eval_state);

  // Read back with slurp
  test_snprintf(expr, sizeof(expr), "(slurp \"%s\")", test_file);
  CljObject *read_result = eval_string(expr, g_test_eval_state);

  TEST_ASSERT_NOT_NULL(read_result);
  TEST_ASSERT_TRUE(TAG(read_result) == CLJ_STRING);

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

  // Test: parse a vector with strings inside eval_string (which creates inner
  // pool)
  WITH_AUTORELEASE_POOL({
    // This simulates what happens in history_load_from_file
    // eval_string creates an inner pool, parse_vector adds strings to that pool
    // When inner pool is popped, strings are freed even if retained by vector
    char expr[256];
    test_snprintf(expr, sizeof(expr), "[\"test1\" \"test2\" \"test3\"]");

    // eval_string creates inner pool
    CljObject *result = eval_string(expr, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR);

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
  if (test_content) fputs(test_content, fp);
  fclose(fp);

  // Simulate exactly what history_load_from_file does:
  // 1. Call eval_string (creates inner pool)
  // 2. Parse the result (adds objects to inner pool)
  // 3. Create new vector with parsed strings
  // 4. When inner pool is popped, strings should still be valid
    WITH_AUTORELEASE_POOL({
        TRY {
            char expr[512];
            test_snprintf(expr, sizeof(expr), "(slurp \"%s\")", tmp_hist_path);
            ID slurp_result = eval_string(expr, g_test_eval_state);
            
            if (!slurp_result || TAG(slurp_result) != CLJ_STRING) {
    TEST_FAIL_MESSAGE("slurp should return a string");
            } else {
    // Copy string buffer to avoid dependency on inner pool
    CljString *content = as_clj_string((CljObject *)slurp_result);
    const char *buf = clj_string_data(content);
    size_t buf_len = strlen(buf);
    char *buf_copy = (char *)CLJ_MALLOC(buf_len + 1);
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

    CLJ_FREE(buf_copy);

    // Process form after pool is popped (simulate what happens after
    // WITH_AUTORELEASE_POOL)
    if (form && !IS_IMMEDIATE(form) && TAG(form) == CLJ_VECTOR) {
      CljVector *v = as_vector((CljObject *)form);
      int count = vector_count(v);
      bool all_strings = count > 0;
      for (int i = 0; i < count && all_strings; i++) {
        ID elem = vector_nth(v, i);
        if (TAG(elem) != CLJ_STRING) {
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
                            // nth2 returns element with lifetime tied to vector - no release needed
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

// ============================================================================
// FILESYSTEM LAYER TESTS (from test_fs_layer.c)
// ============================================================================

TEST(test_fs_kv_store_roundtrip_bytes)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    const uint8_t in[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(fs_kv_put(st, "/k", in, sizeof(in)));

    uint8_t out[8] = {0};
    size_t saved_len = 0;
    size_t n = fs_kv_get(st, "/k", out, sizeof(out), &saved_len);
    TEST_ASSERT_EQUAL_UINT32(sizeof(in), (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT32(sizeof(in), (uint32_t)saved_len);
    TEST_ASSERT_EQUAL_MEMORY(in, out, sizeof(in));

    TEST_ASSERT_TRUE(fs_kv_del(st, "/k"));
    saved_len = 123;
    n = fs_kv_get(st, "/k", out, sizeof(out), &saved_len);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)saved_len);

    fs_kv_store_free(st);
}

TEST(test_fs_layer_write_read_stat_list_delete)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    /* write a file (no explicit mkdir needed - directories are implicit) */
    uint8_t bytes[600];
    for (size_t i = 0; i < sizeof(bytes); i++) {
        bytes[i] = (uint8_t)(i & 0xFF);
    }
    fs_err_t e = fs_write_bytes(st, "/data/file.bin", bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    // Sanity: binary meta must exist and be readable right after write
    {
        typedef struct __attribute__((packed)) FsFileMetaDbg {
            uint32_t magic;
            uint32_t version;
            uint32_t size;
            uint32_t chunks;
        } FsFileMetaDbg;
        FsFileMetaDbg meta = {0};
        size_t saved = 0;
        (void)fs_kv_get(st, "/data/file.bin", (uint8_t *)&meta, sizeof(meta), &saved);
        TEST_ASSERT_EQUAL_UINT32(sizeof(meta), (uint32_t)saved);
        TEST_ASSERT_EQUAL_HEX32(0x454C4946u, meta.magic); // 'F''I''L''E'
        TEST_ASSERT_EQUAL_UINT32(1u, meta.version);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(bytes), meta.size);
        TEST_ASSERT_EQUAL_UINT32(1u, meta.chunks);
    }

    /* read back */
    ID out = fs_read_bytes(st, "/data/file.bin");
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(out));
    CljByteArray *ba = as_byte_array(out);
    TEST_ASSERT_EQUAL_INT((int)sizeof(bytes), ba->length);
    TEST_ASSERT_EQUAL_MEMORY(bytes, ba->data, sizeof(bytes));

    /* stat */
    int64_t size = fs_stat_size(st, "/data/file.bin");
    TEST_ASSERT_EQUAL_INT64((int64_t)sizeof(bytes), size);

    /* list dir: contains exactly the file path */
    char last_key[FS_KEY_MAX] = {0};
    ID lst = fs_list_dir_batch(st, "/data/", NULL, 32, last_key, sizeof(last_key));
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(lst));
    CljVector *v = as_vector(lst);
    TEST_ASSERT_EQUAL_INT(1, vector_count(v));
    TEST_ASSERT_TRUE(last_key[0] == '\0'); // only entry, end reached

    // Entry shape: {:path "/data/file.bin" :meta {:size 600 :chunks 1}}
    ID entry0 = vector_nth(v, 0);
    assert_map((CljObject*)entry0);
    ID kw_path = (ID)intern_symbol_global(":path");
    ID kw_meta = (ID)intern_symbol_global(":meta");
    ID kw_size = (ID)intern_symbol_global(":size");
    ID kw_chunks = (ID)intern_symbol_global(":chunks");
    ID p0 = map_get((CljMap*)entry0, kw_path);
    assert_string((CljObject*)p0, "/data/file.bin");
    ID m0 = map_get((CljMap*)entry0, kw_meta);
    assert_map((CljObject*)m0);
    assert_fixnum((CljObject*)map_get((CljMap*)m0, kw_size), (int)sizeof(bytes));
    assert_fixnum((CljObject*)map_get((CljMap*)m0, kw_chunks), 1);

    /* delete meta only */
    TEST_ASSERT_TRUE(fs_delete(st, "/data/file.bin"));
    TEST_ASSERT_FALSE(fs_exists(st, "/data/file.bin"));

    /* chunks are still present (GC will clean later) */
    uint8_t tmp[16] = {0};
    size_t saved_len = 0;
    size_t n = fs_kv_get(st, "/data/file.bin@1#0000", tmp, sizeof(tmp), &saved_len);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(saved_len > 0);

    fs_kv_store_free(st);
}

TEST(test_fs_list_dir_batch_many_files)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    // Create: /many/file_00.bin .. /many/file_49.bin
    for (int i = 0; i < 50; i++) {
        char path[FS_KEY_MAX];
        mini_snprintf(path, sizeof(path), "/many/file_%02d.bin", i);
        uint8_t data = (uint8_t)i;
        fs_err_t e = fs_write_bytes(st, path, &data, 1);
        TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);
    }

    // Batch 1
    char last_key[FS_KEY_MAX] = {0};
    ID batch1 = fs_list_dir_batch(st, "/many/", NULL, 32, last_key, sizeof(last_key));
    TEST_ASSERT_NOT_NULL(batch1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(batch1));
    TEST_ASSERT_EQUAL_INT(32, vector_count(as_vector(batch1)));
    TEST_ASSERT_TRUE(last_key[0] != '\0');

    // Batch 2
    char last_key2[FS_KEY_MAX] = {0};
    ID batch2 = fs_list_dir_batch(st, "/many/", last_key, 32, last_key2, sizeof(last_key2));
    TEST_ASSERT_NOT_NULL(batch2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(batch2));
    TEST_ASSERT_EQUAL_INT(18, vector_count(as_vector(batch2)));
    TEST_ASSERT_TRUE(last_key2[0] == '\0'); // no more

    // Correctness: combine and verify order + uniqueness.
    CljVector *v1 = as_vector(batch1);
    CljVector *v2 = as_vector(batch2);
    TEST_ASSERT_NOT_NULL(v1);
    TEST_ASSERT_NOT_NULL(v2);
    TEST_ASSERT_EQUAL_INT(32, vector_count(v1));
    TEST_ASSERT_EQUAL_INT(18, vector_count(v2));

    // Verify every expected path is present exactly once, in lexicographic order.
    // The %02d naming makes lexicographic == numeric order for 0..49.
    for (int i = 0; i < 50; i++) {
        char expected[FS_KEY_MAX];
        mini_snprintf(expected, sizeof(expected), "/many/file_%02d.bin", i);

        ID elem = (i < 32) ? vector_nth(v1, (unsigned int)i)
                           : vector_nth(v2, (unsigned int)(i - 32));
        TEST_ASSERT_NOT_NULL(elem);
        assert_map((CljObject*)elem);
        ID kw_path = (ID)intern_symbol_global(":path");
        ID p = map_get((CljMap*)elem, kw_path);
        assert_string((CljObject*)p, expected);
    }

    fs_kv_store_free(st);
}

TEST(test_fs_layer_rewrite_increments_version)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    const uint8_t v1[3] = {1, 2, 3};
    fs_err_t e = fs_write_bytes(st, "/cfg/a.bin", v1, sizeof(v1));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    const uint8_t v2[5] = {9, 8, 7, 6, 5};
    e = fs_write_bytes(st, "/cfg/a.bin", v2, sizeof(v2));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, e);

    ID out = fs_read_bytes(st, "/cfg/a.bin");
    TEST_ASSERT_NOT_NULL(out);
    CljByteArray *ba = as_byte_array(out);
    TEST_ASSERT_EQUAL_INT((int)sizeof(v2), ba->length);
    TEST_ASSERT_EQUAL_MEMORY(v2, ba->data, sizeof(v2));

    /* old version chunks still exist */
    uint8_t tmp[8] = {0};
    size_t saved_len = 0;
    size_t n = fs_kv_get(st, "/cfg/a.bin@1#0000", tmp, sizeof(tmp), &saved_len);
    TEST_ASSERT_TRUE(n > 0);

    fs_kv_store_free(st);
}

// ============================================================================
// TINYCLJ FILESYSTEM AND KV BINDINGS TESTS (from test_tinyclj_bindings_fs_kv.c)
// ============================================================================

TEST(test_tinyclj_fs_and_kv_bindings_smoke)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    /* Reset global store so tests are deterministic. */
    fs_global_store_reset();

    /* Load :native stubs */
    eval_string("(require 'tinyclj.fs)", g_test_eval_state);
    eval_string("(require 'tiny-db.kv)", g_test_eval_state);

    /* write bytes (no explicit mkdir needed - directories are implicit) */
    CljObject *w = eval_string(
        "(let [a (byte-array 3)]"
        "  (aset a 0 1) (aset a 1 2) (aset a 2 3)"
        "  (tinyclj.fs/spit-bytes \"/data/x.bin\" a))",
        g_test_eval_state);
    (void)w; /* returns nil */

    /* read bytes */
    CljObject *rb = eval_string("(tinyclj.fs/slurp-bytes \"/data/x.bin\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(rb);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(rb));
    CljByteArray *ba = as_byte_array(rb);
    TEST_ASSERT_EQUAL_INT(3, ba->length);
    TEST_ASSERT_EQUAL_UINT8(1, ba->data[0]);
    TEST_ASSERT_EQUAL_UINT8(2, ba->data[1]);
    TEST_ASSERT_EQUAL_UINT8(3, ba->data[2]);

    /* list (lazy): realize into a vector */
    CljObject *lst = eval_string("(vec (tinyclj.fs/list \"/data/\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(lst));
    TEST_ASSERT_EQUAL_INT(1, vector_count(as_vector(lst)));

    /* list with >32 files (forces batching in tinyclj.fs/list) */
    eval_string(
        "(dotimes [i 50]"
        "  (let [a (byte-array 1)]"
        "    (aset a 0 (mod i 256))"
        "    (tinyclj.fs/spit-bytes (str \"/many/file_\" i \".bin\") a)))",
        g_test_eval_state);
    CljObject *many = eval_string("(vec (tinyclj.fs/list \"/many/\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(many);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(many));
    TEST_ASSERT_EQUAL_INT(50, vector_count(as_vector(many)));

    /* kv put/get (key must not start with /) */
    eval_string(
        "(let [a (byte-array 2)]"
        "  (aset a 0 9) (aset a 1 8)"
        "  (tiny-db.kv/put-bytes \"user:prefs\" a))",
        g_test_eval_state);
    CljObject *kvb = eval_string("(tiny-db.kv/get-bytes \"user:prefs\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(kvb);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(kvb));
    CljByteArray *kba = as_byte_array(kvb);
    TEST_ASSERT_EQUAL_INT(2, kba->length);
    TEST_ASSERT_EQUAL_UINT8(9, kba->data[0]);
    TEST_ASSERT_EQUAL_UINT8(8, kba->data[1]);
}

TEST(test_tinyclj_kv_supports_large_values)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    fs_global_store_reset();
    eval_string("(require 'tiny-db.kv)", g_test_eval_state);

    // 600 bytes -> forces chunking in fs_layer kv backend.
    eval_string(
        "(let [a (byte-array 600)]"
        "  (dotimes [i 600]"
        "    (aset a i (mod i 256)))"
        "  (tiny-db.kv/put-bytes \"big\" a))",
        g_test_eval_state);

    CljObject *kvb = eval_string("(tiny-db.kv/get-bytes \"big\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(kvb);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(kvb));
    CljByteArray *ba = as_byte_array(kvb);
    TEST_ASSERT_EQUAL_INT(600, ba->length);
    TEST_ASSERT_EQUAL_UINT8(0, ba->data[0]);
    TEST_ASSERT_EQUAL_UINT8(1, ba->data[1]);
    TEST_ASSERT_EQUAL_UINT8(255, ba->data[255]);
    TEST_ASSERT_EQUAL_UINT8(0, ba->data[256]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(599 % 256), ba->data[599]);
}

// ============================================================================
// STREAMING KV AND FILESYSTEM TESTS (from test_streaming_kv_fs.c)
// ============================================================================

typedef struct {
    uint8_t* out;
    size_t out_cap;
    size_t out_len;
    size_t max_seen;
} SinkCtx;

static tdb_status_t sink_collect_cb(const uint8_t* data, size_t len, void* arg)
{
    SinkCtx* c = (SinkCtx*)arg;
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_TRUE(len <= 4096); // contract: app never sees > 4KB
    if (len > c->max_seen) c->max_seen = len;
    if (c->out_len + len > c->out_cap) return TDB_ERR_NO_MEMORY;
    if (len) memcpy(c->out + c->out_len, data, len);
    c->out_len += len;
    return TDB_OK;
}

TEST(test_kv_stream_read_enforces_4kb_and_preserves_bytes)
{
    FsKvStore* st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    // 32KB test payload
    enum { N = 32 * 1024 };
    static uint8_t data[N];
    for (size_t i = 0; i < (size_t)N; i++) data[i] = (uint8_t)(i & 0xFF);

    const uint8_t key[] = {'k'};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_put_key_bytes_status(st, key, sizeof(key), data, sizeof(data)));

    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_reset(st));

    static uint8_t out[N];
    SinkCtx ctx = {.out = out, .out_cap = sizeof(out), .out_len = 0, .max_seen = 0};

    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_stream_read_key_bytes(st, key, sizeof(key), 4096, sink_collect_cb, &ctx));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)ctx.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof(data));
    TEST_ASSERT_TRUE(ctx.max_seen <= 4096);

    FsStreamStats stats = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_get(st, &stats));
    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)stats.blocks_read); // 32KB / 4KB

    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_reset(st));
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_get(st, &stats));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)stats.blocks_read);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)stats.blocks_written);

    fs_kv_store_free(st);
}

typedef struct {
    const uint8_t* data;
    size_t len;
    size_t pos;
    size_t call_idx;
} SrcCtx;

static tdb_status_t src_var_chunks(uint8_t* out, size_t out_cap, size_t* out_len, void* arg)
{
    if (out_len) *out_len = 0;
    SrcCtx* c = (SrcCtx*)arg;
    if (!c || (!out && out_cap != 0) || !out_len) return TDB_ERR_INVALID_ARG;
    if (c->pos >= c->len) return TDB_OK; // EOF -> out_len stays 0

    // Intentionally irregular sizes (includes > 4096).
    static const size_t pattern[] = {1, 7, 9000, 13, 4096, 3, 2000, 8191};
    const size_t want0 = pattern[c->call_idx % (sizeof(pattern) / sizeof(pattern[0]))];
    c->call_idx++;

    size_t want = want0;
    size_t remaining = c->len - c->pos;
    if (want > remaining) want = remaining;
    if (want > out_cap) want = out_cap;

    memcpy(out, c->data + c->pos, want);
    c->pos += want;
    *out_len = want;
    return TDB_OK;
}

TEST(test_kv_stream_write_accepts_variable_input_sizes_and_roundtrips)
{
    FsKvStore* st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    enum { N = 32 * 1024 };
    static uint8_t data[N];
    for (size_t i = 0; i < (size_t)N; i++) data[i] = (uint8_t)(0xA5u ^ (uint8_t)(i & 0xFF));

    const uint8_t key[] = {'w'};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_reset(st));

    SrcCtx src = {.data = data, .len = sizeof(data), .pos = 0, .call_idx = 0};
    size_t total_written = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_stream_write_key_bytes(st, key, sizeof(key), src_var_chunks, &src, &total_written));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)total_written);

    static uint8_t out[N];
    size_t saved = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_get_key_bytes_status(st, key, sizeof(key), out, sizeof(out), &saved));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)saved);
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof(data));

    FsStreamStats stats = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_get(st, &stats));
    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)stats.blocks_written);

    fs_kv_store_free(st);
}

TEST(test_fs_file_stream_write_read_roundtrip_and_chunk_cap)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    FsKvStore* st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    enum { N = 9000 };
    static uint8_t data[N];
    for (size_t i = 0; i < (size_t)N; i++) data[i] = (uint8_t)(i * 3u);

    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_reset(st));

    SrcCtx src = {.data = data, .len = sizeof(data), .pos = 0, .call_idx = 0};
    size_t total_written = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_file_stream_write(st, "/data/s.bin", src_var_chunks, &src, &total_written));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)total_written);

    static uint8_t out[N];
    SinkCtx sink = {.out = out, .out_cap = sizeof(out), .out_len = 0, .max_seen = 0};
    // Use a small max_chunk to ensure slicing works and stays <= max_chunk.
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_file_stream_read(st, "/data/s.bin", 1024, sink_collect_cb, &sink));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)sink.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof(data));
    TEST_ASSERT_TRUE(sink.max_seen <= 1024);

    FsStreamStats stats = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_get(st, &stats));
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)stats.blocks_written); // 9000 -> 3 chunks of 4096
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)stats.blocks_read);

    fs_kv_store_free(st);
}

TEST(test_kv_stream_read_from_offset_seeks_by_chunk_arithmetic)
{
    FsKvStore* st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    enum { N = 32 * 1024 };
    static uint8_t data[N];
    for (size_t i = 0; i < (size_t)N; i++) data[i] = (uint8_t)(i & 0xFF);

    const uint8_t key[] = {'s'};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_put_key_bytes_status(st, key, sizeof(key), data, sizeof(data)));

    const size_t off = 12345;
    static uint8_t out[N];
    SinkCtx sink = {.out = out, .out_cap = sizeof(out), .out_len = 0, .max_seen = 0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_stream_read_key_bytes_from(st, key, sizeof(key), off, 4096, sink_collect_cb, &sink));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(N - off), (uint32_t)sink.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data + off, out, N - off);
    fs_kv_store_free(st);
}

TEST(test_fs_file_stream_read_from_offset_seeks_by_chunk_arithmetic)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    FsKvStore* st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    enum { N = 9000 };
    static uint8_t data[N];
    for (size_t i = 0; i < (size_t)N; i++) data[i] = (uint8_t)(0x5Au + (uint8_t)i);

    SrcCtx src = {.data = data, .len = sizeof(data), .pos = 0, .call_idx = 0};
    size_t total_written = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_file_stream_write(st, "/data/seek.bin", src_var_chunks, &src, &total_written));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)total_written);

    const size_t off = 5000;
    static uint8_t out[N];
    SinkCtx sink = {.out = out, .out_cap = sizeof(out), .out_len = 0, .max_seen = 0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_file_stream_read_from(st, "/data/seek.bin", off, 1024, sink_collect_cb, &sink));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(N - off), (uint32_t)sink.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data + off, out, N - off);

    fs_kv_store_free(st);
}
