#!/bin/bash

# Auto Benchmark Script
# Runs benchmarks after each commit and logs performance data

set -e

# Configuration
REPORTS_DIR="Reports"
BENCHMARK_LOG="$REPORTS_DIR/benchmark_history.csv"
SIZE_LOG="$REPORTS_DIR/executable_size_history.csv"
CURRENT_COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Auto Benchmark System ===${NC}"
echo "Commit: $CURRENT_COMMIT"
echo "Timestamp: $TIMESTAMP"
echo ""

# Create reports directory if it doesn't exist
mkdir -p "$REPORTS_DIR"

# Function to log benchmark data
log_benchmark() {
    local name="$1"
    local time_ms="$2"
    local iterations="$3"
    local ops_per_sec="$4"
    local memory_bytes="$5"
    
    # Get baseline time for comparison
    local baseline_time=""
    if [ -f "$BENCHMARK_LOG" ] && grep -q ",$name," "$BENCHMARK_LOG"; then
        # Get the first occurrence as baseline
        baseline_time=$(grep ",$name," "$BENCHMARK_LOG" | head -1 | cut -d',' -f3)
    fi
    
    # Calculate change percentage
    local change_percent="0.00"
    if [ -n "$baseline_time" ] && [ "$baseline_time" != "0" ]; then
        change_percent=$(echo "scale=2; (($time_ms - $baseline_time) / $baseline_time) * 100" | bc -l 2>/dev/null || echo "0.00")
    fi
    
    # Create log entry
    local log_entry="$TIMESTAMP,$name,$time_ms,$baseline_time,$change_percent,$iterations,$ops_per_sec,$memory_bytes,$CURRENT_COMMIT"
    
    # Ensure header exists
    if [ ! -f "$BENCHMARK_LOG" ]; then
        echo "timestamp,name,time_ms,baseline_time_ms,change_percent,iterations,ops_per_sec,memory_bytes,commit" > "$BENCHMARK_LOG"
    fi
    
    # Append log entry
    echo "$log_entry" >> "$BENCHMARK_LOG"
    
    echo -e "  ${GREEN}✓${NC} $name: ${time_ms}ms (${change_percent}% vs baseline)"
}

# Function to log executable sizes
log_executable_size() {
    local name="$1"
    local size_bytes="$2"
    local text_size="$3"
    local data_size="$4"
    local bss_size="$5"
    
    # Ensure header exists
    if [ ! -f "$SIZE_LOG" ]; then
        echo "timestamp,name,size_bytes,text_size,data_size,bss_size,total_size" > "$SIZE_LOG"
    fi
    
    # Append log entry
    local log_entry="$TIMESTAMP,$name,$size_bytes,$text_size,$data_size,$bss_size,$size_bytes"
    echo "$log_entry" >> "$SIZE_LOG"
    
    local size_kb=$(echo "scale=1; $size_bytes/1024" | bc -l)
    echo -e "  ${GREEN}✓${NC} $name: ${size_kb}KB"
}

# Function to run a benchmark and log results
run_benchmark() {
    local benchmark_name="$1"
    local iterations="$2"
    local command="$3"
    
    echo -e "${YELLOW}Running $benchmark_name...${NC}"
    
    # Run the benchmark and capture timing
    local start_time=$(date +%s.%N)
    eval "$command" > /dev/null 2>&1
    local end_time=$(date +%s.%N)
    
    # Calculate timing in milliseconds
    local duration=$(echo "($end_time - $start_time) * 1000" | bc -l)
    local ops_per_sec=$(echo "scale=0; $iterations / ($duration / 1000)" | bc -l 2>/dev/null || echo "0")
    
    # Log the results
    log_benchmark "$benchmark_name" "$duration" "$iterations" "$ops_per_sec" "0"
}

# Function to measure executable size
measure_executable_size() {
    local executable="$1"
    
    if [ -f "$executable" ]; then
        # Use file size as primary measurement
        local file_size=$(stat -f%z "$executable" 2>/dev/null || echo "0")
        local base_name=$(basename "$executable")
        
        # Try to get detailed size information
        local size_output=$(size "$executable" 2>/dev/null | tail -1)
        local text_size="0"
        local data_size="0"
        local bss_size="0"
        
        if [ -n "$size_output" ] && [ $(echo "$size_output" | wc -w) -ge 4 ]; then
            text_size=$(echo "$size_output" | awk '{print $1}')
            data_size=$(echo "$size_output" | awk '{print $2}')
            bss_size=$(echo "$size_output" | awk '{print $3}')
        fi
        
        log_executable_size "$base_name" "$file_size" "$text_size" "$data_size" "$bss_size"
    fi
}

echo -e "${BLUE}=== Building Project ===${NC}"
# Build the project
make clean > /dev/null 2>&1 || true
make > /dev/null 2>&1 || cmake . > /dev/null 2>&1 && make > /dev/null 2>&1

echo -e "${BLUE}=== Running Benchmarks ===${NC}"

# Run simple benchmarks
run_benchmark "project_build_time" "1" "make clean >/dev/null 2>&1 && make >/dev/null 2>&1"
run_benchmark "test_execution_time" "1" "./test-benchmark >/dev/null 2>&1"
run_benchmark "unit_tests_time" "1" "./test-unit >/dev/null 2>&1"
run_benchmark "parser_tests_time" "1" "./test-parser >/dev/null 2>&1"
run_benchmark "memory_tests_time" "1" "./test-memory-simple >/dev/null 2>&1"

echo -e "${BLUE}=== Measuring Executable Sizes ===${NC}"

# Measure executable sizes
measure_executable_size "./test-benchmark"
measure_executable_size "./test-unit"
measure_executable_size "./test-parser"
measure_executable_size "./test-seq"
measure_executable_size "./test-for-loops"
measure_executable_size "./test-memory-simple"

echo ""
echo -e "${GREEN}=== Benchmark Complete ===${NC}"
echo "Results logged to:"
echo "  - $BENCHMARK_LOG"
echo "  - $SIZE_LOG"
echo ""
echo -e "${BLUE}Latest benchmark data:${NC}"

# Show latest benchmark results
if [ -f "$BENCHMARK_LOG" ]; then
    echo "Benchmarks:"
    tail -5 "$BENCHMARK_LOG" | while IFS=',' read -r timestamp name time_ms baseline change_percent iterations ops_per_sec memory commit; do
        if [ "$name" != "name" ]; then
            printf "  %-25s: %8s ms (%+6s%%)\n" "$name" "$time_ms" "$change_percent"
        fi
    done
fi

if [ -f "$SIZE_LOG" ]; then
    echo "Executable sizes:"
    tail -3 "$SIZE_LOG" | while IFS=',' read -r timestamp name size_bytes text_size data_size bss_size total_size; do
        if [ "$name" != "name" ]; then
            local size_kb=$(echo "scale=1; $size_bytes/1024" | bc -l)
            printf "  %-25s: %8s KB\n" "$name" "$size_kb"
        fi
    done
fi

echo ""
echo -e "${GREEN}Auto benchmark completed successfully!${NC}"
