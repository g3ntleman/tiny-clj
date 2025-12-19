#!/bin/bash
# Sample profiling for fib(30) using the profiling build
set -e

BUILD_DIR="build"
OUTPUT_FILE="sample_fib30_$(date +%Y%m%d_%H%M%S).txt"

echo "=== Building profiling build (ENABLE_PROFILING) ==="

# Clean rebuild of tiny-clj-profile to ensure ENABLE_PROFILING is active
rm -f "$BUILD_DIR/tiny-clj-profile"
rm -f "$BUILD_DIR/CMakeFiles/tiny-clj-profile.dir/src/"*.o 2>/dev/null || true

# Configure and build
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j --target tiny-clj-profile

# Verify the binary exists
if [ ! -f "$BUILD_DIR/tiny-clj-profile" ]; then
    echo "ERROR: Failed to build tiny-clj-profile"
    exit 1
fi

echo "✅ Profiling build ready: $BUILD_DIR/tiny-clj-profile"

echo ""
echo "=== Running sample on fib(30) ==="
echo "Output: $OUTPUT_FILE"
echo ""

# Start the benchmark in background
"$BUILD_DIR/tiny-clj-profile" -e '(load-file "benchmarks/fibonacci_30.clj")' &
PID=$!

# Give it a moment to start
sleep 0.2

# Sample the running process (10 seconds, 1ms interval)
# -mayDie: process may exit before sampling completes
if sample "$PID" 10 -file "$OUTPUT_FILE" -mayDie 2>/dev/null; then
    echo "✅ Sampling completed"
else
    echo "⚠️  Process finished before sampling completed (this is OK for fast benchmarks)"
fi

# Wait for the benchmark to finish
wait "$PID" 2>/dev/null || true

echo ""
echo "=== Top functions in sample output ==="
grep -E "^\s+[0-9]+ " "$OUTPUT_FILE" | head -30 || echo "(no matching lines)"

echo ""
echo "Full output saved to: $OUTPUT_FILE"
