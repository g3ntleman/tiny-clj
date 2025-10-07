#!/bin/bash
# Performance Profiling Script for Tiny-CLJ

set -e

echo "=== Tiny-CLJ Performance Profiler ==="

# Check if gprof is available
if ! command -v gprof &> /dev/null; then
    echo "gprof not found. Installing profiling tools..."
    if command -v brew &> /dev/null; then
        brew install gprof
    elif command -v apt-get &> /dev/null; then
        sudo apt-get install binutils
    fi
fi

# Build with profiling enabled
echo "Building with profiling enabled..."
cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPROFILING=ON
make clean
make -j

# Run benchmark with profiling
echo "Running performance tests (optimized build)..."
./test-performance > benchmark_profile.log 2>&1

# Generate profile report
echo "Generating profile report..."
gprof test-benchmark gmon.out > profile_report.txt

# Analyze hotspots
echo "=== TOP 10 PERFORMANCE HOTSPOTS ==="
grep -A 15 "time   seconds" profile_report.txt | head -20

echo "=== PROFILE REPORT GENERATED ==="
echo "Full report: profile_report.txt"
echo "Benchmark log: benchmark_profile.log"

# Suggest optimizations
echo ""
echo "=== OPTIMIZATION SUGGESTIONS ==="
echo "1. Focus on functions with highest time percentage"
echo "2. Look for functions called many times"
echo "3. Consider inlining frequently called small functions"
echo "4. Cache expensive computations"
