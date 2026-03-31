#!/bin/bash

# Fair comparison of naive recursive Fibonacci between Clojure and tiny-clj
# Uses the same algorithm, same number of iterations, and same (time) function on both sides

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
RESULTS_DIR="./benchmark_results"
TIMEOUT_SECONDS=60

mkdir -p "$RESULTS_DIR"

echo -e "${BLUE}🔬 Naive Recursive Fibonacci Benchmark Comparison${NC}"
echo "=================================================="
echo ""
echo "Algorithm: fib(n) = if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))"
echo "Test: 5 iterations of fib(20)"
echo "Measurement: Both sides use (time) function"
echo ""

# Find executables
if ! command -v clojure &> /dev/null; then
    echo -e "${RED}❌ Clojure not found. Please install Clojure CLI.${NC}"
    exit 1
fi

# Ensure canonical build directory exists
BUILD_DIR="./build"
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}⚠️  Build directory not found. Creating build...${NC}"
fi

# Configure Release build in canonical build dir
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DENABLE_LTO=OFF

# Build Release version
echo -e "${YELLOW}🔨 Building Release version of tiny-clj-repl...${NC}"
(cmake --build "$BUILD_DIR" --target tiny-clj-repl)

TINY_CLJ_PATH="$BUILD_DIR/tiny-clj-repl"
if [ ! -f "$TINY_CLJ_PATH" ]; then
    echo -e "${RED}❌ Could not build tiny-clj-repl in Release mode${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Release build ready${NC}"
echo ""

# Function to extract time from (time) output
# Clojure outputs: "Elapsed time: X.XX msecs" (with quotes in some contexts)
# tiny-clj outputs: "Elapsed time: X.XX msecs" (without quotes, may have "nil" after)
extract_time_output() {
    local output_file="$1"
    # Try to extract time - handle both quoted and unquoted formats
    # Match: "Elapsed time: 123.45 msecs" or Elapsed time: 123.45 msecs
    local time_ms=$(grep -E "Elapsed time:" "$output_file" | sed -E 's/.*Elapsed time: ([0-9.]+) msecs.*/\1/' | head -1)
    # If that didn't work, try with quotes
    if [ -z "$time_ms" ]; then
        time_ms=$(grep -E '"Elapsed time:' "$output_file" | sed -E 's/.*"Elapsed time: ([0-9.]+) msecs".*/\1/' | head -1)
    fi
    # Remove any trailing characters (like "nil" that might be on the same line)
    time_ms=$(echo "$time_ms" | sed 's/[^0-9.]//g')
    echo "$time_ms"
}

# Test Clojure
echo -e "${BLUE}📊 Testing Clojure (JIT-optimized)${NC}"
echo "----------------------------------------"
echo -e "${YELLOW}⏱️  Running Clojure benchmark...${NC}"
timeout "$TIMEOUT_SECONDS" clojure "$BENCHMARK_FILE" > "$RESULTS_DIR/clojure_fibonacci_naive.log" 2>&1 || true
CLOJURE_TIME=$(extract_time_output "$RESULTS_DIR/clojure_fibonacci_naive.log")

if [ -z "$CLOJURE_TIME" ]; then
    echo -e "${RED}❌ Could not extract time from Clojure output${NC}"
    echo "Output:"
    cat "$RESULTS_DIR/clojure_fibonacci_naive.log"
    CLOJURE_TIME="0"
else
    echo -e "${GREEN}✅ Clojure: ${CLOJURE_TIME}ms${NC}"
fi
echo ""

# Test tiny-clj
echo -e "${BLUE}📊 Testing tiny-clj (interpreter)${NC}"
echo "----------------------------------------"
echo -e "${YELLOW}⏱️  Running tiny-clj benchmark...${NC}"
# Use -f flag to execute file directly (non-interactive mode)
timeout "$TIMEOUT_SECONDS" "$TINY_CLJ_PATH" -f "$BENCHMARK_FILE" > "$RESULTS_DIR/tiny_clj_fibonacci_naive.log" 2>&1 || true
TINY_CLJ_TIME=$(extract_time_output "$RESULTS_DIR/tiny_clj_fibonacci_naive.log")

if [ -z "$TINY_CLJ_TIME" ]; then
    echo -e "${RED}❌ Could not extract time from tiny-clj output${NC}"
    echo "Output:"
    cat "$RESULTS_DIR/tiny_clj_fibonacci_naive.log"
    TINY_CLJ_TIME="0"
else
    echo -e "${GREEN}✅ tiny-clj: ${TINY_CLJ_TIME}ms${NC}"
fi
echo ""

# Calculate ratio
if [ -n "$CLOJURE_TIME" ] && [ -n "$TINY_CLJ_TIME" ] && [ "$CLOJURE_TIME" != "0" ] && [ "$TINY_CLJ_TIME" != "0" ]; then
    RATIO=$(echo "scale=2; $TINY_CLJ_TIME / $CLOJURE_TIME" | bc -l)
    
    echo -e "${BLUE}📈 Comparison Results${NC}"
    echo "=================================================="
    echo "Clojure:  ${CLOJURE_TIME}ms (from (time) output)"
    echo "tiny-clj: ${TINY_CLJ_TIME}ms (from (time) output)"
    echo ""
    echo -e "${YELLOW}Ratio: tiny-clj is ${RATIO}x slower than Clojure${NC}"
    echo ""
    
    if (( $(echo "$RATIO > 1000" | bc -l) )); then
        echo -e "${RED}⚠️  WARNING: Ratio > 1000x - this seems unusually high!${NC}"
        echo "   Possible causes:"
        echo "   - Debug build with profiling enabled"
        echo "   - Memory profiler overhead"
        echo "   - Build configuration differences"
    elif (( $(echo "$RATIO > 100" | bc -l) )); then
        echo -e "${YELLOW}⚠️  Ratio > 100x - higher than expected for interpreter vs JIT${NC}"
        echo "   Expected range: 10-100x for interpreter vs JIT-optimized code"
    elif (( $(echo "$RATIO > 20" | bc -l) )); then
        echo -e "${GREEN}✓ Ratio is in expected range for interpreter vs JIT (20-100x)${NC}"
    else
        echo -e "${GREEN}✓ Ratio seems reasonable (< 20x)${NC}"
    fi
    
    # Save results
    echo ""
    echo "Saving results to $RESULTS_DIR/fibonacci_naive_comparison.txt"
    cat > "$RESULTS_DIR/fibonacci_naive_comparison.txt" << EOF
Naive Recursive Fibonacci Benchmark Comparison
===============================================
Date: $(date)
Algorithm: fib(n) = if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))
Test: 5 iterations of fib(20)
Measurement: Both sides use (time) function

Results:
--------
Clojure:  ${CLOJURE_TIME}ms
tiny-clj: ${TINY_CLJ_TIME}ms

Ratio: tiny-clj is ${RATIO}x slower than Clojure

Notes:
- Both use the same naive recursive algorithm
- Both use the same (time) function for measurement
- Clojure benefits from JIT optimization
- tiny-clj runs as an interpreter
- Ratio > 1000x may indicate profiling/debug overhead
- Expected ratio for interpreter vs JIT: 10-100x
EOF
    
    echo -e "${GREEN}✅ Results saved to $RESULTS_DIR/fibonacci_naive_comparison.txt${NC}"
    echo ""
    echo "Full output logs:"
    echo "  Clojure:  $RESULTS_DIR/clojure_fibonacci_naive.log"
    echo "  tiny-clj: $RESULTS_DIR/tiny_clj_fibonacci_naive.log"
else
    echo -e "${RED}❌ Could not calculate comparison (missing or invalid time data)${NC}"
    echo ""
    echo "Clojure output:"
    [ -f "$RESULTS_DIR/clojure_fibonacci_naive.log" ] && cat "$RESULTS_DIR/clojure_fibonacci_naive.log"
    echo ""
    echo "tiny-clj output:"
    [ -f "$RESULTS_DIR/tiny_clj_fibonacci_naive.log" ] && cat "$RESULTS_DIR/tiny_clj_fibonacci_naive.log"
fi
