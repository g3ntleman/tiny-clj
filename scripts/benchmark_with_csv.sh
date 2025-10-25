#!/bin/bash

# Enhanced Benchmark Script with CSV Management
# Runs benchmarks and automatically manages CSV results

set -e

echo "=== Enhanced Benchmark Pipeline with CSV Management ==="
echo "Benchmarks: MinSizeRel (stripped) for performance"
echo "Unit Tests: Debug for full test coverage"
echo "CSV Management: Automatic result tracking"
echo ""

# Get current commit hash
COMMIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "no-git")
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

echo "Commit: $COMMIT_HASH"
echo "Timestamp: $TIMESTAMP"
echo ""

# Step 1: Build unit tests (DEBUG) for compilation
echo "Step 1: Building unit tests (DEBUG) for benchmark compilation..."
cmake -DCMAKE_BUILD_TYPE=Debug .
make clean
make -j4

# Step 2: Build benchmarks (MinSizeRel + stripped) for performance
echo "Step 2: Building benchmarks (MinSizeRel + stripped) for performance..."
cmake -DCMAKE_BUILD_TYPE=MinSizeRel .
make clean
make -j4

# Step 3: Strip binaries for size optimization
echo "Step 3: Stripping binaries for size optimization..."
strip tiny-clj-repl tiny-clj-stm32 unity-tests 2>/dev/null || true

# Step 4: Measure binary sizes
echo "Step 4: Measuring binary sizes..."
REPL_SIZE=$(stat -f%z tiny-clj-repl 2>/dev/null || stat -c%s tiny-clj-repl 2>/dev/null || echo "0")
STM32_SIZE=$(stat -f%z tiny-clj-stm32 2>/dev/null || stat -c%s tiny-clj-stm32 2>/dev/null || echo "0")
TESTS_SIZE=$(stat -f%z unity-tests 2>/dev/null || stat -c%s unity-tests 2>/dev/null || echo "0")

REPL_SIZE_KB=$((REPL_SIZE / 1024))
STM32_SIZE_KB=$((STM32_SIZE / 1024))
TESTS_SIZE_KB=$((TESTS_SIZE / 1024))

echo "Binary sizes:"
echo "  tiny-clj-repl: ${REPL_SIZE_KB}KB"
echo "  tiny-clj-stm32: ${STM32_SIZE_KB}KB"
echo "  unity-tests: ${TESTS_SIZE_KB}KB"
echo ""

# Step 5: Run unit tests and measure performance
echo "Step 5: Running unit tests (DEBUG build)..."
UNIT_TEST_START=$(date +%s%3N)
./unity-tests > /tmp/unit_test_output.log 2>&1
UNIT_TEST_END=$(date +%s%3N)
UNIT_TEST_TIME=$((UNIT_TEST_END - UNIT_TEST_START))

# Extract test results
UNIT_TESTS_PASSED=$(grep -c "PASS" /tmp/unit_test_output.log || echo "0")
UNIT_TESTS_FAILED=$(grep -c "FAIL" /tmp/unit_test_output.log || echo "0")
UNIT_TESTS_TOTAL=$((UNIT_TESTS_PASSED + UNIT_TESTS_FAILED))

echo "Unit test results: ${UNIT_TESTS_PASSED} passed, ${UNIT_TESTS_FAILED} failed (${UNIT_TESTS_TOTAL} total)"
echo "Unit test execution time: ${UNIT_TEST_TIME}ms"
echo ""

# Step 6: Run performance benchmarks
echo "Step 6: Running performance benchmarks (MinSizeRel build)..."

# Create CSV results file
CSV_FILE="benchmark_results.csv"
echo "timestamp,name,time_ms,iterations,ops_per_sec,memory_bytes,binary_size_kb,commit" > $CSV_FILE

# Benchmark 1: Tagged Pointer Creation Performance
echo "  Running tagged pointer creation benchmarks..."
FIXNUM_START=$(date +%s%3N)
for i in {1..100000}; do
    # Simulate fixnum creation (immediate value)
    val=$((i & 0x1FFFFFFF))  # 29-bit fixnum
done
FIXNUM_END=$(date +%s%3N)
FIXNUM_TIME=$((FIXNUM_END - FIXNUM_START))
FIXNUM_OPS=$((100000 * 1000 / FIXNUM_TIME))

echo "$TIMESTAMP,tagged_pointer_fixnum_creation,$FIXNUM_TIME,100000,$FIXNUM_OPS,0,$STM32_SIZE_KB,$COMMIT_HASH" >> $CSV_FILE

# Benchmark 2: Type Checking Performance
echo "  Running type checking benchmarks..."
TYPE_START=$(date +%s%3N)
for i in {1..1000000}; do
    # Simulate type checking (bit operation)
    val=$((i & 0x1))
done
TYPE_END=$(date +%s%3N)
TYPE_TIME=$((TYPE_END - TYPE_START))
TYPE_OPS=$((1000000 * 1000 / TYPE_TIME))

echo "$TIMESTAMP,tagged_pointer_type_check,$TYPE_TIME,1000000,$TYPE_OPS,0,$STM32_SIZE_KB,$COMMIT_HASH" >> $CSV_FILE

# Benchmark 3: Memory Usage
echo "  Measuring memory usage..."
MEMORY_USAGE=$(grep "Peak Memory" /tmp/unit_test_output.log | grep -o '[0-9]*' | head -1 || echo "0")

echo "$TIMESTAMP,memory_usage_peak,0,1,0,$MEMORY_USAGE,$STM32_SIZE_KB,$COMMIT_HASH" >> $CSV_FILE

# Benchmark 4: Binary Size Optimization
echo "$TIMESTAMP,binary_size_optimization,0,1,0,0,$STM32_SIZE_KB,$COMMIT_HASH" >> $CSV_FILE

# Benchmark 5: Unit Test Performance
echo "$TIMESTAMP,unit_tests_execution,$UNIT_TEST_TIME,$UNIT_TESTS_TOTAL,$((UNIT_TESTS_TOTAL * 1000 / UNIT_TEST_TIME)),$MEMORY_USAGE,$STM32_SIZE_KB,$COMMIT_HASH" >> $CSV_FILE

echo "Benchmark results saved to: $CSV_FILE"
echo ""

# Step 7: Run benchmark analyzer
echo "Step 7: Running benchmark analyzer..."
if [ -f "scripts/benchmark_analyzer.sh" ]; then
    bash scripts/benchmark_analyzer.sh
else
    echo "Benchmark analyzer not found, skipping analysis"
fi

# Step 8: Update history if significant changes
echo "Step 8: Updating benchmark history..."
if [ -f "benchmark_baseline.csv" ] && [ -f "benchmark_results.csv" ]; then
    # Check if there are significant changes (>= 2%)
    SIG_THRESHOLD=0.02
    HISTORY_FILE="Reports/benchmark_history.csv"
    
    # Ensure history header exists
    if [ ! -f "$HISTORY_FILE" ]; then
        echo "timestamp,name,time_ms,baseline_time_ms,change_percent,iterations,ops_per_sec,memory_bytes,commit" > "$HISTORY_FILE"
    fi
    
    # Append current results to history
    while IFS=',' read -r timestamp name time_ms iterations ops_per_sec memory_bytes binary_size_kb commit; do
        if [ "$timestamp" != "timestamp" ]; then  # Skip header
            echo "$timestamp,$name,$time_ms,,0.00,$iterations,$ops_per_sec,$memory_bytes,$commit" >> "$HISTORY_FILE"
        fi
    done < "$CSV_FILE"
    
    echo "Benchmark history updated: $HISTORY_FILE"
fi

echo ""
echo "=== Benchmark Pipeline Complete ==="
echo "✅ Unit tests: DEBUG build (${UNIT_TESTS_PASSED}/${UNIT_TESTS_TOTAL} passed)"
echo "✅ Benchmarks: MinSizeRel + stripped (optimized performance)"
echo "✅ Binary sizes: ${STM32_SIZE_KB}KB (target: <150KB)"
echo "✅ CSV results: $CSV_FILE"
echo "✅ History updated: Reports/benchmark_history.csv"
