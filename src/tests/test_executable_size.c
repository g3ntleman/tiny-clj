#include "../unity.h"
#include "../executable_size.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Cleanup after each test
}

void test_executable_size_measurement(void) {
    printf("\n=== Testing Executable Size Measurement ===\n");
    
    // Measure current executable sizes
    measure_executable_sizes();
    
    // Verify we got some measurements
    TEST_ASSERT_TRUE(g_size_measurement_count > 0);
    
    printf("Measured %d executables:\n", g_size_measurement_count);
    for (int i = 0; i < g_size_measurement_count; i++) {
        printf("  %s: %llu bytes (%.1f KB)\n", 
               g_size_measurements[i].name,
               g_size_measurements[i].size_bytes,
               g_size_measurements[i].size_bytes / 1024.0);
    }
    
    // Check that main executable exists and has reasonable size
    bool found_main = false;
    for (int i = 0; i < g_size_measurement_count; i++) {
        if (strcmp(g_size_measurements[i].name, "tiny-clj") == 0) {
            found_main = true;
            // Main executable should be at least 100KB
            TEST_ASSERT_TRUE(g_size_measurements[i].size_bytes > 100 * 1024);
            break;
        }
    }
    TEST_ASSERT_TRUE(found_main);
}

void test_size_analysis_printing(void) {
    printf("\n=== Testing Size Analysis Printing ===\n");
    
    measure_executable_sizes();
    print_size_analysis();
    
    // Test should complete without crashing
    TEST_ASSERT_TRUE(g_size_measurement_count >= 0);
}

void test_size_history_export(void) {
    printf("\n=== Testing Size History Export ===\n");
    
    measure_executable_sizes();
    export_size_history_csv();
    
    // Verify CSV file was created
    FILE *file = fopen("executable_size_history.csv", "r");
    TEST_ASSERT_NOT_NULL(file);
    if (file) {
        fclose(file);
    }
}

void test_size_regression_detection(void) {
    printf("\n=== Testing Size Regression Detection ===\n");
    
    measure_executable_sizes();
    detect_size_regressions();
    
    // Test should complete without crashing
    TEST_ASSERT_TRUE(g_size_measurement_count >= 0);
}

void test_individual_executable_size(void) {
    printf("\n=== Testing Individual Executable Size ===\n");
    
    uint64_t size = get_executable_size("tiny-clj");
    printf("tiny-clj size: %llu bytes (%.1f KB)\n", size, size / 1024.0);
    
    // Should have a reasonable size (between 50KB and 10MB)
    TEST_ASSERT_TRUE(size > 50 * 1024);
    TEST_ASSERT_TRUE(size < 10 * 1024 * 1024);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_executable_size_measurement);
    RUN_TEST(test_size_analysis_printing);
    RUN_TEST(test_size_history_export);
    RUN_TEST(test_size_regression_detection);
    RUN_TEST(test_individual_executable_size);
    
    return UNITY_END();
}
