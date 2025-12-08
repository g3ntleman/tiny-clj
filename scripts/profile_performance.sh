#!/bin/bash
# Performance Comparison Script: tiny-clj vs Clojure/JVM
# Compares fib(20) performance with proper JVM warmup (original benchmarks-game version without recur)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Performance Comparison: tiny-clj vs Clojure/JVM ===${NC}"
echo "Benchmark: fib(20) - original benchmarks-game version (without recur)"
echo ""

# Configuration
BUILD_DIR="build"
RESULTS_DIR="benchmark_results"
HISTORY_FILE="$RESULTS_DIR/performance_history.csv"
BENCHMARK_FILE="benchmarks/fibonacci.clj"
WARMUP_SECONDS=4
ITERATIONS=100
FIB_N=20

# Create results directory
mkdir -p "$RESULTS_DIR"

# Check if Clojure is available
if command -v clojure &> /dev/null; then
    CLOJURE_AVAILABLE=true
    echo -e "${GREEN}✅ Clojure/JVM found${NC}"
else
    CLOJURE_AVAILABLE=false
    echo -e "${YELLOW}⚠️  Clojure/JVM not found - skipping Clojure benchmarks${NC}"
fi

# ============================================================================
# 1. Clean Release-Build für tiny-clj
# ============================================================================
echo -e "${BLUE}📦 Building clean Release build for tiny-clj...${NC}"
rm -rf "$BUILD_DIR"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j -t unit-tests
# Try to build tiny-clj-repl, but don't fail if it doesn't work
cmake --build "$BUILD_DIR" -j -t tiny-clj-repl 2>/dev/null || true
TEST_BINARY="$BUILD_DIR/unit-tests"

if [ ! -f "$TEST_BINARY" ]; then
    echo -e "${RED}❌ Failed to build tiny-clj${NC}"
    exit 1
fi
echo -e "${GREEN}✅ tiny-clj Release build complete${NC}"
echo ""

# ============================================================================
# 2. Clojure/JVM Benchmark with Warmup
# ============================================================================
CLOJURE_TIME_MS=0
CLOJURE_WARMUP_MS=0

if [ "$CLOJURE_AVAILABLE" = true ]; then
    echo -e "${BLUE}🔥 Running Clojure/JVM benchmark with ${WARMUP_SECONDS}s warmup...${NC}"
    
    # Create temporary Clojure script with warmup and measurement
    # Uses the same code from benchmarks/fibonacci.clj
    TEMP_CLJ=$(mktemp /tmp/fib_benchmark_XXXXXX.clj)
    cat > "$TEMP_CLJ" << CLOJURE_EOF
;; Fibonacci function from benchmarks/fibonacci.clj (original benchmarks-game version)
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

;; Warmup: ${WARMUP_SECONDS} seconds
(let [warmup-start (System/currentTimeMillis)]
  (loop []
    (dotimes [i 100] (fib ${FIB_N}))
    (if (< (- (System/currentTimeMillis) warmup-start) ${WARMUP_SECONDS}000)
      (recur)))
  (let [warmup-end (System/currentTimeMillis)
        warmup-time (- warmup-end warmup-start)]
    (println (str "WARMUP_TIME_MS=" warmup-time))))

;; Actual measurement with ${ITERATIONS} iterations
(let [iterations ${ITERATIONS}
      start (System/currentTimeMillis)]
  (dotimes [i iterations] (fib ${FIB_N}))
  (let [end (System/currentTimeMillis)
        test-time (- end start)
        time-per-iter (double (/ test-time iterations))]
    (println (str "TEST_TIME_MS=" test-time))
    (println (str "ITERATIONS=" iterations))
    (println (str "TIME_PER_ITER_MS=" time-per-iter))))
CLOJURE_EOF

    # Run Clojure benchmark
    CLJ_OUTPUT=$(clojure "$TEMP_CLJ" 2>&1)
    
    # Extract results
    CLOJURE_WARMUP_MS=$(echo "$CLJ_OUTPUT" | grep "WARMUP_TIME_MS=" | cut -d'=' -f2)
    CLOJURE_TIME_MS=$(echo "$CLJ_OUTPUT" | grep "TEST_TIME_MS=" | cut -d'=' -f2)
    ITERATIONS_CLJ=$(echo "$CLJ_OUTPUT" | grep "ITERATIONS=" | cut -d'=' -f2)
    TIME_PER_ITER_CLJ=$(echo "$CLJ_OUTPUT" | grep "TIME_PER_ITER_MS=" | cut -d'=' -f2)
    
    rm -f "$TEMP_CLJ"
    
    if [ -n "$CLOJURE_TIME_MS" ] && [ -n "$CLOJURE_WARMUP_MS" ]; then
        echo -e "${GREEN}✅ Clojure/JVM: ${CLOJURE_TIME_MS}ms (${TIME_PER_ITER_CLJ}ms/iter, warmup: ${CLOJURE_WARMUP_MS}ms)${NC}"
    else
        echo -e "${RED}❌ Failed to extract Clojure results${NC}"
        CLOJURE_AVAILABLE=false
    fi
    echo ""
fi

# ============================================================================
# 3. tiny-clj Benchmark
# ============================================================================
echo -e "${BLUE}⚡ Running tiny-clj benchmark...${NC}"

# Create modified version of benchmarks/fibonacci.clj with timing output
# All code runs in a single execution (one runtime initialization)
TEMP_CLJ_TINY=$(mktemp /tmp/fib_benchmark_tiny_XXXXXX.clj 2>/dev/null || echo "/tmp/fib_benchmark_tiny_$$.clj")
rm -f "$TEMP_CLJ_TINY"
cat > "$TEMP_CLJ_TINY" << TINYCLJ_EOF
;; Fibonacci function from benchmarks/fibonacci.clj (original benchmarks-game version)
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

;; Measurement with ${ITERATIONS} iterations (same as Clojure/JVM)
;; All iterations run in one execution - runtime initialized only once
;; Use time macro to measure execution time - inline the code to ensure it's executed
(time
  (let [iterations ${ITERATIONS}]
    (dotimes [i iterations] (fib ${FIB_N}))))

(println (str "ITERATIONS=${ITERATIONS}"))
(println (str "BENCHMARK_COMPLETE=true"))
TINYCLJ_EOF

# Try to find tiny-clj-repl - prefer build, then search
TINYCLJ_BIN=""
if [ -f "$BUILD_DIR/tiny-clj-repl" ]; then
    TINYCLJ_BIN="$BUILD_DIR/tiny-clj-repl"
else
    TINYCLJ_BIN=$(find . -name "tiny-clj-repl" -type f -executable 2>/dev/null | head -1)
fi

if [ -n "$TINYCLJ_BIN" ] && [ -f "$TINYCLJ_BIN" ]; then
    # Run tiny-clj benchmark using -f option to execute file
    TINYCLJ_OUTPUT=$("$TINYCLJ_BIN" -f "$TEMP_CLJ_TINY" 2>&1)
else
    echo -e "${YELLOW}⚠️  tiny-clj-repl not found. Skipping tiny-clj benchmark.${NC}"
    echo -e "${YELLOW}   Please build tiny-clj-repl to enable comparison.${NC}"
    TINYCLJ_OUTPUT=""
    TINYCLJ_TIME_MS=0
    TIME_PER_ITER_TINYCLJ=0
    TINYCLJ_ITERATIONS=0
fi

# Extract results from time output
# time prints "Elapsed time: X.XX msecs" or "Elapsed time: X.XX μsecs"
if [ -n "$TINYCLJ_OUTPUT" ]; then
    # Extract time from "Elapsed time: X.XX msecs" or "Elapsed time: X.XX μsecs"
    ELAPSED_TIME=$(echo "$TINYCLJ_OUTPUT" | grep -E "Elapsed time:" | sed -E 's/.*Elapsed time: ([0-9.]+) (msecs|μsecs).*/\1 \2/')
    TIME_VALUE=$(echo "$ELAPSED_TIME" | awk '{print $1}')
    TIME_UNIT=$(echo "$ELAPSED_TIME" | awk '{print $2}')
    
    # Convert to milliseconds
    if [ "$TIME_UNIT" = "μsecs" ]; then
        TINYCLJ_TIME_MS=$(echo "scale=3; $TIME_VALUE / 1000" | bc)
    elif [ "$TIME_UNIT" = "msecs" ]; then
        TINYCLJ_TIME_MS="$TIME_VALUE"
    else
        TINYCLJ_TIME_MS="0"
    fi
    
    TINYCLJ_ITERATIONS=${ITERATIONS}
    if [ -n "$TINYCLJ_TIME_MS" ] && [ "$TINYCLJ_TIME_MS" != "0" ]; then
        TIME_PER_ITER_TINYCLJ=$(echo "scale=6; $TINYCLJ_TIME_MS / $TINYCLJ_ITERATIONS" | bc)
    else
        TIME_PER_ITER_TINYCLJ="0"
    fi
fi

rm -f "$TEMP_CLJ_TINY"

if [ -n "$TINYCLJ_OUTPUT" ] && [ -n "$TINYCLJ_TIME_MS" ] && [ -n "$TIME_PER_ITER_TINYCLJ" ] && [ "$TINYCLJ_TIME_MS" != "0" ]; then
    # Format time per iteration for display
    TIME_PER_ITER_FORMATTED=$(echo "$TIME_PER_ITER_TINYCLJ" | awk '{if ($1 < 0.001) printf "%.6f", $1; else printf "%.3f", $1}')
    echo -e "${GREEN}✅ tiny-clj: ${TINYCLJ_TIME_MS}ms (${TIME_PER_ITER_FORMATTED}ms/iter, ${TINYCLJ_ITERATIONS} iterations)${NC}"
else
    echo -e "${RED}❌ Failed to extract tiny-clj results${NC}"
    echo "Output was: $TINYCLJ_OUTPUT"
    TINYCLJ_TIME_MS=0
    TIME_PER_ITER_TINYCLJ=0
    TINYCLJ_ITERATIONS=0
fi
echo ""

# ============================================================================
# 4. Comparison and CSV Export
# ============================================================================
echo -e "${BLUE}📊 Performance Comparison${NC}"
echo "=========================================="

# Create CSV header if file doesn't exist
if [ ! -f "$HISTORY_FILE" ]; then
    echo "timestamp,system,warmup_time_ms,test_time_ms,iterations,time_per_iteration_ms" > "$HISTORY_FILE"
fi

# Get current timestamp (Unix timestamp in seconds)
TIMESTAMP=$(date +%s)

# Append results to CSV
if [ "$CLOJURE_AVAILABLE" = true ] && [ -n "$CLOJURE_TIME_MS" ]; then
    echo "$TIMESTAMP,clojure-jvm,$CLOJURE_WARMUP_MS,$CLOJURE_TIME_MS,$ITERATIONS_CLJ,$TIME_PER_ITER_CLJ" >> "$HISTORY_FILE"
fi

if [ -n "$TINYCLJ_TIME_MS" ] && [ "$TINYCLJ_TIME_MS" != "0" ] && [ -n "$TIME_PER_ITER_TINYCLJ" ] && [ -n "$TINYCLJ_ITERATIONS" ]; then
    echo "$TIMESTAMP,tiny-clj,0,$TINYCLJ_TIME_MS,$TINYCLJ_ITERATIONS,$TIME_PER_ITER_TINYCLJ" >> "$HISTORY_FILE"
fi

# Display comparison table
printf "%-15s %12s %12s %12s %15s\n" "System" "Warmup (ms)" "Test (ms)" "Iterations" "ms/iter"
echo "------------------------------------------------------------------------"

if [ "$CLOJURE_AVAILABLE" = true ] && [ -n "$CLOJURE_TIME_MS" ]; then
    # Format time per iteration - handle scientific notation
    TIME_PER_ITER_CLJ_FORMATTED=$(echo "$TIME_PER_ITER_CLJ" | awk '{if ($1 < 0.001) printf "%.6f", $1; else printf "%.3f", $1}')
    printf "%-15s %12s %12s %12s %15s\n" "Clojure/JVM" "$CLOJURE_WARMUP_MS" "$CLOJURE_TIME_MS" "$ITERATIONS_CLJ" "$TIME_PER_ITER_CLJ_FORMATTED"
fi

if [ -n "$TINYCLJ_TIME_MS" ] && [ "$TINYCLJ_TIME_MS" != "0" ] && [ -n "$TIME_PER_ITER_TINYCLJ" ] && [ -n "$TINYCLJ_ITERATIONS" ]; then
    # Format time per iteration - handle very small values
    TIME_PER_ITER_FORMATTED=$(echo "$TIME_PER_ITER_TINYCLJ" | awk '{if ($1 < 0.001) printf "%.6f", $1; else printf "%.3f", $1}')
    printf "%-15s %12s %12s %12s %15s\n" "tiny-clj" "0" "$TINYCLJ_TIME_MS" "$TINYCLJ_ITERATIONS" "$TIME_PER_ITER_FORMATTED"
fi

echo ""

# Calculate performance ratio if both results available
if [ "$CLOJURE_AVAILABLE" = true ] && [ -n "$CLOJURE_TIME_MS" ] && [ -n "$TIME_PER_ITER_TINYCLJ" ] && [ "$TIME_PER_ITER_TINYCLJ" != "0" ] && [ -n "$TIME_PER_ITER_CLJ" ] && [ "$TIME_PER_ITER_CLJ" != "0" ]; then
    # Convert scientific notation to decimal for bc
    TIME_PER_ITER_CLJ_DECIMAL=$(echo "$TIME_PER_ITER_CLJ" | awk '{printf "%.10f", $1}')
    # Compare time per iteration - Clojure time / tiny-clj time
    # If ratio > 1, tiny-clj is faster (takes less time)
    RATIO=$(echo "scale=2; $TIME_PER_ITER_CLJ_DECIMAL / $TIME_PER_ITER_TINYCLJ" | bc 2>/dev/null || echo "0")
    if [ -n "$RATIO" ] && [ "$RATIO" != "0" ]; then
        if (( $(echo "$RATIO > 1" | bc -l 2>/dev/null || echo "0") )); then
            echo -e "${GREEN}🚀 tiny-clj is ${RATIO}x faster than Clojure/JVM (per iteration)${NC}"
        else
            INV_RATIO=$(echo "scale=2; 1 / $RATIO" | bc 2>/dev/null || echo "0")
            if [ -n "$INV_RATIO" ] && [ "$INV_RATIO" != "0" ]; then
                echo -e "${YELLOW}⚠️  Clojure/JVM is ${INV_RATIO}x faster than tiny-clj (per iteration)${NC}"
            fi
        fi
    fi
fi

echo ""
echo -e "${GREEN}✅ Results saved to: $HISTORY_FILE${NC}"
echo ""
