#!/bin/bash
# Sample profiling for fib(30) using the profiling build
set -e

BUILD_DIR="build"
OUTPUT_DIR="benchmark_results"
OUTPUT_FILE="$OUTPUT_DIR/sample_fib30_$(date +%Y%m%d_%H%M%S).txt"

# Ensure output directory exists
mkdir -p "$OUTPUT_DIR"

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

# Start sampler FIRST to avoid race with short-lived benchmarks.
# sample attaches by (partial) process name and waits until it exists.
sample "tiny-clj-profile" 10 1 -wait -mayDie -file "$OUTPUT_FILE" >/dev/null 2>&1 &
SAMPLE_PID=$!

# Run the benchmark long enough to get stable samples.
# NOTE: benchmarks/fibonacci_30.clj already runs fib(30) a few times; we add extra iterations here.
"$BUILD_DIR/tiny-clj-profile" -e '(do (load-file "benchmarks/fibonacci_30.clj") (dotimes [_ 30] (fib 30)))'

# Wait for sampling to complete (or finish early if the process exits)
wait "$SAMPLE_PID" 2>/dev/null || true

echo ""
echo "=== Top functions in sample output ==="
grep -E "^\s+[0-9]+ " "$OUTPUT_FILE" | head -30 || echo "(no matching lines)"

echo ""
echo "Full output saved to: $OUTPUT_FILE"
