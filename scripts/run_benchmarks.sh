#!/bin/bash

# Benchmark Runner Script
# Only runs benchmarks if all tests pass

set -e  # Exit on any error

echo "=== Running All Tests First ==="

# Run all tests via CTest
ctest --output-on-failure

echo "\n=== All Tests Passed! Running Benchmarks ==="

# Build and run benchmark target
cmake . >/dev/null
make -j test-benchmark >/dev/null
./test-benchmark --report --compare

echo "\n=== Benchmark Complete ==="
echo "All tests passed and benchmarks completed successfully!"

# Persist latest results automatically if available
if [ -f "benchmark_results.csv" ]; then
  # Append to history only if significant changes vs baseline (>= 2%)
  SIG_THRESHOLD=0.02
  COMMIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "no-git")
  HISTORY_FILE="benchmark_history.csv"

  # Ensure history header exists
  if [ ! -f "$HISTORY_FILE" ]; then
    echo "timestamp,name,time_ms,baseline_time_ms,change_percent,iterations,ops_per_sec,memory_bytes,commit" > "$HISTORY_FILE"
  fi

  if [ -f "benchmark_baseline.csv" ]; then
    # awk: load baseline times by name, then scan current and emit rows with |change| >= 2%
    awk -F, -v th=$SIG_THRESHOLD -v commit="$COMMIT_HASH" 'NR==FNR {if(NR>1) base[$2]=$3; next} NR>1 {
      name=$2; curr=$3; iter=$4; ops=$5; mem=$6; b=base[name]; if(b=="" || b==0) next;
      pct=(curr-b)/b; if(pct<0)pct=-pct; if(pct>=th){printf "%s,%s,%.6f,%.6f,%.2f,%s,%s,%s,%s\n", $1,name,curr,b,(($3-b)/b)*100,iter,ops,mem,commit}}
    ' benchmark_baseline.csv benchmark_results.csv >> "$HISTORY_FILE"
  fi

  # Update baseline with latest results
  cp benchmark_results.csv benchmark_baseline.csv
  echo "✓ Baseline updated (benchmark_baseline.csv)"
fi
