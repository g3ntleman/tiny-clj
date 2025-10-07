#!/bin/bash

# Benchmark Analyzer Script
# Detects significant changes in benchmark performance

set -e

BASELINE_FILE="benchmark_baseline.csv"
CURRENT_FILE="benchmark_results.csv"
THRESHOLD_PERCENT=5.0   # Alert if performance changes by more than 5%
SIGNIFICANT_THRESHOLD=2.0  # Mark as significant if change > 2%

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "=== Benchmark Performance Analyzer ==="
echo "Threshold for alerts: ${THRESHOLD_PERCENT}%"
echo "Threshold for significant changes: ${SIGNIFICANT_THRESHOLD}%"
echo ""

# Check if baseline exists
if [ ! -f "$BASELINE_FILE" ]; then
    echo -e "${YELLOW}No baseline file found. Creating baseline from current results...${NC}"
    if [ -f "$CURRENT_FILE" ]; then
        cp "$CURRENT_FILE" "$BASELINE_FILE"
        echo -e "${GREEN}Baseline created: $BASELINE_FILE${NC}"
    else
        echo -e "${RED}Error: No current results found. Run benchmarks first.${NC}"
        exit 1
    fi
    exit 0
fi

if [ ! -f "$CURRENT_FILE" ]; then
    echo -e "${RED}Error: No current results found. Run benchmarks first.${NC}"
    exit 1
fi

# Function to extract performance data from CSV
extract_benchmark_data() {
    local file="$1"
    local name="$2"
    
    # Extract the latest entry for this benchmark name
    grep ",$name," "$file" | tail -1 | cut -d',' -f3,4,5
}

# Function to calculate percentage change
calculate_change() {
    local current="$1"
    local baseline="$2"
    
    if [ "$baseline" = "0" ] || [ -z "$baseline" ]; then
        echo "N/A"
        return
    fi
    
    # Calculate percentage change: ((current - baseline) / baseline) * 100
    local change=$(echo "scale=2; (($current - $baseline) / $baseline) * 100" | bc -l)
    echo "$change"
}

# Function to get performance category
get_performance_category() {
    local change="$1"
    
    if [ "$change" = "N/A" ]; then
        echo "NEW"
    elif (( $(echo "$change > $THRESHOLD_PERCENT" | bc -l) )); then
        echo "SIGNIFICANT_IMPROVEMENT"
    elif (( $(echo "$change < -$THRESHOLD_PERCENT" | bc -l) )); then
        echo "SIGNIFICANT_REGRESSION"
    elif (( $(echo "$change > $SIGNIFICANT_THRESHOLD" | bc -l) )); then
        echo "IMPROVEMENT"
    elif (( $(echo "$change < -$SIGNIFICANT_THRESHOLD" | bc -l) )); then
        echo "REGRESSION"
    else
        echo "STABLE"
    fi
}

# Function to format change with color
format_change() {
    local change="$1"
    local category="$2"
    
    case "$category" in
        "SIGNIFICANT_IMPROVEMENT")
            echo -e "${GREEN}+${change}%${NC}"
            ;;
        "IMPROVEMENT")
            echo -e "${GREEN}+${change}%${NC}"
            ;;
        "SIGNIFICANT_REGRESSION")
            echo -e "${RED}${change}%${NC}"
            ;;
        "REGRESSION")
            echo -e "${RED}${change}%${NC}"
            ;;
        "STABLE")
            echo -e "${BLUE}${change}%${NC}"
            ;;
        "NEW")
            echo -e "${YELLOW}NEW${NC}"
            ;;
    esac
}

# Benchmark names to analyze
benchmarks=(
    "primitive_object_creation"
    "type_checking"
    "reference_counting"
    "vector_creation"
    "map_operations"
    "function_calls"
)

echo -e "${BLUE}=== Performance Change Analysis ===${NC}"
echo "Benchmark Name                    | Current (ms) | Baseline (ms) | Change      | Status"
echo "----------------------------------|--------------|---------------|-------------|--------"

significant_changes=0
improvements=0
regressions=0

for benchmark in "${benchmarks[@]}"; do
    # Extract data
    current_data=$(extract_benchmark_data "$CURRENT_FILE" "$benchmark")
    baseline_data=$(extract_benchmark_data "$BASELINE_FILE" "$benchmark")
    
    if [ -z "$current_data" ]; then
        echo -e "${YELLOW}$benchmark${NC} | N/A | N/A | ${YELLOW}MISSING${NC} | ${YELLOW}NOT_FOUND${NC}"
        continue
    fi
    
    if [ -z "$baseline_data" ]; then
        echo -e "${YELLOW}$benchmark${NC} | $current_data | N/A | ${YELLOW}NEW${NC} | ${YELLOW}NEW_BENCHMARK${NC}"
        ((significant_changes++))
        continue
    fi
    
    # Parse data (time_ms,iterations,ops_per_sec)
    current_time=$(echo "$current_data" | cut -d',' -f1)
    baseline_time=$(echo "$baseline_data" | cut -d',' -f1)
    
    # Calculate change
    change=$(calculate_change "$current_time" "$baseline_time")
    category=$(get_performance_category "$change")
    
    # Format output
    printf "%-33s | %-12s | %-13s | %-11s | " "$benchmark" "$current_time" "$baseline_time"
    format_change "$change" "$category"
    
    # Count changes
    case "$category" in
        "SIGNIFICANT_IMPROVEMENT"|"IMPROVEMENT")
            ((improvements++))
            ((significant_changes++))
            ;;
        "SIGNIFICANT_REGRESSION"|"REGRESSION")
            ((regressions++))
            ((significant_changes++))
            ;;
    esac
done

echo ""
echo -e "${BLUE}=== Summary ===${NC}"
echo "Total benchmarks analyzed: ${#benchmarks[@]}"
echo "Significant changes: $significant_changes"
echo -e "Improvements: ${GREEN}$improvements${NC}"
echo -e "Regressions: ${RED}$regressions${NC}"

# Alert on significant regressions
if [ $regressions -gt 0 ]; then
    echo ""
    echo -e "${RED}⚠️  WARNING: $regressions performance regression(s) detected!${NC}"
    echo -e "${RED}   Consider investigating the changes that caused these regressions.${NC}"
    exit 1
fi

# Celebrate improvements
if [ $improvements -gt 0 ]; then
    echo ""
    echo -e "${GREEN}🎉 Great! $improvements performance improvement(s) detected!${NC}"
fi

# No significant changes
if [ $significant_changes -eq 0 ]; then
    echo ""
    echo -e "${BLUE}✅ All benchmarks are stable within acceptable ranges.${NC}"
fi

echo ""
echo -e "${BLUE}=== Recommendations ===${NC}"

# Analyze specific benchmarks
for benchmark in "${benchmarks[@]}"; do
    current_data=$(extract_benchmark_data "$CURRENT_FILE" "$benchmark")
    baseline_data=$(extract_benchmark_data "$BASELINE_FILE" "$benchmark")
    
    if [ -z "$current_data" ] || [ -z "$baseline_data" ]; then
        continue
    fi
    
    current_time=$(echo "$current_data" | cut -d',' -f1)
    baseline_time=$(echo "$baseline_data" | cut -d',' -f1)
    change=$(calculate_change "$current_time" "$baseline_time")
    category=$(get_performance_category "$change")
    
    case "$category" in
        "SIGNIFICANT_REGRESSION")
            echo -e "${RED}• $benchmark: Investigate recent changes that may have caused this regression${NC}"
            ;;
        "SIGNIFICANT_IMPROVEMENT")
            echo -e "${GREEN}• $benchmark: Great improvement! Document what changes caused this${NC}"
            ;;
    esac
done

echo ""
echo "Baseline file: $BASELINE_FILE"
echo "Current file: $CURRENT_FILE"
