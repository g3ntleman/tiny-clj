/*
 * Performance Tests for (let) bindings in Tiny-CLJ
 * 
 * Measures:
 * 1. let-Erstellungszeit (mit clojure.core-Kopieren)
 * 2. Symbol-Auflösungszeit in verschachtelten let-Blöcken
 * 
 * Baseline-Messung VOR Optimierung (Entfernen des clojure.core-Kopierens)
 */

#include "tests_common.h"
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>

enum {
    LET_PERF_MAX_DEPTH = 10
};

// Helper: Get current time in microseconds
static long get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000L + tv.tv_usec;
}

// Helper: Measure let creation time
static double measure_let_creation_time(int iterations) {
    long total_time = 0;
    
    for (int i = 0; i < iterations; i++) {
        long start = get_time_us();
        
        WITH_AUTORELEASE_POOL({
            // Simple let expression
            const char *code = "(let [x 10] x)";
            CljValue result = eval_string(code, g_test_eval_state);
            (void)result;  // Suppress unused warning
        });
        
        long end = get_time_us();
        total_time += (end - start);
    }
    
    return (double)total_time / iterations / 1000.0;  // Return in milliseconds
}

// Helper: Build nested let expression string
// For depth=3: (let [a3 3] (let [a2 2] (let [a1 1] a1)))
static void build_nested_let(char *buffer, size_t buffer_size, int depth) {
    if (depth <= 0) {
        buffer[0] = '\0';
        return;
    }
    
    size_t pos = 0;
    
    // Build nested lets from outer to inner
    for (int i = depth; i >= 1; i--) {
        int written = clj_mini_snprintf(buffer + pos, buffer_size - pos,
                              "(let [a%d %d] ", i, i);
        if (written < 0 || (size_t)written >= buffer_size - pos) break;
        pos += written;
    }
    
    // Body: access innermost variable
    int written = clj_mini_snprintf(buffer + pos, buffer_size - pos, "a1");
    if (written > 0 && (size_t)written < buffer_size - pos) {
        pos += written;
    }
    
    // Close all let blocks
    for (int i = 0; i < depth; i++) {
        written = clj_mini_snprintf(buffer + pos, buffer_size - pos, ")");
        if (written < 0 || (size_t)written >= buffer_size - pos) break;
        pos += written;
    }
}

// Helper: Measure symbol resolution time in nested lets
static double measure_nested_let_resolution_time(int depth, int iterations) {
    char code[2048];
    build_nested_let(code, sizeof(code), depth);
    
    long total_time = 0;
    
    for (int i = 0; i < iterations; i++) {
        long start = get_time_us();
        
        WITH_AUTORELEASE_POOL({
            CljValue result = eval_string(code, g_test_eval_state);
            (void)result;  // Suppress unused warning
        });
        
        long end = get_time_us();
        total_time += (end - start);
    }
    
    return (double)total_time / iterations / 1000.0;  // Return in milliseconds
}

// Helper: Write results to file
static void write_baseline_results(const char *filename, 
                                   double *let_creation_times,
                                   double *nested_resolution_times,
                                   int max_depth) {
    // Ensure directory exists
    const char *dir = "benchmark_results";
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        mkdir(dir, 0700);
    }
    
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        // File open failed - silently return (no fprintf output in tests)
        return;
    }
    
    test_fprintf(fp, "# Performance Baseline: let-Bindungen (VOR Optimierung)\n");
    test_fprintf(fp, "# Datum: %s\n", __DATE__);
    test_fprintf(fp, "# Zeit: %s\n", __TIME__);
    test_fprintf(fp, "\n");
    
    test_fprintf(fp, "## let-Erstellungszeit (mit clojure.core-Kopieren)\n");
    test_fprintf(fp, "Iterationen,Zeit_ms\n");
    test_fprintf(fp, "10,%f\n", let_creation_times[0]);
    test_fprintf(fp, "100,%f\n", let_creation_times[1]);
    test_fprintf(fp, "1000,%f\n", let_creation_times[2]);
    test_fprintf(fp, "\n");
    
    test_fprintf(fp, "## Symbol-Auflösungszeit in verschachtelten let-Blöcken\n");
    test_fprintf(fp, "Verschachtelungstiefe,Zeit_ms\n");
    for (int i = 1; i <= max_depth; i++) {
        test_fprintf(fp, "%d,%f\n", i, nested_resolution_times[i - 1]);
    }
    
    fclose(fp);
}

// ============================================================================
// TEST: Baseline - let-Erstellungszeit messen
// ============================================================================
TEST(test_let_creation_time_baseline) {
    WITH_AUTORELEASE_POOL({
        double times[3];
        
        // Measure with different iteration counts
        times[0] = measure_let_creation_time(10);
        times[1] = measure_let_creation_time(100);
        times[2] = measure_let_creation_time(1000);
        
        // Store results (will be written to file in main test)
        // For now, just verify measurements are reasonable (> 0)
        TEST_ASSERT_TRUE(times[0] > 0);
        TEST_ASSERT_TRUE(times[1] > 0);
        TEST_ASSERT_TRUE(times[2] > 0);
        
        // Performance output removed for silent test execution
    });
}

// ============================================================================
// TEST: Baseline - Symbol resolution time in nested let blocks
// ============================================================================
TEST(test_nested_let_resolution_time_baseline) {
    WITH_AUTORELEASE_POOL({
        const int max_depth = LET_PERF_MAX_DEPTH;
        const int iterations = 100;
        double times[LET_PERF_MAX_DEPTH];
        
        // Measure resolution time for different nesting depths
        for (int depth = 1; depth <= max_depth; depth++) {
            times[depth - 1] = measure_nested_let_resolution_time(depth, iterations);
        }
        
        // Verify measurements are reasonable
        for (int i = 0; i < max_depth; i++) {
            TEST_ASSERT_TRUE(times[i] > 0);
        }
        
        // Performance output removed for silent test execution
    });
}

// ============================================================================
// TEST: Baseline - Gesamte Performance-Messung und Speicherung
// ============================================================================
TEST(test_let_performance_baseline_complete) {
    WITH_AUTORELEASE_POOL({
        // Measure let creation times
        double let_creation_times[3];
        let_creation_times[0] = measure_let_creation_time(10);
        let_creation_times[1] = measure_let_creation_time(100);
        let_creation_times[2] = measure_let_creation_time(1000);
        
        // Measure nested let resolution times
        const int max_depth = LET_PERF_MAX_DEPTH;
        const int iterations = 100;
        double nested_resolution_times[LET_PERF_MAX_DEPTH];
        
        for (int depth = 1; depth <= max_depth; depth++) {
            nested_resolution_times[depth - 1] = measure_nested_let_resolution_time(depth, iterations);
        }
        
        // Write results to file (baseline or after optimization)
        const char *filename = "benchmark_results/let_performance_baseline.csv";
        // Check if baseline exists - if so, this is the "after" measurement
        FILE *baseline_check = fopen(filename, "r");
        if (baseline_check) {
            fclose(baseline_check);
            filename = "benchmark_results/let_performance_after.csv";
        }
        
        write_baseline_results(filename,
                              let_creation_times,
                              nested_resolution_times,
                              max_depth);
        
        // Performance output removed for silent test execution
        
        // Verify file was created
        FILE *fp = fopen(filename, "r");
        TEST_ASSERT_NOT_NULL_MESSAGE(fp, "Ergebnisdatei sollte erstellt worden sein");
        if (fp) fclose(fp);
    });
}

