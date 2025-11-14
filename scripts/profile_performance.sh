#!/bin/bash
# Performance Profiling Script for Tiny-CLJ

set -e

echo "=== Tiny-CLJ Performance Profiler ==="

# Detect OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS - use sample or Instruments
    echo "macOS detected - using sample for profiling"
    USE_SAMPLE=true
else
    # Linux - use gprof
    echo "Linux detected - using gprof for profiling"
    USE_SAMPLE=false
    
    # Check if gprof is available
    if ! command -v gprof &> /dev/null; then
        echo "gprof not found. Installing profiling tools..."
        if command -v apt-get &> /dev/null; then
            sudo apt-get install binutils
        fi
    fi
fi

# Build directory
BUILD_DIR="build-debug-noasan"

# Build with profiling enabled (debug build without ASAN for profiling)
echo "Building with profiling enabled..."
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=OFF
fi
cmake --build "$BUILD_DIR" -j -t unity-tests

# Run benchmark with profiling
echo "Running slow fibonacci test with profiling..."
TEST_BINARY="$BUILD_DIR/unity-tests"
TEST_NAME="test_memory_leak_fibonacci_reproduction"

if [ "$USE_SAMPLE" = true ]; then
    # macOS: Use sample
    echo "Profiling with sample (10 seconds)..."
    echo "Starting test in background..."
    "$TEST_BINARY" --test "$TEST_NAME" > benchmark_profile.log 2>&1 &
    TEST_PID=$!
    
    # Sample the running process
    sample "$TEST_PID" 10 -f benchmark_profile_sample.txt > /dev/null 2>&1
    
    # Wait for test to finish
    wait $TEST_PID 2>/dev/null || true
    
    echo "=== SAMPLE PROFILE GENERATED ==="
    echo "Profile saved to: benchmark_profile_sample.txt"
    echo ""
    echo "=== TOP FUNCTIONS (from sample) ==="
    if [ -f benchmark_profile_sample.txt ]; then
        grep -E "^[0-9]+\s+[0-9]+\.[0-9]+%" benchmark_profile_sample.txt | head -20 || echo "No sample data found in expected format"
        echo ""
        echo "View full profile: less benchmark_profile_sample.txt"
    else
        echo "Warning: benchmark_profile_sample.txt not found"
    fi
else
    # Linux: Use gprof
    echo "Running test with gprof..."
    "$TEST_BINARY" --test "$TEST_NAME" > benchmark_profile.log 2>&1
    
    # Generate profile report
    echo "Generating profile report..."
    if [ -f gmon.out ]; then
        gprof "$TEST_BINARY" gmon.out > profile_report.txt
        echo "=== TOP 10 PERFORMANCE HOTSPOTS ==="
        grep -A 15 "time   seconds" profile_report.txt | head -20
        echo ""
        echo "=== PROFILE REPORT GENERATED ==="
        echo "Full report: profile_report.txt"
    else
        echo "Warning: gmon.out not found. Make sure binary was built with -pg flag."
    fi
fi

echo "Benchmark log: benchmark_profile.log"

# Suggest optimizations
echo ""
echo "=== OPTIMIZATION SUGGESTIONS ==="
echo "1. Focus on functions with highest time percentage"
echo "2. Look for functions called many times"
echo "3. Consider inlining frequently called small functions"
echo "4. Cache expensive computations"
