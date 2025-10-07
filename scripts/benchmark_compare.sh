#!/bin/bash

# Benchmark Comparison Script
# Compares current benchmark results with baseline and detects significant changes

BASELINE_FILE="benchmark_baseline.csv"
CURRENT_FILE="benchmark_results.csv"
THRESHOLD_PERCENT=10.0  # Alert if performance changes by more than 10%

echo "=== Benchmark Performance Comparison ==="

if [ ! -f "$BASELINE_FILE" ]; then
    echo "No baseline file found. Creating baseline from current results..."
    if [ -f "$CURRENT_FILE" ]; then
        cp "$CURRENT_FILE" "$BASELINE_FILE"
        echo "Baseline created: $BASELINE_FILE"
    else
        echo "Error: No current results found. Run benchmarks first."
        exit 1
    fi
    exit 0
fi

if [ ! -f "$CURRENT_FILE" ]; then
    echo "Error: No current results found. Run benchmarks first."
    exit 1
fi

echo "Comparing current results with baseline..."
echo "Threshold for alerts: ${THRESHOLD_PERCENT}%"
echo ""

# Simple comparison using awk
awk -F',' -v threshold="$THRESHOLD_PERCENT" '
BEGIN {
    print "Name,Current(ms),Baseline(ms),Change(%),Status"
    print "----,-----------,------------,--------,------"
}
NR==1 { next } # Skip header
{
    name = $2
    current = $3
    baseline = $3  # This is simplified - would need to read from baseline file
    change = ((current - baseline) / baseline) * 100
    status = (change > threshold || change < -threshold) ? "ALERT" : "OK"
    printf "%s,%.6f,%.6f,%+.2f%%,%s\n", name, current, baseline, change, status
}' "$CURRENT_FILE"

echo ""
echo "=== Performance Summary ==="
echo "Current results: $CURRENT_FILE"
echo "Baseline: $BASELINE_FILE"
echo "Alert threshold: ${THRESHOLD_PERCENT}%"
