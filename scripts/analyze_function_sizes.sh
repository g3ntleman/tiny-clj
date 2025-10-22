#!/bin/bash

# Function Size Analysis Script
# Analyzes which functions generate the most object code for tiny-clj-stm32

set -e

TARGET="tiny-clj-stm32"
STM32_TOOLCHAIN="arm-none-eabi-"
OUTPUT_FILE="Reports/function_size_analysis.md"
TOP_COUNT=20

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "=== Function Size Analysis ==="
echo "Target: $TARGET"
echo "Output: $OUTPUT_FILE"
echo ""

# Check if target exists
if [ ! -f "$TARGET" ]; then
    echo -e "${RED}Error: $TARGET not found!${NC}"
    echo "Please build the project first:"
    echo "  make clean && cmake -DCMAKE_BUILD_TYPE=MinSizeRel . && make -j4"
    echo ""
    echo "Note: This script analyzes the macOS build. For STM32-specific analysis:"
    echo "  cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/stm32.cmake -DCMAKE_BUILD_TYPE=MinSizeRel ."
    echo "  make -j4"
    exit 1
fi

# Create Reports directory if it doesn't exist
mkdir -p Reports

echo "Analyzing function sizes..."

# Get total executable size
TOTAL_SIZE=$(stat -f%z "$TARGET" 2>/dev/null || stat -c%s "$TARGET" 2>/dev/null)
TOTAL_SIZE_KB=$(echo "scale=1; $TOTAL_SIZE / 1024" | bc -l)

# Use size command to get section sizes
echo "Extracting section information..."
TEXT_SIZE=$(size -A "$TARGET" 2>/dev/null | grep -E "__text|\.text" | awk '{sum += $2} END {print sum}' || echo "0")

# Detect target architecture and use appropriate tools
TARGET_TYPE=$(file "$TARGET" 2>/dev/null | grep -o "Mach-O\|ELF" || echo "UNKNOWN")

if [ "$TARGET_TYPE" = "Mach-O" ]; then
    echo "Detected: macOS Mach-O executable"
    echo "Note: This analysis includes macOS-specific symbols (__mh_execute_header, etc.)"
    echo "For STM32-specific analysis, use STM32 toolchain build"
    echo ""
elif [ "$TARGET_TYPE" = "ELF" ]; then
    echo "Detected: ELF executable (STM32/ARM)"
    echo "This is the correct target for STM32 optimization analysis"
    echo ""
else
    echo "Unknown target type. Proceeding with analysis..."
    echo ""
fi

# Use nm to get function symbols and estimate sizes
echo "Extracting function symbols..."

# Get function symbols and estimate sizes based on symbol table
FUNCTIONS_DATA=$(nm "$TARGET" | grep ' [Tt] ' | awk '{
    if (NF >= 3) {
        addr = $1
        type = $2
        name = $3
        # Remove leading underscores
        gsub(/^_+/, "", name)
        gsub(/@.*$/, "", name)
        # Skip macOS-specific symbols that don'\''t exist on STM32
        if (name != "" && name != "__mh_execute_header" && name != "__mh_dylib_header") {
            print addr " " name
        }
    }
}' | sort -k1,1n)

# Calculate function sizes by address differences
echo "Calculating function sizes..."
FUNCTIONS_WITH_SIZES=""
prev_addr=""
prev_name=""

echo "$FUNCTIONS_DATA" | while read addr name; do
    if [ -n "$prev_addr" ] && [ -n "$prev_name" ]; then
        # Calculate size as difference in addresses (in bytes)
        size=$((0x$addr - 0x$prev_addr))
        if [ "$size" -gt 0 ] && [ "$size" -lt 10000 ]; then  # Reasonable size limit
            echo "$size $prev_name"
        fi
    fi
    prev_addr="$addr"
    prev_name="$name"
done | sort -nr > /tmp/function_sizes.txt

# Read the results
FUNCTIONS_DATA=$(cat /tmp/function_sizes.txt)
rm -f /tmp/function_sizes.txt

# Count total functions
TOTAL_FUNCTIONS=$(echo "$FUNCTIONS_DATA" | wc -l)

echo "Found $TOTAL_FUNCTIONS functions"

# Calculate total code size
TOTAL_CODE_SIZE=$(echo "$FUNCTIONS_DATA" | awk '{sum += $1} END {print sum}')

# Get top functions
TOP_FUNCTIONS=$(echo "$FUNCTIONS_DATA" | head -n $TOP_COUNT)

# Calculate cumulative size of top functions
TOP_CUMULATIVE_SIZE=$(echo "$TOP_FUNCTIONS" | awk '{sum += $1} END {print sum}')
if [ "$TOTAL_CODE_SIZE" -gt 0 ]; then
    TOP_PERCENTAGE=$(echo "scale=2; ($TOP_CUMULATIVE_SIZE / $TOTAL_CODE_SIZE) * 100" | bc -l)
else
    TOP_PERCENTAGE="0.00"
fi

# Categorize functions by size
LARGE_FUNCTIONS=$(echo "$FUNCTIONS_DATA" | awk '$1 > 5120 {count++; sum += $1} END {print count " " sum}')
MEDIUM_FUNCTIONS=$(echo "$FUNCTIONS_DATA" | awk '$1 >= 1024 && $1 <= 5120 {count++; sum += $1} END {print count " " sum}')
SMALL_FUNCTIONS=$(echo "$FUNCTIONS_DATA" | awk '$1 < 1024 {count++; sum += $1} END {print count " " sum}')

LARGE_COUNT=$(echo $LARGE_FUNCTIONS | cut -d' ' -f1)
LARGE_SIZE=$(echo $LARGE_FUNCTIONS | cut -d' ' -f2)
MEDIUM_COUNT=$(echo $MEDIUM_FUNCTIONS | cut -d' ' -f1)
MEDIUM_SIZE=$(echo $MEDIUM_FUNCTIONS | cut -d' ' -f2)
SMALL_COUNT=$(echo $SMALL_FUNCTIONS | cut -d' ' -f1)
SMALL_SIZE=$(echo $SMALL_FUNCTIONS | cut -d' ' -f2)

LARGE_SIZE_KB=$(echo "scale=1; $LARGE_SIZE / 1024" | bc -l)
MEDIUM_SIZE_KB=$(echo "scale=1; $MEDIUM_SIZE / 1024" | bc -l)
SMALL_SIZE_KB=$(echo "scale=1; $SMALL_SIZE / 1024" | bc -l)

echo "Generating report..."

# Generate Markdown report
cat > "$OUTPUT_FILE" << EOF
# Function Size Analysis Report

**Generated:** $(date)
**Target:** $TARGET
**Build Type:** MinSizeRel
**Target Type:** $TARGET_TYPE

## Summary

- **Total Executable Size:** $TOTAL_SIZE_KB KB
- **Total Code Size:** $(echo "scale=1; $TOTAL_CODE_SIZE / 1024" | bc -l) KB
- **Number of Functions:** $TOTAL_FUNCTIONS
- **Top $TOP_COUNT Functions Account for:** $TOP_PERCENTAGE%

## Top $TOP_COUNT Largest Functions

| Rank | Function Name | Size (bytes) | Size (KB) | % of Total |
|------|---------------|--------------|-----------|------------|
EOF

# Add top functions to report
rank=1
echo "$TOP_FUNCTIONS" | while read size name; do
    if [ -n "$size" ] && [ -n "$name" ]; then
        size_kb=$(echo "scale=1; $size / 1024" | bc -l)
        if [ "$TOTAL_CODE_SIZE" -gt 0 ]; then
            percentage=$(echo "scale=2; ($size / $TOTAL_CODE_SIZE) * 100" | bc -l)
        else
            percentage="0.00"
        fi
        printf "| %d | %s | %d | %.1f | %.2f%% |\n" "$rank" "$name" "$size" "$size_kb" "$percentage" >> "$OUTPUT_FILE"
        rank=$((rank + 1))
    fi
done

cat >> "$OUTPUT_FILE" << EOF

## Size Distribution

- **Large Functions (>5KB):** $LARGE_COUNT functions, $LARGE_SIZE_KB KB total
- **Medium Functions (1-5KB):** $MEDIUM_COUNT functions, $MEDIUM_SIZE_KB KB total  
- **Small Functions (<1KB):** $SMALL_COUNT functions, $SMALL_SIZE_KB KB total

## Optimization Recommendations

EOF

# Add function-specific recommendations
echo "$TOP_FUNCTIONS" | head -n 10 | while read size name; do
    if [ -n "$size" ] && [ -n "$name" ]; then
        size_kb=$(echo "scale=1; $size / 1024" | bc -l)
        if (( $(echo "$size > 2048" | bc -l) )); then
            echo "- **$name** ($size_kb KB): Consider breaking into smaller functions or optimizing algorithm" >> "$OUTPUT_FILE"
        fi
    fi
done

# Add general recommendations
cat >> "$OUTPUT_FILE" << EOF

### General Optimization Strategies

1. **Large Functions (>5KB):** Consider breaking into smaller, more focused functions
2. **Frequently Called Functions:** Profile runtime usage to identify hot paths
3. **String Operations:** Look for opportunities to use string interning or const strings
4. **Data Structures:** Consider more compact representations for large data structures
5. **Dead Code Elimination:** Ensure unused functions are properly eliminated by the linker

### Target-Specific Notes

EOF

# Add target-specific notes
if [ "$TARGET_TYPE" = "Mach-O" ]; then
    cat >> "$OUTPUT_FILE" << EOF
**⚠️ macOS Analysis:** This report analyzes a macOS Mach-O executable, which includes macOS-specific symbols that don't exist on STM32 targets.

**For STM32 Optimization:** Use STM32 toolchain build:
\`\`\`bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/stm32.cmake -DCMAKE_BUILD_TYPE=MinSizeRel .
make -j4
\`\`\`

**macOS-specific symbols to ignore:**
- \`__mh_execute_header\` - Mach-O header (not optimizable)
- \`__mh_dylib_header\` - Dynamic library header (not optimizable)
- Other \`__mh_*\` symbols - macOS system symbols

EOF
elif [ "$TARGET_TYPE" = "ELF" ]; then
    cat >> "$OUTPUT_FILE" << EOF
**✅ STM32 Analysis:** This report analyzes an ELF executable suitable for STM32 targets.

**STM32-specific optimizations:**
- Focus on ARM Cortex-M instruction set efficiency
- Consider function inlining for small, frequently called functions
- Optimize for flash memory constraints
- Use STM32-specific compiler flags (-Os, -ffunction-sections, etc.)

EOF
fi

cat >> "$OUTPUT_FILE" << EOF

### Next Steps

1. Review the top 10 largest functions for optimization opportunities
2. Consider function inlining for small, frequently called functions
3. Profile runtime to identify which large functions are actually used
4. Look for opportunities to reduce data structure sizes

EOF

echo -e "${GREEN}Report generated: $OUTPUT_FILE${NC}"
echo ""
echo -e "${BLUE}=== Quick Summary ===${NC}"
echo "Total executable size: $TOTAL_SIZE_KB KB"
echo "Total functions: $TOTAL_FUNCTIONS"
echo "Top $TOP_COUNT functions: $TOP_PERCENTAGE% of code"
echo ""
echo -e "${YELLOW}Largest functions:${NC}"
echo "$TOP_FUNCTIONS" | head -n 5 | while read size name; do
    if [ -n "$size" ] && [ -n "$name" ]; then
        size_kb=$(echo "scale=1; $size / 1024" | bc -l)
        echo "  $name: $size_kb KB"
    fi
done

echo ""
echo "View full report: cat $OUTPUT_FILE"