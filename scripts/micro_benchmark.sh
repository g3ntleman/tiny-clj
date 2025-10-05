#!/bin/bash
# Micro-benchmark script to identify specific performance bottlenecks

set -e

echo "=== Tiny-CLJ Micro-Benchmark Analysis ==="

# Create temporary benchmark file
cat > temp_micro_benchmark.c << 'EOF'
#include <stdio.h>
#include <time.h>
#include "src/utf8.h"

#define ITERATIONS 1000000

int main() {
    printf("=== UTF-8 Performance Micro-Benchmarks ===\n");
    
    // Benchmark 1: ASCII symbol character check
    clock_t start = clock();
    for (int i = 0; i < ITERATIONS; i++) {
        utf8_is_symbol_char('a');
        utf8_is_symbol_char('Z');
        utf8_is_symbol_char('9');
        utf8_is_symbol_char('-');
    }
    clock_t end = clock();
    double ascii_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("ASCII symbol check: %.6f ms (%.2f ns/call)\n", 
           ascii_time * 1000, (ascii_time * 1000000000) / (ITERATIONS * 4));
    
    // Benchmark 2: Unicode symbol character check
    start = clock();
    for (int i = 0; i < ITERATIONS; i++) {
        utf8_is_symbol_char(0x00E4); // ä
        utf8_is_symbol_char(0x00F6); // ö
        utf8_is_symbol_char(0x00FC); // ü
        utf8_is_symbol_char(0x2713); // ✓
    }
    end = clock();
    double unicode_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Unicode symbol check: %.6f ms (%.2f ns/call)\n", 
           unicode_time * 1000, (unicode_time * 1000000000) / (ITERATIONS * 4));
    
    // Benchmark 3: UTF-8 validation
    const char *test_strings[] = {
        "hello",
        "Grüße",
        "✓ check",
        "café",
        NULL
    };
    
    start = clock();
    for (int i = 0; i < ITERATIONS / 10; i++) {
        for (int j = 0; test_strings[j] != NULL; j++) {
            utf8valid(test_strings[j]);
        }
    }
    end = clock();
    double valid_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("UTF-8 validation: %.6f ms (%.2f ns/call)\n", 
           valid_time * 1000, (valid_time * 1000000000) / (ITERATIONS / 10 * 4));
    
    printf("\n=== OPTIMIZATION TARGETS ===\n");
    if (unicode_time > ascii_time * 2) {
        printf("⚠️  Unicode checks are %.1fx slower than ASCII\n", unicode_time / ascii_time);
        printf("   Consider lookup table for common Unicode ranges\n");
    }
    
    if (valid_time > ascii_time * 10) {
        printf("⚠️  UTF-8 validation is %.1fx slower than simple checks\n", valid_time / ascii_time);
        printf("   Consider caching validation results\n");
    }
    
    return 0;
}
EOF

# Compile and run micro-benchmark
echo "Compiling micro-benchmark..."
gcc -O2 -I. temp_micro_benchmark.c -o temp_micro_benchmark

echo "Running micro-benchmark..."
./temp_micro_benchmark

# Cleanup
rm -f temp_micro_benchmark.c temp_micro_benchmark

echo ""
echo "=== PERFORMANCE RECOMMENDATIONS ==="
echo "1. If Unicode checks are slow: Implement lookup table"
echo "2. If validation is slow: Cache results or lazy validation"
echo "3. If symbol checks are slow: Optimize range checks"
echo "4. Consider SIMD instructions for bulk operations"
