/*
 * Performance test for require 'clojure.string using (time)
 */

#include "tests_common.h"
#include <sys/time.h>

TEST(test_require_clojure_string_time) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Temporarily enable time output for this test
    set_suppress_time_output(false);
    
    // Test: (time (require 'clojure.string)) - measure load time using Clojure's time function
    // time prints the elapsed time and returns the result
    fprintf(stderr, "\n[PERF] Measuring require 'clojure.string with (time):\n");
    CljObject *result = eval_string("(time (require 'clojure.string))", g_test_eval_state);
    (void)result; // require returns nil, time returns the result
    
    // Re-disable time output
    set_suppress_time_output(true);
    
    // Verify that clojure.string namespace exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL(string_ns);
    TEST_ASSERT_NOT_NULL(string_ns->mappings);
}

TEST(test_require_clojure_string_performance) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Temporarily enable time output for this test
    set_suppress_time_output(false);
    
    // Measure require time multiple times
    fprintf(stderr, "\n[PERF] Measuring require 'clojure.string load time (5 runs):\n");
    for (int i = 0; i < 5; i++) {
        // Use (time) to measure - output will show elapsed time
        CljObject *result = eval_string("(time (require 'clojure.string))", g_test_eval_state);
        (void)result;
    }
    
    // Re-disable time output
    set_suppress_time_output(true);
    
    // Verify namespace exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL(string_ns);
    TEST_ASSERT_NOT_NULL(string_ns->mappings);
}

TEST(test_require_clojure_string_multiple_times) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Measure multiple requires
    double times[5];
    for (int i = 0; i < 5; i++) {
        struct timeval start, end;
        gettimeofday(&start, NULL);
        
        CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
        (void)req_result;
        
        gettimeofday(&end, NULL);
        times[i] = (end.tv_sec - start.tv_sec) * 1000.0 + 
                   (end.tv_usec - start.tv_usec) / 1000.0;
    }
    
    double total = 0;
    for (int i = 0; i < 5; i++) {
        total += times[i];
        fprintf(stderr, "[PERF] require #%d: %.2f ms\n", i+1, times[i]);
    }
    double avg = total / 5.0;
    fprintf(stderr, "[PERF] Average require time: %.2f ms\n", avg);
    
    // Test passes
    TEST_ASSERT_TRUE(true);
}

