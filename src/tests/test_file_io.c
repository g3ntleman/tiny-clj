// Tests for history persistence (Vector<String>) via to-string/Parser
// Consolidated filesystem tests (fs_layer, tiny-clj bindings, streaming)
#include "tests_common.h"
#include "../to_string.h"
#include "vector.h"
#include "../fs_layer.h"
#include "../event_loop.h"
#include "mini_format.h"
#include <unistd.h>

// Forward declarations from repl.c
extern bool history_save_to_file(CljPersistentVector *vec, const char *path);
extern CljObject *history_load_from_file(const char *path);
extern CljObject *history_trim_last_n(CljPersistentVector *vec, int limit);

static const char *tmp_hist_path = "/tmp/tiny_clj_history_test.edn";

TEST(test_history_roundtrip_basic) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Erzeuge Vector aus Strings
  CljObject *vec = eval_string("[\"a\" \"b\" \"c\"]", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(vec);
  TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(vec));

  // Speichern
  bool ok = history_save_to_file(as_persistent_vector((ID)vec), tmp_hist_path);
  TEST_ASSERT_TRUE(ok);

  // Laden
  CljObject *loaded = history_load_from_file(tmp_hist_path);
  TEST_ASSERT_NOT_NULL(loaded);
  TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(loaded));

  // Vergleiche Count und Werte
  CljObject *c = eval_string("(count [\"a\" \"b\" \"c\"])", g_test_eval_state);
  TEST_ASSERT_TRUE(is_fixnum((CljValue)c));
  CljPersistentVector *v = as_persistent_vector(loaded);
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
  TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(vec));
  CljPersistentVector *v = as_persistent_vector((ID)vec);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_INT(75, vector_count(v));

  // RETAIN vec to keep it alive while trimmed references its elements
  RETAIN(vec);
  CljObject *trimmed = history_trim_last_n(as_persistent_vector((ID)vec), 50);
  TEST_ASSERT_NOT_NULL(trimmed);
  // RETAIN trimmed to keep it alive outside of autorelease pool
  RETAIN(trimmed);
  CljPersistentVector *tv = as_persistent_vector((ID)trimmed);
  TEST_ASSERT_NOT_NULL(tv);
  TEST_ASSERT_EQUAL_INT(50, vector_count(tv));

  // Speichern und Laden, weiterhin 50
  bool ok = history_save_to_file(as_persistent_vector((ID)trimmed), tmp_hist_path);
  TEST_ASSERT_TRUE(ok);
  CljObject *loaded = history_load_from_file(tmp_hist_path);
  TEST_ASSERT_NOT_NULL(loaded);
  // RETAIN loaded to keep it alive outside of autorelease pool
  RETAIN(loaded);
  CljPersistentVector *lv = as_persistent_vector(loaded);
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
  CljObject *loaded = history_load_from_file(tmp_hist_path);
  TEST_ASSERT_NOT_NULL(loaded);
  // RETAIN loaded to keep it alive outside of autorelease pool
  RETAIN(loaded);
  TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(loaded));

  // Verify vector structure
  CljPersistentVector *v = as_persistent_vector(loaded);
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
  CljObject *loaded = history_load_from_file(tmp_hist_path);

  // If we get here, the crash didn't happen (or was caught)
  // But the memory management is still wrong
  if (loaded) {
    RETAIN(loaded);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(loaded));

  CljPersistentVector *v = as_persistent_vector(loaded);
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

TEST(test_make_string_description_escapes_quotes) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // Create a string with quotes inside
  const char *test_input = "(list 1 1.0 \"1\" \"one\")";
  CljString *str = make_string(test_input);
  TEST_ASSERT_NOT_NULL(str);

  // Test make_string_description on the string
  CljString *result = make_string_description(str);
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
  CljString *str = make_string(test_input);
  TEST_ASSERT_NOT_NULL(str);

  CljPersistentVector *vec = make_vector(1, false);
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
    FsKvStore *st = fs_global_store_if_initialized();
    if (st) {
      (void)fs_delete(st, path);
    }
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
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);

    CljPersistentVector *v = as_persistent_vector((ID)result);
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
    if (form && !IS_IMMEDIATE(form) && TAG(form) == CLJ_VECTOR_PERSISTENT) {
      CljPersistentVector *v = as_persistent_vector(form);
      int count = (int)vector_count(v);
      bool all_strings = count > 0;
      for (int i = 0; i < count && all_strings; i++) {
        ID elem = vector_nth(v, i);
        if (TAG(elem) != CLJ_STRING) {
          all_strings = false;
        }
      }
      if (all_strings) {
        CljPersistentVector* new_vec = make_vector((unsigned int)count, false);
        for (int i = 0; i < count; i++) {
          ID elem = vector_nth(v, (unsigned int)i);
          new_vec = vector_conj(new_vec, elem);
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

TEST(test_fs_kv_put_schedules_named_sync_timer)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    const uint8_t in[] = {0xAA, 0xBB};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_put_status(st, "/sync/key", in, sizeof(in)));

    ID sync_key = intern_symbol_global("fs.kv.sync");
    TEST_ASSERT_TRUE(timer_cancel_named(sync_key));
    TEST_ASSERT_FALSE(timer_cancel_named(sync_key));

    fs_kv_store_free(st);
}

TEST(test_fs_kv_debounce_restarts_named_sync_timer)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    const uint8_t a[] = {0x01};
    const uint8_t b[] = {0x02};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_put_status(st, "/sync/a", a, sizeof(a)));
    usleep(200000);
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_put_status(st, "/sync/b", b, sizeof(b)));

    usleep(850000);
    TEST_ASSERT_FALSE(event_loop_run_next(NULL, g_test_eval_state));

    usleep(300000);
    // Debounced sync now runs in two idle-safe steps:
    // 1) named timer callback enqueues one coalesced idle ingress event
    // 2) ingress callback performs the actual kv sync
    TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));
    TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));
    TEST_ASSERT_FALSE(event_loop_run_next(NULL, g_test_eval_state));

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
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(lst));
    CljPersistentVector *v = as_persistent_vector((ID)lst);
    TEST_ASSERT_EQUAL_INT(1, vector_count(v));
    TEST_ASSERT_TRUE(last_key[0] == '\0'); // only entry, end reached

    // Entry shape: {:path "/data/file.bin" :meta {:size 600 :chunks 1}}
    ID entry0 = vector_nth(v, 0);
    assert_map((CljObject*)entry0);
    ID kw_path = (ID)intern_symbol_global(":path");
    ID kw_meta = (ID)intern_symbol_global(":meta");
    ID kw_size = (ID)intern_symbol_global(":size");
    ID kw_chunks = (ID)intern_symbol_global(":chunks");
    ID p0 = map_get((CljPersistentMap*)entry0, kw_path);
    assert_string((CljObject*)p0, "/data/file.bin");
    ID m0 = map_get((CljPersistentMap*)entry0, kw_meta);
    assert_map((CljObject*)m0);
    assert_fixnum((CljObject*)map_get((CljPersistentMap*)m0, kw_size), (int)sizeof(bytes));
    assert_fixnum((CljObject*)map_get((CljPersistentMap*)m0, kw_chunks), 1);

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
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(batch1));
    TEST_ASSERT_EQUAL_INT(32, vector_count(as_persistent_vector(batch1)));
    TEST_ASSERT_TRUE(last_key[0] != '\0');

    // Batch 2
    char last_key2[FS_KEY_MAX] = {0};
    ID batch2 = fs_list_dir_batch(st, "/many/", last_key, 32, last_key2, sizeof(last_key2));
    TEST_ASSERT_NOT_NULL(batch2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(batch2));
    TEST_ASSERT_EQUAL_INT(18, vector_count(as_persistent_vector(batch2)));
    TEST_ASSERT_TRUE(last_key2[0] == '\0'); // no more

    // Correctness: combine and verify order + uniqueness.
    CljPersistentVector *v1 = as_persistent_vector(batch1);
    CljPersistentVector *v2 = as_persistent_vector(batch2);
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
        ID p = map_get((CljPersistentMap*)elem, kw_path);
        assert_string((CljObject*)p, expected);
    }

    fs_kv_store_free(st);
}

// ============================================================================
// Cursor edge cases: list_dir + delete + close (DEALLOC). Regression for
// B_DELCRSR / bt_bcursor on tdb_kv_close.
// ============================================================================

TEST(test_fs_cursor_list_empty_dir_then_close)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);
    char last_key[FS_KEY_MAX] = {0};
    ID lst = fs_list_dir_batch(st, "/empty/", NULL, 32, last_key, sizeof(last_key));
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(0, vector_count(as_persistent_vector(lst)));
    fs_kv_store_free(st);
}

TEST(test_fs_cursor_list_one_file_delete_it_then_close)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);
    uint8_t b = 1;
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, "/x/only.bin", &b, 1));
    char last_key[FS_KEY_MAX] = {0};
    ID lst = fs_list_dir_batch(st, "/x/", NULL, 32, last_key, sizeof(last_key));
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(1, vector_count(as_persistent_vector(lst)));
    TEST_ASSERT_TRUE(fs_delete(st, "/x/only.bin"));
    fs_kv_store_free(st);
}

TEST(test_fs_cursor_list_then_delete_other_then_close)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);
    uint8_t b = 0;
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, "/d/a.bin", &b, 1));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, "/d/b.bin", &b, 1));
    char last_key[FS_KEY_MAX] = {0};
    ID lst = fs_list_dir_batch(st, "/d/", NULL, 32, last_key, sizeof(last_key));
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(2, vector_count(as_persistent_vector(lst)));
    TEST_ASSERT_TRUE(fs_delete(st, "/d/b.bin"));
    TEST_ASSERT_FALSE(fs_exists(st, "/d/b.bin"));
    fs_kv_store_free(st);
}

TEST(test_fs_cursor_list_then_delete_first_listed_then_close)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);
    uint8_t b = 0;
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, "/d/first.bin", &b, 1));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, "/d/second.bin", &b, 1));
    char last_key[FS_KEY_MAX] = {0};
    ID lst = fs_list_dir_batch(st, "/d/", NULL, 32, last_key, sizeof(last_key));
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(2, vector_count(as_persistent_vector(lst)));
    TEST_ASSERT_TRUE(fs_delete(st, "/d/first.bin"));
    fs_kv_store_free(st);
}

TEST(test_fs_cursor_list_batch_partial_then_delete_then_close)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);
    uint8_t b = 0;
    for (int i = 0; i < 10; i++) {
        char path[FS_KEY_MAX];
        mini_snprintf(path, sizeof(path), "/batch/f_%02d.bin", i);
        TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, path, &b, 1));
    }
    char last_key[FS_KEY_MAX] = {0};
    ID batch = fs_list_dir_batch(st, "/batch/", NULL, 4, last_key, sizeof(last_key));
    TEST_ASSERT_NOT_NULL(batch);
    TEST_ASSERT_EQUAL_INT(4, vector_count(as_persistent_vector(batch)));
    TEST_ASSERT_TRUE(last_key[0] != '\0');
    TEST_ASSERT_TRUE(fs_delete(st, "/batch/f_01.bin"));
    fs_kv_store_free(st);
}

TEST(test_fs_cursor_two_lists_then_delete_then_close)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);
    uint8_t b = 0;
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, "/two/a.bin", &b, 1));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, "/two/b.bin", &b, 1));
    char last[FS_KEY_MAX] = {0};
    ID l1 = fs_list_dir_batch(st, "/two/", NULL, 32, last, sizeof(last));
    TEST_ASSERT_NOT_NULL(l1);
    TEST_ASSERT_EQUAL_INT(2, vector_count(as_persistent_vector(l1)));
    ID l2 = fs_list_dir_batch(st, "/two/", NULL, 32, last, sizeof(last));
    TEST_ASSERT_NOT_NULL(l2);
    TEST_ASSERT_TRUE(fs_delete(st, "/two/a.bin"));
    fs_kv_store_free(st);
}

TEST(test_fs_cursor_list_exhaust_then_delete_all_then_close)
{
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);
    uint8_t b = 0;
    for (int i = 0; i < 5; i++) {
        char path[FS_KEY_MAX];
        mini_snprintf(path, sizeof(path), "/exhaust/e_%d.bin", i);
        TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, path, &b, 1));
    }
    char last_key[FS_KEY_MAX] = {0};
    ID lst = fs_list_dir_batch(st, "/exhaust/", NULL, 32, last_key, sizeof(last_key));
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(5, vector_count(as_persistent_vector(lst)));
    TEST_ASSERT_TRUE(last_key[0] == '\0');
    for (int i = 0; i < 5; i++) {
        char path[FS_KEY_MAX];
        mini_snprintf(path, sizeof(path), "/exhaust/e_%d.bin", i);
        TEST_ASSERT_TRUE(fs_delete(st, path));
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

  TEST(test_fs_list_dir_batch_skips_internal_meta_sidecars_during_pagination)
  {
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    uint8_t a = 1;
    uint8_t b = 2;
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, "/paged/a.bin", &a, 1));
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR, fs_write_bytes(st, "/paged/b.bin", &b, 1));

    uint8_t meta_key[FS_KEY_MAX];
    size_t meta_key_len = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                fs_make_meta_sidecar_key("/paged/a.bin",
                             meta_key,
                             sizeof(meta_key),
                             &meta_key_len));

    const uint8_t meta_bytes[2] = {9, 9};
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                fs_kv_put_key_bytes_status(st,
                             meta_key,
                             meta_key_len,
                             meta_bytes,
                             sizeof(meta_bytes)));

    char last1[FS_KEY_MAX] = {0};
    ID page1 = fs_list_dir_batch(st, "/paged/", NULL, 1, last1, sizeof(last1));
    TEST_ASSERT_NOT_NULL(page1);
    TEST_ASSERT_EQUAL_INT(1, vector_count(as_persistent_vector(page1)));
    ID page1_entry = vector_nth(as_persistent_vector(page1), 0);
    assert_string((CljObject *)map_get((CljPersistentMap *)page1_entry,
                       (ID)intern_symbol_global(":path")),
            "/paged/a.bin");
    TEST_ASSERT_TRUE(last1[0] != '\0');

    char last2[FS_KEY_MAX] = {0};
    ID page2 = fs_list_dir_batch(st, "/paged/", last1, 1, last2, sizeof(last2));
    TEST_ASSERT_NOT_NULL(page2);
    TEST_ASSERT_EQUAL_INT(1, vector_count(as_persistent_vector(page2)));
    ID page2_entry = vector_nth(as_persistent_vector(page2), 0);
    assert_string((CljObject *)map_get((CljPersistentMap *)page2_entry,
                       (ID)intern_symbol_global(":path")),
            "/paged/b.bin");

    char last3[FS_KEY_MAX] = {0};
    ID page3 = fs_list_dir_batch(st, "/paged/", last2, 1, last3, sizeof(last3));
    TEST_ASSERT_NOT_NULL(page3);
    TEST_ASSERT_EQUAL_INT(0, vector_count(as_persistent_vector(page3)));
    TEST_ASSERT_TRUE(last3[0] == '\0');

    fs_kv_store_free(st);
  }

  TEST(test_fs_write_bytes_rejects_reserved_control_bytes_in_public_paths)
  {
    FsKvStore *st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    char invalid_path[] = {'/', 'b', 'a', 'd', (char)0x01, 'x', '\0'};
    uint8_t byte = 1;

    TEST_ASSERT_EQUAL_INT(FS_ERR_INVALID_PATH,
                fs_write_bytes(st, invalid_path, &byte, 1));
    TEST_ASSERT_EQUAL_INT(FS_ERR_INVALID_PATH,
                fs_set_size(st, invalid_path, 4));
    TEST_ASSERT_FALSE(fs_exists(st, invalid_path));
    TEST_ASSERT_FALSE(fs_delete(st, invalid_path));

    fs_kv_store_free(st);
  }

// ============================================================================
// TINY-CLJ FILESYSTEM AND KV BINDINGS TESTS (from test_tiny_clj_bindings_fs_kv.c)
// ============================================================================

TEST(test_tiny_clj_fs_and_kv_bindings_smoke)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    /* Reset global store so tests are deterministic. */
    fs_global_store_reset();

    /* Load :native stubs */
    eval_string("(require 'tiny-clj.fs)", g_test_eval_state);
    eval_string("(require 'tiny-db.kv)", g_test_eval_state);

    /* write bytes (no explicit mkdir needed - directories are implicit) */
    CljObject *w = eval_string(
        "(let [a (byte-array 3)]"
        "  (aset a 0 1) (aset a 1 2) (aset a 2 3)"
        "  (tiny-clj.fs/spit-bytes \"/data/x.bin\" a))",
        g_test_eval_state);
    (void)w; /* returns nil */

    /* read bytes */
    CljObject *rb = eval_string("(tiny-clj.fs/slurp-bytes \"/data/x.bin\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(rb);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(rb));
    CljByteArray *ba = as_byte_array(rb);
    TEST_ASSERT_EQUAL_INT(3, ba->length);
    TEST_ASSERT_EQUAL_UINT8(1, ba->data[0]);
    TEST_ASSERT_EQUAL_UINT8(2, ba->data[1]);
    TEST_ASSERT_EQUAL_UINT8(3, ba->data[2]);

    /* list (lazy): realize into a vector */
    CljObject *lst = eval_string("(vec (tiny-clj.fs/list \"/data/\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(lst);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(lst));
    TEST_ASSERT_EQUAL_INT(1, vector_count(as_persistent_vector((ID)lst)));

    /* list with >32 files (forces batching in tiny-clj.fs/list) */
    eval_string(
        "(dotimes [i 50]"
        "  (let [a (byte-array 1)]"
        "    (aset a 0 (mod i 256))"
        "    (tiny-clj.fs/spit-bytes (str \"/many/file_\" i \".bin\") a)))",
        g_test_eval_state);
    CljObject *many = eval_string("(vec (tiny-clj.fs/list \"/many/\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(many);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(many));
    TEST_ASSERT_EQUAL_INT(50, vector_count(as_persistent_vector((ID)many)));

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

TEST(test_tiny_clj_fs_meta_set_merges_listing_via_internal_sidecar)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    fs_global_store_reset();
    eval_string("(require 'tiny-clj.fs)", g_test_eval_state);

    FsKvStore *st = fs_global_store();
    TEST_ASSERT_NOT_NULL(st);

    (void)eval_string(
      "(let [a (byte-array 3)]"
      "  (aset a 0 1) (aset a 1 2) (aset a 2 3)"
      "  (tiny-clj.fs/spit-bytes \"/data/meta.bin\" a))",
      g_test_eval_state);

    (void)eval_string(
      "(tiny-clj.fs/meta-set! \"/data/meta.bin\" {:foo \"bar\" :answer 42})",
      g_test_eval_state);

    TEST_ASSERT_FALSE(fs_exists(st, "/data/meta.bin.meta"));

    uint8_t meta_key[FS_KEY_MAX];
    size_t meta_key_len = 0u;
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                          fs_make_meta_sidecar_key("/data/meta.bin",
                                                   meta_key,
                                                   sizeof(meta_key),
                                                   &meta_key_len));

    size_t saved = 0u;
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                          fs_kv_get_key_bytes_status(st,
                                                     meta_key,
                                                     meta_key_len,
                                                     NULL,
                                                     0,
                                                     &saved));
    TEST_ASSERT_TRUE(saved > 0u);

    CljObject *entries = eval_string("(vec (tiny-clj.fs/list \"/data/\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(entries);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(entries));
    TEST_ASSERT_EQUAL_INT(1, vector_count(as_persistent_vector((ID)entries)));

    ID entry = vector_nth(as_persistent_vector((ID)entries), 0);
    assert_map((CljObject *)entry);
    ID meta = map_get((CljPersistentMap *)entry, (ID)intern_symbol_global(":meta"));
    assert_map((CljObject *)meta);

    assert_string((CljObject *)map_get((CljPersistentMap *)meta,
                                       (ID)intern_symbol_global(":foo")),
                  "bar");
    assert_fixnum((CljObject *)map_get((CljPersistentMap *)meta,
                                       (ID)intern_symbol_global(":answer")),
                  42);
    assert_fixnum((CljObject *)map_get((CljPersistentMap *)meta,
                                       (ID)intern_symbol_global(":size")),
                  3);
}

  TEST(test_tiny_clj_fs_list_ignores_unreadable_or_non_map_meta_sidecars)
  {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    fs_global_store_reset();
    eval_string("(require 'tiny-clj.fs)", g_test_eval_state);

    FsKvStore *st = fs_global_store();
    TEST_ASSERT_NOT_NULL(st);

    (void)eval_string(
      "(let [a (byte-array 4)]"
      "  (aset a 0 1) (aset a 1 2) (aset a 2 3) (aset a 3 4)"
      "  (tiny-clj.fs/spit-bytes \"/data/bad-meta-a.bin\" a)"
      "  (tiny-clj.fs/spit-bytes \"/data/bad-meta-b.bin\" a))",
      g_test_eval_state);

    uint8_t meta_key_a[FS_KEY_MAX];
    uint8_t meta_key_b[FS_KEY_MAX];
    size_t meta_key_a_len = 0u;
    size_t meta_key_b_len = 0u;
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                fs_make_meta_sidecar_key("/data/bad-meta-a.bin",
                             meta_key_a,
                             sizeof(meta_key_a),
                             &meta_key_a_len));
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                fs_make_meta_sidecar_key("/data/bad-meta-b.bin",
                             meta_key_b,
                             sizeof(meta_key_b),
                             &meta_key_b_len));

    const uint8_t invalid_edn[] = {0xffu, 0xfeu};
    const uint8_t non_map_edn[] = {'4', '2'};

    TEST_ASSERT_EQUAL_INT(TDB_OK,
                fs_kv_put_key_bytes_status(st,
                             meta_key_a,
                             meta_key_a_len,
                             invalid_edn,
                             sizeof(invalid_edn)));
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                fs_kv_put_key_bytes_status(st,
                             meta_key_b,
                             meta_key_b_len,
                             non_map_edn,
                             sizeof(non_map_edn)));

    CljObject *entries = eval_string("(vec (tiny-clj.fs/list \"/data/\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(entries);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(entries));
    TEST_ASSERT_EQUAL_INT(2, vector_count(as_persistent_vector((ID)entries)));

    ID kw_path = (ID)intern_symbol_global(":path");
    ID kw_meta = (ID)intern_symbol_global(":meta");
    ID kw_size = (ID)intern_symbol_global(":size");
    bool saw_a = false;
    bool saw_b = false;

    int entry_count = vector_count(as_persistent_vector((ID)entries));
    for (int i = 0; i < entry_count; i++) {
      ID entry = vector_nth(as_persistent_vector((ID)entries), i);
      assert_map((CljObject *)entry);
      CljString *path = as_clj_string(map_get((CljPersistentMap *)entry, kw_path));
      ID meta = map_get((CljPersistentMap *)entry, kw_meta);
      assert_map((CljObject *)meta);

      const char *path_str = clj_string_data(path);
      if (strcmp(path_str, "/data/bad-meta-a.bin") == 0 ||
        strcmp(path_str, "/data/bad-meta-b.bin") == 0) {
        ID size = map_get((CljPersistentMap *)meta, kw_size);
        assert_fixnum((CljObject *)size, 4);
        TEST_ASSERT_TRUE(map_get((CljPersistentMap *)meta,
               (ID)intern_symbol_global(":foo")) == NOT_FOUND);
        if (strcmp(path_str, "/data/bad-meta-a.bin") == 0) {
          saw_a = true;
        } else {
          saw_b = true;
        }
      }
    }

    TEST_ASSERT_TRUE(saw_a);
    TEST_ASSERT_TRUE(saw_b);
  }

  TEST(test_tiny_clj_fs_spit_bytes_nil_deletes_and_recreates_file)
  {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    fs_global_store_reset();
    eval_string("(require 'tiny-clj.fs)", g_test_eval_state);

    FsKvStore *st = fs_global_store();
    TEST_ASSERT_NOT_NULL(st);

    (void)eval_string(
      "(let [a (byte-array 3)]"
      "  (aset a 0 1) (aset a 1 2) (aset a 2 3)"
      "  (tiny-clj.fs/spit-bytes \"/data/delete.bin\" a))",
      g_test_eval_state);

    TEST_ASSERT_TRUE(fs_exists(st, "/data/delete.bin"));

    const uint8_t legacy_meta_bytes[3] = {7, 7, 7};
    TEST_ASSERT_EQUAL_INT(FS_NO_ERR,
                fs_write_bytes(st,
                       "/data/delete.bin.meta",
                       legacy_meta_bytes,
                       sizeof(legacy_meta_bytes)));

    const uint8_t kv_meta_bytes[4] = {4, 3, 2, 1};
    uint8_t kv_meta_key[FS_KEY_MAX];
    size_t kv_meta_key_len = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                          fs_make_meta_sidecar_key("/data/delete.bin",
                                                   kv_meta_key,
                                                   sizeof(kv_meta_key),
                                                   &kv_meta_key_len));
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                fs_kv_put_key_bytes_status(st,
                             kv_meta_key,
                                                     kv_meta_key_len,
                             kv_meta_bytes,
                             sizeof(kv_meta_bytes)));

    TEST_ASSERT_TRUE(fs_exists(st, "/data/delete.bin.meta"));

    size_t saved = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                fs_kv_get_key_bytes_status(st,
                             kv_meta_key,
                                                     kv_meta_key_len,
                             NULL,
                             0,
                             &saved));
    TEST_ASSERT_EQUAL_UINT32(sizeof(kv_meta_bytes), (uint32_t)saved);

    (void)eval_string("(tiny-clj.fs/spit-bytes \"/data/delete.bin\" nil)", g_test_eval_state);

    TEST_ASSERT_FALSE(fs_exists(st, "/data/delete.bin"));
    TEST_ASSERT_FALSE(fs_exists(st, "/data/delete.bin.meta"));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_NOT_FOUND,
                fs_kv_get_key_bytes_status(st,
                             kv_meta_key,
                                                     kv_meta_key_len,
                             NULL,
                             0,
                             &saved));

    CljObject *entries = eval_string("(vec (tiny-clj.fs/list \"/data/\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(entries);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(entries));
    TEST_ASSERT_EQUAL_INT(0, vector_count(as_persistent_vector((ID)entries)));

    (void)eval_string(
      "(let [a (byte-array 2)]"
      "  (aset a 0 9) (aset a 1 7)"
      "  (tiny-clj.fs/spit-bytes \"/data/delete.bin\" a))",
      g_test_eval_state);

    CljObject *rb = eval_string("(tiny-clj.fs/slurp-bytes \"/data/delete.bin\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(rb);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(rb));
    CljByteArray *ba = as_byte_array(rb);
    TEST_ASSERT_EQUAL_INT(2, ba->length);
    TEST_ASSERT_EQUAL_UINT8(9, ba->data[0]);
    TEST_ASSERT_EQUAL_UINT8(7, ba->data[1]);
  }

  TEST(test_tiny_clj_fs_read_write_block_patch_and_extend)
  {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    fs_global_store_reset();
    eval_string("(require 'tiny-clj.fs)", g_test_eval_state);

    (void)eval_string(
      "(let [a (byte-array 6)]"
      "  (aset a 0 1) (aset a 1 2) (aset a 2 3)"
      "  (aset a 3 4) (aset a 4 5) (aset a 5 6)"
      "  (tiny-clj.fs/spit-bytes \"/data/block.bin\" a))",
      g_test_eval_state);

    CljObject *mid = eval_string("(tiny-clj.fs/read-block \"/data/block.bin\" 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(mid);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(mid));
    CljByteArray *mid_ba = as_byte_array(mid);
    TEST_ASSERT_EQUAL_INT(3, mid_ba->length);
    TEST_ASSERT_EQUAL_UINT8(3, mid_ba->data[0]);
    TEST_ASSERT_EQUAL_UINT8(4, mid_ba->data[1]);
    TEST_ASSERT_EQUAL_UINT8(5, mid_ba->data[2]);

    CljObject *patch_stat = eval_string(
      "(let [a (byte-array 3)]"
      "  (aset a 0 9) (aset a 1 8) (aset a 2 7)"
      "  (tiny-clj.fs/write-block \"/data/block.bin\" 2 a))",
      g_test_eval_state);
    assert_map((CljObject *)patch_stat);
    ID patch_size = map_get((CljPersistentMap *)patch_stat, (ID)intern_symbol_global(":size"));
    assert_fixnum((CljObject *)patch_size, 6);

    CljObject *patched = eval_string("(tiny-clj.fs/slurp-bytes \"/data/block.bin\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(patched);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(patched));
    CljByteArray *patched_ba = as_byte_array(patched);
    const uint8_t expected_patch[6] = {1, 2, 9, 8, 7, 6};
    TEST_ASSERT_EQUAL_INT(6, patched_ba->length);
    TEST_ASSERT_EQUAL_MEMORY(expected_patch, patched_ba->data, sizeof(expected_patch));

    CljObject *extend_stat = eval_string(
      "(let [a (byte-array 2)]"
      "  (aset a 0 4) (aset a 1 5)"
      "  (tiny-clj.fs/write-block \"/data/block.bin\" 8 a))",
      g_test_eval_state);
    assert_map((CljObject *)extend_stat);
    ID extend_size = map_get((CljPersistentMap *)extend_stat, (ID)intern_symbol_global(":size"));
    assert_fixnum((CljObject *)extend_size, 10);

    CljObject *extended = eval_string("(tiny-clj.fs/slurp-bytes \"/data/block.bin\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(extended);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(extended));
    CljByteArray *extended_ba = as_byte_array(extended);
    const uint8_t expected_extended[10] = {1, 2, 9, 8, 7, 6, 0, 0, 4, 5};
    TEST_ASSERT_EQUAL_INT(10, extended_ba->length);
    TEST_ASSERT_EQUAL_MEMORY(expected_extended, extended_ba->data, sizeof(expected_extended));

    CljObject *tail = eval_string("(tiny-clj.fs/read-block \"/data/block.bin\" 9 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(tail);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(tail));
    CljByteArray *tail_ba = as_byte_array(tail);
    TEST_ASSERT_EQUAL_INT(1, tail_ba->length);
    TEST_ASSERT_EQUAL_UINT8(5, tail_ba->data[0]);

    CljObject *empty = eval_string("(tiny-clj.fs/read-block \"/data/block.bin\" 99 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(empty);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(empty));
    TEST_ASSERT_EQUAL_INT(0, as_byte_array(empty)->length);
  }

  TEST(test_tiny_clj_fs_set_size_creates_zero_filled_file_and_rejects_negative_inputs)
  {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    fs_global_store_reset();
    eval_string("(require 'tiny-clj.fs)", g_test_eval_state);

    CljObject *stat = eval_string("(tiny-clj.fs/set-size! \"/data/sized.bin\" 5)", g_test_eval_state);
    assert_map((CljObject *)stat);
    ID size = map_get((CljPersistentMap *)stat, (ID)intern_symbol_global(":size"));
    assert_fixnum((CljObject *)size, 5);

    CljObject *bytes = eval_string("(tiny-clj.fs/slurp-bytes \"/data/sized.bin\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(bytes));
    CljByteArray *ba = as_byte_array(bytes);
    TEST_ASSERT_EQUAL_INT(5, ba->length);
    for (int i = 0; i < ba->length; i++) {
      TEST_ASSERT_EQUAL_UINT8(0, ba->data[i]);
    }

    bool caught_read = false;
    bool caught_write = false;
    bool caught_size = false;

    TRY {
      (void)eval_string("(tiny-clj.fs/read-block \"/data/sized.bin\" -1 1)", g_test_eval_state);
    } CATCH(ex) {
      (void)ex;
      caught_read = true;
    } END_TRY

    TRY {
      (void)eval_string(
        "(let [a (byte-array 1)]"
        "  (aset a 0 1)"
        "  (tiny-clj.fs/write-block \"/data/sized.bin\" -1 a))",
        g_test_eval_state);
    } CATCH(ex) {
      (void)ex;
      caught_write = true;
    } END_TRY

    TRY {
      (void)eval_string("(tiny-clj.fs/set-size! \"/data/sized.bin\" -1)", g_test_eval_state);
    } CATCH(ex) {
      (void)ex;
      caught_size = true;
    } END_TRY

    TEST_ASSERT_TRUE(caught_read);
    TEST_ASSERT_TRUE(caught_write);
    TEST_ASSERT_TRUE(caught_size);
  }

TEST(test_tiny_clj_kv_supports_large_values)
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
