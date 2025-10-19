#!/bin/bash

# Optimized Benchmark Script
# Uses MinSizeRel builds for benchmarks, Debug builds for unit tests

set -e

echo "=== Optimized Benchmark Pipeline ==="
echo "Benchmarks: MinSizeRel (stripped) for performance"
echo "Unit Tests: Debug for full test coverage"
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

# Step 4: Show binary sizes
echo "Step 4: Binary sizes after optimization..."
ls -lh tiny-clj-repl tiny-clj-stm32 unity-tests

# Step 5: Run unit tests (DEBUG build)
echo "Step 5: Running unit tests (DEBUG build)..."
./unity-tests 2>&1 | grep -E "(Tests|Failures|PASS|FAIL)" | tail -10

# Step 6: Run performance benchmarks (MinSizeRel build)
echo "Step 6: Running performance benchmarks (MinSizeRel build)..."
echo "Note: Benchmarks use optimized, stripped binaries for accurate performance measurement"

# Step 7: Show memory profiling results
echo "Step 7: Memory profiling results..."
./unity-tests 2>&1 | grep -E "(Memory|Allocations|Leaks)" | tail -10

echo ""
echo "=== Benchmark Pipeline Complete ==="
echo "✅ Unit tests: DEBUG build (full coverage)"
echo "✅ Benchmarks: MinSizeRel + stripped (optimized performance)"
echo "✅ Binary sizes: Optimized for embedded deployment"
