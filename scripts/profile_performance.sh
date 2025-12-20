#!/bin/bash
# Performance Comparison Script: tiny-clj vs Clojure/JVM vs ClojureScript vs Python3
# Compares fib(20) performance (original benchmarks-game version without recur)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Performance Comparison: tiny-clj vs Clojure/JVM vs ClojureScript vs Python3 ===${NC}"
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
# Number of measurement runs per system (median is reported)
RUNS=${RUNS:-5}

# Create results directory
mkdir -p "$RESULTS_DIR"

# ============================================================================
# Helpers: median/min/max for numeric lists
# ============================================================================
median_value() {
    local values=("$@")
    local n=${#values[@]}
    if [ "$n" -eq 0 ]; then
        echo ""
        return 1
    fi
    local sorted
    sorted=$(printf '%s\n' "${values[@]}" | sort -n)
    if [ $((n % 2)) -eq 1 ]; then
        # Odd: middle element (1-based line number)
        echo "$sorted" | sed -n "$((n / 2 + 1))p"
    else
        # Even: average the two middle values
        local a b
        a=$(echo "$sorted" | sed -n "$((n / 2))p")
        b=$(echo "$sorted" | sed -n "$((n / 2 + 1))p")
        echo "scale=6; ($a + $b) / 2" | bc
    fi
}

min_value() {
    local values=("$@")
    if [ "${#values[@]}" -eq 0 ]; then
        echo ""
        return 1
    fi
    printf '%s\n' "${values[@]}" | sort -n | head -1
}

max_value() {
    local values=("$@")
    if [ "${#values[@]}" -eq 0 ]; then
        echo ""
        return 1
    fi
    printf '%s\n' "${values[@]}" | sort -n | tail -1
}

# Check if Clojure is available
if command -v clojure &> /dev/null; then
    CLOJURE_AVAILABLE=true
    echo -e "${GREEN}✅ Clojure/JVM found${NC}"
else
    CLOJURE_AVAILABLE=false
    echo -e "${YELLOW}⚠️  Clojure/JVM not found - skipping Clojure benchmarks${NC}"
fi

# Check if ClojureScript is available (planck or lumo or node)
CLJS_AVAILABLE=false
CLJS_RUNTIME=""
if command -v planck &> /dev/null; then
    CLJS_AVAILABLE=true
    CLJS_RUNTIME="planck"
    echo -e "${GREEN}✅ ClojureScript (planck) found${NC}"
elif command -v lumo &> /dev/null; then
    CLJS_AVAILABLE=true
    CLJS_RUNTIME="lumo"
    echo -e "${GREEN}✅ ClojureScript (lumo) found${NC}"
elif command -v node &> /dev/null; then
    CLJS_AVAILABLE=true
    CLJS_RUNTIME="node"
    NODE_VERSION=$(node --version 2>&1)
    echo -e "${GREEN}✅ ClojureScript (Node.js ${NODE_VERSION}) found${NC}"
else
    echo -e "${YELLOW}⚠️  ClojureScript not found (install planck, lumo, or node) - skipping${NC}"
fi

# Check if Python3 is available
if command -v python3 &> /dev/null; then
    PYTHON_AVAILABLE=true
    PYTHON_VERSION=$(python3 --version 2>&1 | cut -d' ' -f2)
    echo -e "${GREEN}✅ Python ${PYTHON_VERSION} found${NC}"
else
    PYTHON_AVAILABLE=false
    echo -e "${YELLOW}⚠️  Python3 not found - skipping Python benchmarks${NC}"
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
    echo -e "${BLUE}🔥 Running Clojure/JVM benchmark (${RUNS} runs, ${WARMUP_SECONDS}s warmup)...${NC}"
    
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

;; Actual measurement with ${ITERATIONS} iterations, repeated ${RUNS} times (median reported by shell)
(let [iterations ${ITERATIONS}
      runs ${RUNS}]
  (dotimes [run runs]
    (let [start (System/nanoTime)]
      (dotimes [i iterations] (fib ${FIB_N}))
      (let [end (System/nanoTime)
            test-time-ms (/ (- end start) 1000000.0)
            time-per-iter (/ test-time-ms iterations)]
        ;; IMPORTANT: Avoid locale-dependent formatting (decimal comma) -> print raw doubles (dot)
        (println (str "RUN_" (inc run) "_TEST_TIME_MS=" test-time-ms))
        (println (str "RUN_" (inc run) "_TIME_PER_ITER_MS=" time-per-iter))))))
  (println (str "ITERATIONS=" iterations))
  (println (str "RUNS=" runs)))
CLOJURE_EOF

    # Run Clojure benchmark
    CLJ_OUTPUT=$(clojure "$TEMP_CLJ" 2>&1)
    
    # Extract results
    CLOJURE_WARMUP_MS=$(echo "$CLJ_OUTPUT" | grep "WARMUP_TIME_MS=" | cut -d'=' -f2)
    ITERATIONS_CLJ=$(echo "$CLJ_OUTPUT" | grep "^ITERATIONS=" | cut -d'=' -f2)
    CLJ_RUN_TIMES=($(echo "$CLJ_OUTPUT" | grep -E "^RUN_[0-9]+_TEST_TIME_MS=" | cut -d'=' -f2))
    CLJ_RUN_PER_ITER=($(echo "$CLJ_OUTPUT" | grep -E "^RUN_[0-9]+_TIME_PER_ITER_MS=" | cut -d'=' -f2))
    CLOJURE_TIME_MS=$(median_value "${CLJ_RUN_TIMES[@]}")
    TIME_PER_ITER_CLJ=$(median_value "${CLJ_RUN_PER_ITER[@]}")
    CLJ_MIN_TIME=$(min_value "${CLJ_RUN_TIMES[@]}")
    CLJ_MAX_TIME=$(max_value "${CLJ_RUN_TIMES[@]}")
    
    rm -f "$TEMP_CLJ"
    
    if [ -n "$CLOJURE_TIME_MS" ] && [ -n "$CLOJURE_WARMUP_MS" ]; then
        echo -e "${GREEN}✅ Clojure/JVM: ${CLOJURE_TIME_MS}ms median (${TIME_PER_ITER_CLJ}ms/iter, warmup: ${CLOJURE_WARMUP_MS}ms, runs: ${RUNS}, range: ${CLJ_MIN_TIME}-${CLJ_MAX_TIME}ms)${NC}"
    else
        echo -e "${RED}❌ Failed to extract Clojure results${NC}"
        CLOJURE_AVAILABLE=false
    fi
    echo ""
fi

# ============================================================================
# 3. ClojureScript Benchmark (Node.js)
# ============================================================================
CLJS_TIME_MS=0
TIME_PER_ITER_CLJS=0
CLJS_ITERATIONS=0

if [ "$CLJS_AVAILABLE" = true ]; then
    echo -e "${BLUE}📜 Running ClojureScript benchmark...${NC}"
    
    if [ "$CLJS_RUNTIME" = "node" ]; then
        # Use plain JavaScript (equivalent ClojureScript would compile to similar code)
        TEMP_JS=$(mktemp /tmp/fib_benchmark_XXXXXX.js)
        cat > "$TEMP_JS" << JS_EOF
// ClojureScript-equivalent fibonacci (no TCO/recur)
function fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

const iterations = ${ITERATIONS};
const fibN = ${FIB_N};
const runs = ${RUNS};

for (let r = 0; r < runs; r++) {
    const start = process.hrtime.bigint();
    for (let i = 0; i < iterations; i++) {
        fib(fibN);
    }
    const end = process.hrtime.bigint();
    const testTimeMs = Number(end - start) / 1000000;
    const timePerIter = testTimeMs / iterations;
    // Avoid JS template strings here because bash expands ${...} in heredocs.
    console.log("RUN_" + (r + 1) + "_TEST_TIME_MS=" + testTimeMs.toFixed(2));
    console.log("RUN_" + (r + 1) + "_TIME_PER_ITER_MS=" + timePerIter.toFixed(6));
}

console.log("ITERATIONS=" + iterations);
console.log("RUNS=" + runs);
JS_EOF

        CLJS_OUTPUT=$(node "$TEMP_JS" 2>&1)
        rm -f "$TEMP_JS"
    else
        # Use planck or lumo
        TEMP_CLJS=$(mktemp /tmp/fib_benchmark_XXXXXX.cljs)
        cat > "$TEMP_CLJS" << CLJS_EOF
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

(let [iterations ${ITERATIONS}
      runs ${RUNS}]
  (dotimes [run runs]
    (let [start (.getTime (js/Date.))]
      (dotimes [i iterations] (fib ${FIB_N}))
      (let [end (.getTime (js/Date.))
            test-time (- end start)
            time-per-iter (/ test-time iterations)]
        (println (str "RUN_" (inc run) "_TEST_TIME_MS=" test-time))
        (println (str "RUN_" (inc run) "_TIME_PER_ITER_MS=" time-per-iter)))))
  (println (str "ITERATIONS=" iterations))
  (println (str "RUNS=" runs)))
CLJS_EOF

        CLJS_OUTPUT=$($CLJS_RUNTIME "$TEMP_CLJS" 2>&1)
        rm -f "$TEMP_CLJS"
    fi
    
    CLJS_ITERATIONS=$(echo "$CLJS_OUTPUT" | grep "^ITERATIONS=" | cut -d'=' -f2)
    CLJS_RUN_TIMES=($(echo "$CLJS_OUTPUT" | grep -E "^RUN_[0-9]+_TEST_TIME_MS=" | cut -d'=' -f2))
    CLJS_RUN_PER_ITER=($(echo "$CLJS_OUTPUT" | grep -E "^RUN_[0-9]+_TIME_PER_ITER_MS=" | cut -d'=' -f2))
    CLJS_TIME_MS=$(median_value "${CLJS_RUN_TIMES[@]}")
    TIME_PER_ITER_CLJS=$(median_value "${CLJS_RUN_PER_ITER[@]}")
    CLJS_MIN_TIME=$(min_value "${CLJS_RUN_TIMES[@]}")
    CLJS_MAX_TIME=$(max_value "${CLJS_RUN_TIMES[@]}")
    
    if [ -n "$CLJS_TIME_MS" ]; then
        TIME_PER_ITER_CLJS_FORMATTED=$(echo "$TIME_PER_ITER_CLJS" | awk '{if ($1 < 0.001) printf "%.6f", $1; else printf "%.3f", $1}')
        echo -e "${GREEN}✅ ClojureScript: ${CLJS_TIME_MS}ms median (${TIME_PER_ITER_CLJS_FORMATTED}ms/iter, ${CLJS_ITERATIONS} iterations, runs: ${RUNS}, range: ${CLJS_MIN_TIME}-${CLJS_MAX_TIME}ms)${NC}"
    else
        echo -e "${RED}❌ Failed to extract ClojureScript results${NC}"
        CLJS_AVAILABLE=false
    fi
    echo ""
fi

# ============================================================================
# 4. Python3 Benchmark (${RUNS} runs, take median)
# ============================================================================
PYTHON_TIME_MS=0
TIME_PER_ITER_PYTHON=0

if [ "$PYTHON_AVAILABLE" = true ]; then
    echo -e "${BLUE}🐍 Running Python3 benchmark (${RUNS} runs)...${NC}"
    
    TEMP_PY=$(mktemp /tmp/fib_benchmark_XXXXXX.py)
    cat > "$TEMP_PY" << PYTHON_EOF
import time

def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

iterations = ${ITERATIONS}
fib_n = ${FIB_N}
runs = ${RUNS}

for r in range(runs):
    start = time.perf_counter()
    for _ in range(iterations):
        fib(fib_n)
    end = time.perf_counter()
    test_time_ms = (end - start) * 1000
    time_per_iter = test_time_ms / iterations
    print(f"RUN_{r+1}_TEST_TIME_MS={test_time_ms:.2f}")
    print(f"RUN_{r+1}_TIME_PER_ITER_MS={time_per_iter:.6f}")

print(f"ITERATIONS={iterations}")
print(f"RUNS={runs}")
PYTHON_EOF

    PY_OUTPUT=$(python3 "$TEMP_PY" 2>&1)

    PYTHON_ITERATIONS=$(echo "$PY_OUTPUT" | grep "^ITERATIONS=" | cut -d'=' -f2)
    PY_RUN_TIMES=($(echo "$PY_OUTPUT" | grep -E "^RUN_[0-9]+_TEST_TIME_MS=" | cut -d'=' -f2))
    PY_RUN_PER_ITER=($(echo "$PY_OUTPUT" | grep -E "^RUN_[0-9]+_TIME_PER_ITER_MS=" | cut -d'=' -f2))
    PYTHON_TIME_MS=$(median_value "${PY_RUN_TIMES[@]}")
    TIME_PER_ITER_PYTHON=$(median_value "${PY_RUN_PER_ITER[@]}")
    PY_MIN_TIME=$(min_value "${PY_RUN_TIMES[@]}")
    PY_MAX_TIME=$(max_value "${PY_RUN_TIMES[@]}")
    
    rm -f "$TEMP_PY"
    
    if [ -n "$PYTHON_TIME_MS" ]; then
        TIME_PER_ITER_PY_DISPLAY=$(echo "$TIME_PER_ITER_PYTHON" | awk '{if ($1 < 0.001) printf "%.6f", $1; else printf "%.3f", $1}')
        echo -e "${GREEN}✅ Python3: ${PYTHON_TIME_MS}ms median (${TIME_PER_ITER_PY_DISPLAY}ms/iter, runs: ${RUNS}, range: ${PY_MIN_TIME}-${PY_MAX_TIME}ms)${NC}"
    else
        echo -e "${RED}❌ Failed to extract Python results${NC}"
        PYTHON_AVAILABLE=false
    fi
    echo ""
fi

# ============================================================================
# 5. tiny-clj Benchmark (${RUNS} runs, take median)
# ============================================================================
echo -e "${BLUE}⚡ Running tiny-clj benchmark (${RUNS} runs)...${NC}"

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
;; Repeat ${RUNS} times in a single execution (median computed by shell)
(let [iterations ${ITERATIONS}
      runs ${RUNS}]
  (dotimes [run runs]
    (time
      (dotimes [i iterations] (fib ${FIB_N})))))

(println (str "ITERATIONS=${ITERATIONS}"))
(println (str "RUNS=${RUNS}"))
(println (str "BENCHMARK_COMPLETE=true"))
TINYCLJ_EOF

# Try to find tiny-clj-repl - prefer build, then search
TINYCLJ_BIN=""
if [ -f "$BUILD_DIR/tiny-clj-repl" ]; then
    TINYCLJ_BIN="$BUILD_DIR/tiny-clj-repl"
else
    TINYCLJ_BIN=$(find . -name "tiny-clj-repl" -type f -executable 2>/dev/null | head -1)
fi

# Extract all "Elapsed time" values (ms) from tiny-clj output.
# Handles both "msecs" and "µsecs/μsecs".
extract_tinyclj_times_ms() {
    local output="$1"
    echo "$output" | awk '
        /Elapsed time:/ {
            # tokens: Elapsed time: <value> <unit>
            val = $3; unit = $4;
            if (unit == "msecs") {
                print val;
            } else if (unit == "µsecs" || unit == "μsecs") {
                printf "%.3f\n", (val / 1000.0);
            }
        }
    '
}

if [ -n "$TINYCLJ_BIN" ] && [ -f "$TINYCLJ_BIN" ]; then
    TINYCLJ_OUTPUT=$("$TINYCLJ_BIN" -f "$TEMP_CLJ_TINY" 2>&1)
    TIMES=($(extract_tinyclj_times_ms "$TINYCLJ_OUTPUT"))

    # If more timings are present for any reason, take the last ${RUNS}
    if [ "${#TIMES[@]}" -gt "$RUNS" ]; then
        TIMES=("${TIMES[@]: -$RUNS}")
    fi

    # Print individual runs for visibility
    i=1
    for t in "${TIMES[@]}"; do
        echo -e "  Run $i: ${t}ms"
        i=$((i + 1))
    done

    TINYCLJ_TIME_MS=$(median_value "${TIMES[@]}")
    MIN_TIME=$(min_value "${TIMES[@]}")
    MAX_TIME=$(max_value "${TIMES[@]}")
    
    TINYCLJ_ITERATIONS=${ITERATIONS}
    if [ -n "$TINYCLJ_TIME_MS" ] && [ "$TINYCLJ_TIME_MS" != "0" ]; then
        TIME_PER_ITER_TINYCLJ=$(echo "scale=6; $TINYCLJ_TIME_MS / $TINYCLJ_ITERATIONS" | bc)
    else
        TIME_PER_ITER_TINYCLJ="0"
    fi
else
    echo -e "${YELLOW}⚠️  tiny-clj-repl not found. Skipping tiny-clj benchmark.${NC}"
    echo -e "${YELLOW}   Please build tiny-clj-repl to enable comparison.${NC}"
    TINYCLJ_TIME_MS=0
    TIME_PER_ITER_TINYCLJ=0
    TINYCLJ_ITERATIONS=0
    MIN_TIME=0
    MAX_TIME=0
fi

rm -f "$TEMP_CLJ_TINY"

if [ -n "$TINYCLJ_TIME_MS" ] && [ "$TINYCLJ_TIME_MS" != "0" ] && [ -n "$TIME_PER_ITER_TINYCLJ" ]; then
    # Format time per iteration for display
    TIME_PER_ITER_FORMATTED=$(echo "$TIME_PER_ITER_TINYCLJ" | awk '{if ($1 < 0.001) printf "%.6f", $1; else printf "%.3f", $1}')
    echo -e "${GREEN}✅ tiny-clj: ${TINYCLJ_TIME_MS}ms median (${TIME_PER_ITER_FORMATTED}ms/iter, runs: ${RUNS}, range: ${MIN_TIME}-${MAX_TIME}ms)${NC}"
else
    echo -e "${RED}❌ Failed to extract tiny-clj results${NC}"
    TINYCLJ_TIME_MS=0
    TIME_PER_ITER_TINYCLJ=0
    TINYCLJ_ITERATIONS=0
fi
echo ""

# ============================================================================
# 6. Comparison and CSV Export
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

if [ "$CLJS_AVAILABLE" = true ] && [ -n "$CLJS_TIME_MS" ] && [ "$CLJS_TIME_MS" != "0" ]; then
    echo "$TIMESTAMP,clojurescript,0,$CLJS_TIME_MS,$CLJS_ITERATIONS,$TIME_PER_ITER_CLJS" >> "$HISTORY_FILE"
fi

if [ "$PYTHON_AVAILABLE" = true ] && [ -n "$PYTHON_TIME_MS" ] && [ "$PYTHON_TIME_MS" != "0" ]; then
    echo "$TIMESTAMP,python3,0,$PYTHON_TIME_MS,$PYTHON_ITERATIONS,$TIME_PER_ITER_PYTHON" >> "$HISTORY_FILE"
fi

if [ -n "$TINYCLJ_TIME_MS" ] && [ "$TINYCLJ_TIME_MS" != "0" ] && [ -n "$TIME_PER_ITER_TINYCLJ" ] && [ -n "$TINYCLJ_ITERATIONS" ]; then
    echo "$TIMESTAMP,tiny-clj,0,$TINYCLJ_TIME_MS,$TINYCLJ_ITERATIONS,$TIME_PER_ITER_TINYCLJ" >> "$HISTORY_FILE"
fi

# Helper function to calculate and format factor vs tiny-clj
calc_factor() {
    local other_time="$1"
    if [ -z "$TIME_PER_ITER_TINYCLJ" ] || [ "$TIME_PER_ITER_TINYCLJ" = "0" ] || [ -z "$other_time" ] || [ "$other_time" = "0" ]; then
        echo "-"
        return
    fi
    local ratio=$(echo "scale=2; $TIME_PER_ITER_TINYCLJ / $other_time" | bc 2>/dev/null || echo "0")
    if [ "$ratio" = "0" ] || [ -z "$ratio" ]; then
        echo "-"
    elif (( $(echo "$ratio >= 1" | bc -l 2>/dev/null || echo "0") )); then
        # Other system is faster
        printf "%.1fx faster" "$ratio"
    else
        # tiny-clj is faster
        local inv_ratio=$(echo "scale=1; 1 / $ratio" | bc 2>/dev/null || echo "0")
        printf "%.1fx slower" "$inv_ratio"
    fi
}

# Display comparison table with factor column
printf "%-15s %10s %10s %10s %12s %18s\n" "System" "Warmup" "Test (ms)" "Iter" "ms/iter" "vs tiny-clj"
echo "---------------------------------------------------------------------------------"

if [ "$CLOJURE_AVAILABLE" = true ] && [ -n "$CLOJURE_TIME_MS" ]; then
    TIME_PER_ITER_CLJ_FORMATTED=$(echo "$TIME_PER_ITER_CLJ" | awk '{if ($1 < 0.001) printf "%.6f", $1; else printf "%.3f", $1}')
    FACTOR_CLJ=$(calc_factor "$TIME_PER_ITER_CLJ")
    printf "%-15s %10s %10s %10s %12s %18s\n" "Clojure/JVM" "$CLOJURE_WARMUP_MS" "$CLOJURE_TIME_MS" "$ITERATIONS_CLJ" "$TIME_PER_ITER_CLJ_FORMATTED" "$FACTOR_CLJ"
fi

if [ "$CLJS_AVAILABLE" = true ] && [ -n "$CLJS_TIME_MS" ] && [ "$CLJS_TIME_MS" != "0" ]; then
    TIME_PER_ITER_CLJS_FORMATTED=$(echo "$TIME_PER_ITER_CLJS" | awk '{if ($1 < 0.001) printf "%.6f", $1; else printf "%.3f", $1}')
    FACTOR_CLJS=$(calc_factor "$TIME_PER_ITER_CLJS")
    printf "%-15s %10s %10s %10s %12s %18s\n" "ClojureScript" "0" "$CLJS_TIME_MS" "$CLJS_ITERATIONS" "$TIME_PER_ITER_CLJS_FORMATTED" "$FACTOR_CLJS"
fi

if [ "$PYTHON_AVAILABLE" = true ] && [ -n "$PYTHON_TIME_MS" ] && [ "$PYTHON_TIME_MS" != "0" ]; then
    TIME_PER_ITER_PY_FORMATTED=$(echo "$TIME_PER_ITER_PYTHON" | awk '{if ($1 < 0.001) printf "%.6f", $1; else printf "%.3f", $1}')
    FACTOR_PY=$(calc_factor "$TIME_PER_ITER_PYTHON")
    printf "%-15s %10s %10s %10s %12s %18s\n" "Python3" "0" "$PYTHON_TIME_MS" "$PYTHON_ITERATIONS" "$TIME_PER_ITER_PY_FORMATTED" "$FACTOR_PY"
fi

if [ -n "$TINYCLJ_TIME_MS" ] && [ "$TINYCLJ_TIME_MS" != "0" ] && [ -n "$TIME_PER_ITER_TINYCLJ" ] && [ -n "$TINYCLJ_ITERATIONS" ]; then
    TIME_PER_ITER_FORMATTED=$(echo "$TIME_PER_ITER_TINYCLJ" | awk '{if ($1 < 0.001) printf "%.6f", $1; else printf "%.3f", $1}')
    printf "%-15s %10s %10s %10s %12s %18s\n" "tiny-clj" "0" "$TINYCLJ_TIME_MS" "$TINYCLJ_ITERATIONS" "$TIME_PER_ITER_FORMATTED" "(baseline)"
fi

echo ""
echo -e "${GREEN}✅ Results saved to: $HISTORY_FILE${NC}"
echo ""
