#!/bin/bash

# Size Trend Analyzer Script
# Analyzes executable size trends over time

set -e

HISTORY_FILE="executable_size_history.csv"
THRESHOLD_PERCENT=10.0  # Alert if size changes by more than 10%
SIGNIFICANT_THRESHOLD=5.0  # Mark as significant if change > 5%

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "=== Executable Size Trend Analyzer ==="
echo "Threshold for alerts: ${THRESHOLD_PERCENT}%"
echo "Threshold for significant changes: ${SIGNIFICANT_THRESHOLD}%"
echo ""

# Check if history file exists
if [ ! -f "$HISTORY_FILE" ]; then
    echo -e "${YELLOW}No size history file found. Creating baseline...${NC}"
    make test-executable-size > /dev/null 2>&1
    ./test-executable-size > /dev/null 2>&1
    echo -e "${GREEN}Baseline created: $HISTORY_FILE${NC}"
    exit 0
fi

# Function to get latest size for an executable
get_latest_size() {
    local name="$1"
    grep ",$name," "$HISTORY_FILE" | tail -1 | cut -d',' -f3
}

# Function to get previous size for an executable
get_previous_size() {
    local name="$1"
    grep ",$name," "$HISTORY_FILE" | tail -2 | head -1 | cut -d',' -f3
}

# Function to calculate percentage change
calculate_change() {
    local current="$1"
    local previous="$2"
    
    if [ "$previous" = "0" ] || [ -z "$previous" ]; then
        echo "N/A"
        return
    fi
    
    local change=$(echo "scale=2; (($current - $previous) / $previous) * 100" | bc -l)
    echo "$change"
}

# Function to get size category
get_size_category() {
    local change="$1"
    
    if [ "$change" = "N/A" ]; then
        echo "NEW"
    elif (( $(echo "$change > $THRESHOLD_PERCENT" | bc -l) )); then
        echo "SIGNIFICANT_INCREASE"
    elif (( $(echo "$change < -$THRESHOLD_PERCENT" | bc -l) )); then
        echo "SIGNIFICANT_DECREASE"
    elif (( $(echo "$change > $SIGNIFICANT_THRESHOLD" | bc -l) )); then
        echo "INCREASE"
    elif (( $(echo "$change < -$SIGNIFICANT_THRESHOLD" | bc -l) )); then
        echo "DECREASE"
    else
        echo "STABLE"
    fi
}

# Function to format change with color
format_change() {
    local change="$1"
    local category="$2"
    
    case "$category" in
        "SIGNIFICANT_INCREASE")
            echo -e "${RED}+${change}%${NC}"
            ;;
        "INCREASE")
            echo -e "${RED}+${change}%${NC}"
            ;;
        "SIGNIFICANT_DECREASE")
            echo -e "${GREEN}${change}%${NC}"
            ;;
        "DECREASE")
            echo -e "${GREEN}${change}%${NC}"
            ;;
        "STABLE")
            echo -e "${BLUE}${change}%${NC}"
            ;;
        "NEW")
            echo -e "${YELLOW}NEW${NC}"
            ;;
    esac
}

# Get list of executables
executables=($(tail -n +2 "$HISTORY_FILE" | cut -d',' -f2 | sort -u))

echo -e "${BLUE}=== Size Change Analysis ===${NC}"
echo "Executable Name              | Current (KB) | Previous (KB) | Change      | Status"
echo "-----------------------------|--------------|---------------|-------------|--------"

significant_changes=0
increases=0
decreases=0

for executable in "${executables[@]}"; do
    current_size=$(get_latest_size "$executable")
    previous_size=$(get_previous_size "$executable")
    
    if [ -z "$current_size" ]; then
        echo -e "${YELLOW}$executable${NC} | N/A | N/A | ${YELLOW}MISSING${NC} | ${YELLOW}NOT_FOUND${NC}"
        continue
    fi
    
    if [ -z "$previous_size" ]; then
        echo -e "${YELLOW}$executable${NC} | $(echo "scale=1; $current_size/1024" | bc) | N/A | ${YELLOW}NEW${NC} | ${YELLOW}NEW_EXECUTABLE${NC}"
        ((significant_changes++))
        continue
    fi
    
    # Calculate change
    change=$(calculate_change "$current_size" "$previous_size")
    category=$(get_size_category "$change")
    
    # Format output
    printf "%-29s | %-12s | %-13s | %-11s | " "$executable" "$(echo "scale=1; $current_size/1024" | bc)" "$(echo "scale=1; $previous_size/1024" | bc)"
    format_change "$change" "$category"
    
    # Count changes
    case "$category" in
        "SIGNIFICANT_INCREASE"|"INCREASE")
            ((increases++))
            ((significant_changes++))
            ;;
        "SIGNIFICANT_DECREASE"|"DECREASE")
            ((decreases++))
            ((significant_changes++))
            ;;
    esac
done

echo ""
echo -e "${BLUE}=== Summary ===${NC}"
echo "Total executables analyzed: ${#executables[@]}"
echo "Significant changes: $significant_changes"
echo -e "Size increases: ${RED}$increases${NC}"
echo -e "Size decreases: ${GREEN}$decreases${NC}"

# Alert on significant increases
if [ $increases -gt 0 ]; then
    echo ""
    echo -e "${RED}⚠️  WARNING: $increases executable size increase(s) detected!${NC}"
    echo -e "${RED}   Consider investigating recent changes that increased binary size.${NC}"
fi

# Celebrate decreases
if [ $decreases -gt 0 ]; then
    echo ""
    echo -e "${GREEN}🎉 Great! $decreases executable size decrease(s) detected!${NC}"
fi

# No significant changes
if [ $significant_changes -eq 0 ]; then
    echo ""
    echo -e "${BLUE}✅ All executable sizes are stable within acceptable ranges.${NC}"
fi

echo ""
echo -e "${BLUE}=== Size Optimization Recommendations ===${NC}"

# Analyze specific executables
for executable in "${executables[@]}"; do
    current_size=$(get_latest_size "$executable")
    previous_size=$(get_previous_size "$executable")
    
    if [ -z "$current_size" ] || [ -z "$previous_size" ]; then
        continue
    fi
    
    change=$(calculate_change "$current_size" "$previous_size")
    category=$(get_size_category "$change")
    
    case "$category" in
        "SIGNIFICANT_INCREASE")
            echo -e "${RED}• $executable: Investigate recent changes that caused this size increase${NC}"
            echo -e "${RED}  Consider: removing unused code, optimizing includes, using -Os flag${NC}"
            ;;
        "SIGNIFICANT_DECREASE")
            echo -e "${GREEN}• $executable: Great size reduction! Document what changes caused this${NC}"
            ;;
    esac
done

echo ""
echo "History file: $HISTORY_FILE"
echo "Run 'make test-executable-size' to add current measurements"
