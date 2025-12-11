#!/bin/bash

# Create baseline sample profile for fibonacci benchmark
# This will be used as reference before optimizations

set -e

# Change to project root
cd "$(dirname "$0")/.."

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

BENCHMARK_FILE="./benchmarks/fibonacci_naive.clj"
DURATION=30  # 30 seconds should be enough for multiple fib(20) runs

echo -e "${BLUE}🔬 Creating Baseline Sample Profile${NC}"
echo "=================================================="
echo ""
echo "Benchmark: $BENCHMARK_FILE"
echo "Sampling duration: ${DURATION} seconds"
echo ""

# Find tiny-clj executable
TINY_CLJ_PATH="./build/tiny-clj-repl"
if [ ! -f "$TINY_CLJ_PATH" ]; then
    echo -e "${YELLOW}⚠️  tiny-clj-repl not found. Building...${NC}"
    cmake --build build --target tiny-clj-repl
fi

if [ ! -f "$TINY_CLJ_PATH" ]; then
    echo -e "${RED}❌ Could not build tiny-clj-repl${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Found tiny-clj-repl: $TINY_CLJ_PATH${NC}"
echo ""

# Create output filename with timestamp
OUTPUT_FILE="sample_baseline_$(date +%Y%m%d_%H%M%S).txt"
echo "Output file: $OUTPUT_FILE"
echo ""

# Start tiny-clj in background
echo "Starting tiny-clj benchmark..."
"$TINY_CLJ_PATH" -f "$BENCHMARK_FILE" > /dev/null 2>&1 &
TINY_CLJ_PID=$!

# Give it a moment to start
sleep 0.5

# Start sample profiling
echo "Starting sample profiler for ${DURATION} seconds..."
sample "$TINY_CLJ_PID" "$DURATION" -f "$OUTPUT_FILE" 2>&1 | tee /tmp/sample_output.log

# Wait for tiny-clj to finish (it should finish before sample ends)
wait "$TINY_CLJ_PID" 2>/dev/null || true

echo ""
if [ -f "$OUTPUT_FILE" ]; then
    FILE_SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
    echo -e "${GREEN}✅ Baseline sample created successfully!${NC}"
    echo "   File: $OUTPUT_FILE"
    echo "   Size: $FILE_SIZE"
    echo ""
    echo "To view the profile:"
    echo "  less $OUTPUT_FILE"
    echo "  or: open -a TextEdit $OUTPUT_FILE"
else
    echo -e "${RED}❌ Sample file was not created${NC}"
    exit 1
fi



