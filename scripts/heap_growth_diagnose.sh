#!/usr/bin/env bash
set -euo pipefail

# Detailed lineargrowth diagnosis using repeated `(heap ...)` evals.
# Includes counter-probes to isolate result lifetime and syntax-path differences.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPL_BIN="${REPL_BIN:-$ROOT_DIR/build/tiny-clj-repl}"
ITERATIONS="${ITERATIONS:-80}"
WARMUP_EVALS="${WARMUP_EVALS:-3}"
FAIL_ON_AVG_POST_DELTA="${FAIL_ON_AVG_POST_DELTA:-0}"
FAIL_THRESHOLD_BYTES="${FAIL_THRESHOLD_BYTES:-0}"

if [[ ! -x "$REPL_BIN" ]]; then
  echo "error: repl binary not found: $REPL_BIN" >&2
  echo "hint: cmake -DCMAKE_BUILD_TYPE=Debug -DMEMORY_PROFILING_ENABLED=ON -B build && cmake --build build --target tiny-clj-repl" >&2
  exit 1
fi

run_case() {
  local label="$1"
  local iterations="$2"
  local warmup="$3"
  shift 3

  local output
  local -a growths
  local i=0
  local idx=0
  local count=0
  local val=0
  local first_post=""
  local last_post=""
  local post_count=0
  local post_sum=0
  local post_min=""
  local post_max=""
  local up_steps=0
  local down_steps=0
  local prev_post=""
  local avg=0
  local slope="0.00"
  local verdict="stable"

  output="$("$REPL_BIN" "$@")"
  growths=()
  while IFS= read -r line; do
    growths+=("$line")
  done < <(printf "%s\n" "$output" | awk '/^Heap growth: / {print $3}')
  count="${#growths[@]}"
  if [[ "$count" -ne "$iterations" ]]; then
    echo "error: case '$label' expected $iterations heap lines, got $count" >&2
    printf "%s\n" "$output" >&2
    exit 1
  fi

  for val in "${growths[@]}"; do
    idx=$((idx + 1))
    if ((idx <= warmup)); then
      continue
    fi
    if [[ -z "$first_post" ]]; then
      first_post="$val"
      post_min="$val"
      post_max="$val"
    fi
    last_post="$val"
    (( val < post_min )) && post_min="$val"
    (( val > post_max )) && post_max="$val"
    post_count=$((post_count + 1))
    post_sum=$((post_sum + val))
    if [[ -n "$prev_post" ]]; then
      if ((val > prev_post)); then
        up_steps=$((up_steps + 1))
      elif ((val < prev_post)); then
        down_steps=$((down_steps + 1))
      fi
    fi
    prev_post="$val"
  done

  if ((post_count > 0)); then
    avg=$((post_sum / post_count))
  fi
  if ((post_count > 1)); then
    slope="$(awk -v first="$first_post" -v last="$last_post" -v n="$post_count" 'BEGIN { printf "%.2f", (last-first)/(n-1) }')"
  fi

  if ((avg == 0)); then
    verdict="stable"
  elif ((up_steps > down_steps)); then
    verdict="linear-growth-suspected"
  else
    verdict="persistent-positive-delta"
  fi

  printf "%-34s avg_post=%-8s min=%-8s max=%-8s slope=%-10s up=%-6s down=%-6s verdict=%s\n" \
    "$label" "$avg" "${post_min:-n/a}" "${post_max:-n/a}" "$slope" "$up_steps" "$down_steps" "$verdict"

  if ((FAIL_ON_AVG_POST_DELTA == 1)) && ((avg > FAIL_THRESHOLD_BYTES)); then
    echo "failure: '$label' avg post-warmup delta $avg exceeds threshold $FAIL_THRESHOLD_BYTES bytes" >&2
    return 2
  fi
}

run_heap_case() {
  local label="$1"
  local expr="$2"
  local iterations="$3"
  local warmup="$4"
  local -a args=("-e" "(println :case \"$label\")")
  local i=0
  for ((i = 1; i <= iterations; i++)); do
    args+=("-e" "(heap $expr)")
  done
  run_case "$label" "$iterations" "$warmup" "${args[@]}"
}

echo "Running detailed heap growth diagnosis"
echo "repl: $REPL_BIN"
echo "iterations per case: $ITERATIONS (warmup ignored: $WARMUP_EVALS)"
if ((FAIL_ON_AVG_POST_DELTA == 1)); then
  echo "strict mode: avg post-warmup delta must be <= $FAIL_THRESHOLD_BYTES bytes"
fi
echo
printf "%-34s %-15s %-10s %-10s %-12s %-8s %-8s %s\n" "case" "avg_post" "min" "max" "slope" "up" "down" "verdict"
printf "%-34s %-15s %-10s %-10s %-12s %-8s %-8s %s\n" "----" "--------" "---" "---" "-----" "--" "----" "-------"

# Calibration and known stable controls
run_heap_case "control_plus" "(+ 1 2)" "$ITERATIONS" "$WARMUP_EVALS"
run_heap_case "control_reduce_range" "(reduce + (range 200))" "$ITERATIONS" "$WARMUP_EVALS"

# Suspected growth cases
run_heap_case "suspect_assoc_literal" "(assoc {:a 1 :b 2} :c 3)" "$ITERATIONS" "$WARMUP_EVALS"
run_heap_case "suspect_vector_literal" "[1 2 3]" "$ITERATIONS" "$WARMUP_EVALS"
run_heap_case "suspect_list_literal" "(list 1 2 3)" "$ITERATIONS" "$WARMUP_EVALS"

# Counter-probes for lifetime and syntax path isolation
declare -a assoc_var_args=("-e" "(println :case \"probe_assoc_var\")" "-e" "(def m {:a 1 :b 2})")
for ((i = 1; i <= ITERATIONS; i++)); do
  assoc_var_args+=("-e" "(heap (assoc m :c 3))")
done
run_case "probe_assoc_var" "$ITERATIONS" "$WARMUP_EVALS" "${assoc_var_args[@]}"
run_heap_case "probe_discard_result" "(do (assoc {:a 1 :b 2} :c 3) nil)" "$ITERATIONS" "$WARMUP_EVALS"

echo
echo "Next step for type-level breakdown:"
echo "  ./build/tiny-clj-repl -f scripts/leak_diagnose.clj"
