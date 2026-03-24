#!/usr/bin/env bash
# Sample profiling for fib(30) using the profiling build
#
# Goals:
# - Profile steady-state (avoid startup + load-file noise)
# - Use PID-based sampling (no name-based attach ambiguity)
# - Keep defaults safe (no sudo/admin rights needed)
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
# Default output dir is ignored by git; allow overriding to a tracked location.
OUTPUT_DIR="${OUTPUT_DIR:-benchmark_results}"
OUTPUT_FILE="${OUTPUT_FILE:-$OUTPUT_DIR/sample_fib30_$(date +%Y%m%d_%H%M%S).txt}"

# Tuning knobs (override via env vars)
SAMPLE_DURATION_SECONDS="${SAMPLE_DURATION_SECONDS:-30}"   # sample runtime
SAMPLE_INTERVAL_MS="${SAMPLE_INTERVAL_MS:-1}"             # sampling interval
WARMUP_SECONDS="${WARMUP_SECONDS:-2}"                     # delay before sampling
FIB_N="${FIB_N:-30}"                                      # fib argument

# Ensure output directory exists
mkdir -p "$OUTPUT_DIR"

echo "=== Building release binary for sampling ==="

# Configure and build the regular tiny-clj target.
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j --target tiny-clj

# Verify the binary exists
if [ ! -f "$BUILD_DIR/tiny-clj" ]; then
    echo "ERROR: Failed to build tiny-clj"
    exit 1
fi

echo "✅ Sampling binary ready: $BUILD_DIR/tiny-clj"

echo ""
echo "=== Running steady-state sample on fib($FIB_N) ==="
echo "Output: $OUTPUT_FILE"
echo ""

# Run a tight steady-state workload in the background, then attach sample by PID.
# Avoid load-file (benchmarks/fibonacci_30.clj runs side-effects on load).
WORKLOAD_EXPR="(do
  (defn fib [n]
    (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2)))))
  (while true
    (fib ${FIB_N})))"

TINYCLJ_PID=""
cleanup() {
  if [ -n "${TINYCLJ_PID}" ] && kill -0 "${TINYCLJ_PID}" 2>/dev/null; then
    kill "${TINYCLJ_PID}" 2>/dev/null || true
    # If it's still alive, force-kill (no admin rights required for own processes)
    sleep 0.2 || true
    kill -9 "${TINYCLJ_PID}" 2>/dev/null || true
    wait "${TINYCLJ_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

"$BUILD_DIR/tiny-clj" -e "${WORKLOAD_EXPR}" >/dev/null 2>&1 &
TINYCLJ_PID=$!

echo "tiny-clj PID: ${TINYCLJ_PID}"
echo "Warmup delay: ${WARMUP_SECONDS}s"
sleep "${WARMUP_SECONDS}"

echo "Sampling: ${SAMPLE_DURATION_SECONDS}s @ ${SAMPLE_INTERVAL_MS}ms interval"
sample "${TINYCLJ_PID}" "${SAMPLE_DURATION_SECONDS}" "${SAMPLE_INTERVAL_MS}" -mayDie -file "${OUTPUT_FILE}" >/dev/null 2>&1 || true

echo ""
echo "Full output saved to: ${OUTPUT_FILE}"
