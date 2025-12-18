#!/bin/bash

# Profile tiny-clj using macOS Instruments
# This script tests if Instruments works correctly for profiling

set -e

# Change to project root
cd "$(dirname "$0")/.."

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

BENCHMARK_FILE="./benchmarks/fibonacci_naive.clj"
PROFILE_OUTPUT="./benchmark_profile_instruments.trace"

echo -e "${BLUE}🔬 Testing Instruments Profiling${NC}"
echo "=================================================="
echo ""

# Check if we're on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    echo -e "${RED}❌ Instruments is only available on macOS${NC}"
    exit 1
fi

# Check if Instruments is available
INSTRUMENTS_PATH=""
if command -v instruments &> /dev/null; then
    INSTRUMENTS_PATH=$(which instruments)
elif [ -f "/Applications/Xcode.app/Contents/Developer/usr/bin/instruments" ]; then
    INSTRUMENTS_PATH="/Applications/Xcode.app/Contents/Developer/usr/bin/instruments"
fi

if [ -z "$INSTRUMENTS_PATH" ]; then
    echo -e "${YELLOW}⚠️  Instruments not found in PATH or standard location.${NC}"
    echo "   Instruments requires full Xcode.app installation (not just Command Line Tools)."
    echo ""
    echo -e "${BLUE}Alternative: Using 'sample' for profiling (available with Command Line Tools)${NC}"
    echo ""
    
    # Check if sample is available
    if command -v sample &> /dev/null; then
        echo -e "${GREEN}✅ Using 'sample' for profiling instead${NC}"
        echo ""
        echo "Running sample profiler..."
        echo ""
        
        # Run the benchmark in background
        "$TINY_CLJ_PATH" -f "$BENCHMARK_FILE" &
        BENCHMARK_PID=$!
        
        # Sample for 10 seconds (should be enough for fib(20))
        sample "$BENCHMARK_PID" 10 -f "$PROFILE_OUTPUT.txt" 2>&1
        
        wait $BENCHMARK_PID
        
        if [ -f "$PROFILE_OUTPUT.txt" ]; then
            echo ""
            echo -e "${GREEN}✅ Sample profiling completed!${NC}"
            echo "Profile saved to: $PROFILE_OUTPUT.txt"
            echo ""
            echo "To view the profile:"
            echo "  cat $PROFILE_OUTPUT.txt"
            echo ""
            echo "Note: For full Instruments GUI, install Xcode.app from App Store"
        else
            echo -e "${RED}❌ Sample profiling failed${NC}"
            exit 1
        fi
        exit 0
    else
        echo -e "${RED}❌ Neither Instruments nor 'sample' found.${NC}"
        echo "   Please install Xcode.app for Instruments, or ensure Command Line Tools are installed for 'sample'"
        exit 1
    fi
fi

# Find tiny-clj executable
TINY_CLJ_PATH="./build/tiny-clj-repl"
if [ ! -f "$TINY_CLJ_PATH" ]; then
    echo -e "${YELLOW}⚠️  tiny-clj-repl not found. Building...${NC}"
    cmake --build build --target tiny-clj-repl
fi

if [ ! -f "$TINY_CLJ_PATH" ]; then
    echo -e "${RED}❌ Could not build tiny-clj-repl${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Found tiny-clj-repl: $TINY_CLJ_PATH${NC}"
echo ""

# Test Instruments with Time Profiler
echo -e "${BLUE}📊 Running Instruments Time Profiler...${NC}"
echo "This will profile the fibonacci benchmark"
echo ""

# Use instruments to profile with Time Profiler template
# -t: template (Time Profiler)
# -D: output trace file
# --timeout: max time to run (30 seconds should be enough for fib(20))
"$INSTRUMENTS_PATH" -t "Time Profiler" -D "$PROFILE_OUTPUT" "$TINY_CLJ_PATH" -f "$BENCHMARK_FILE" 2>&1 | tee /tmp/instruments_output.log

if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✅ Instruments profiling completed successfully!${NC}"
    echo ""
    echo "Trace file saved to: $PROFILE_OUTPUT"
    echo ""
    echo "To view the profile:"
    echo "  open $PROFILE_OUTPUT"
    echo ""
    echo "Or use Instruments GUI:"
    echo "  instruments -l"
    echo ""
    
    # Check if trace file was created
    if [ -f "$PROFILE_OUTPUT" ]; then
        echo -e "${GREEN}✅ Trace file exists and is ready for analysis${NC}"
        FILE_SIZE=$(du -h "$PROFILE_OUTPUT" | cut -f1)
        echo "   File size: $FILE_SIZE"
    else
        echo -e "${YELLOW}⚠️  Trace file not found, but Instruments completed without errors${NC}"
    fi
else
    echo ""
    echo -e "${RED}❌ Instruments profiling failed${NC}"
    echo ""
    echo "Output from Instruments:"
    cat /tmp/instruments_output.log
    exit 1
fi



















