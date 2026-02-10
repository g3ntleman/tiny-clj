#!/usr/bin/env bash
set -euo pipefail

# Quick smoke test for per-eval heap growth using (heap ...).
# Runs each expression multiple times in one REPL process via repeated -e forms.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPL_BIN="${REPL_BIN:-$ROOT_DIR/build/tiny-clj-repl}"
ITERATIONS="${ITERATIONS:-12}"
WARMUP_EVALS="${WARMUP_EVALS:-1}"

if [[ ! -x "$REPL_BIN" ]]; then
  echo "error: repl binary not found: $REPL_BIN" >&2
  echo "hint: cmake -DCMAKE_BUILD_TYPE=Debug -DMEMORY_PROFILING_ENABLED=ON -B build && cmake --build build --target tiny-clj-repl" >&2
  exit 1
fi

run_case() {
  local label="$1"
  local expr="$2"
  local iterations="$3"
  local warmup="$4"
  local -a args=("-e" "(println :case \"$label\")")
  local output
  local count=0
  local idx=0
  local val=0
  local post_count=0
  local post_sum=0
  local first_post=""
  local last_post=""
  local positive_steps=0
  local prev_post=""
  local avg=0
  local slope="0.00"
  local verdict="stable"

  for ((i = 1; i <= iterations; i++)); do
    args+=("-e" "(heap $expr)")
  done

  output="$("$REPL_BIN" "${args[@]}")"
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
    fi
    last_post="$val"
    post_count=$((post_count + 1))
    post_sum=$((post_sum + val))
    if [[ -n "$prev_post" ]] && ((val > prev_post)); then
      positive_steps=$((positive_steps + 1))
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
  elif ((positive_steps > 0)); then
    verdict="linear-growth-suspected"
  else
    verdict="persistent-positive-delta"
  fi

  printf "%-28s warmup=%s avg=%s first=%s last=%s slope=%s steps_up=%s verdict=%s\n" \
    "$label" "$warmup" "$avg" "${first_post:-n/a}" "${last_post:-n/a}" "$slope" "$positive_steps" "$verdict"
}

echo "Running heap growth smoke test"
echo "repl: $REPL_BIN"
echo "iterations per case: $ITERATIONS (warmup ignored: $WARMUP_EVALS)"
echo
printf "%-28s %-8s %-8s %-8s %-8s %-10s %-9s %s\n" "case" "warmup" "avg" "first" "last" "slope" "steps_up" "verdict"
printf "%-28s %-8s %-8s %-8s %-8s %-10s %-9s %s\n" "----" "------" "---" "-----" "----" "-----" "--------" "-------"

run_case "plus_immediate" "(+ 1 2)" "$ITERATIONS" "$WARMUP_EVALS"
run_case "reduce_range" "(reduce + (range 200))" "$ITERATIONS" "$WARMUP_EVALS"
run_case "assoc_literal" "(assoc {:a 1 :b 2} :c 3)" "$ITERATIONS" "$WARMUP_EVALS"
run_case "vector_literal" "[1 2 3]" "$ITERATIONS" "$WARMUP_EVALS"

echo
echo "Interpretation:"
echo "- stable: post-warmup deltas are zero."
echo "- persistent-positive-delta: each eval retains bytes at a stable rate."
echo "- linear-growth-suspected: post-warmup deltas keep increasing across evals."
