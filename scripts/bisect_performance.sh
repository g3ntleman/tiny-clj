#!/bin/bash
# Git Bisect Script for Performance Regression Detection
# Uses git bisect to find commits that introduced performance regressions

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Git Bisect for Performance Regression ===${NC}"
echo ""

# Configuration
BUILD_DIR="build"
BENCHMARK_FILE="benchmarks/fibonacci.clj"
FIB_N=20
ITERATIONS=100
THRESHOLD_MS=10.0  # Performance threshold in milliseconds

# Check if we're already in a bisect session
if git bisect log | grep -q "git bisect start"; then
    echo -e "${YELLOW}Already in bisect session. Resetting...${NC}"
    git bisect reset
fi

# Build function
build_project() {
    echo -e "${BLUE}Building project...${NC}"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. > /dev/null 2>&1
    make tiny-clj-repl -j$(nproc) > /dev/null 2>&1
    cd ..
}

# Benchmark function
run_benchmark() {
    local result
    result=$(cd "$BUILD_DIR" && ./tiny-clj-repl -f "../$BENCHMARK_FILE" 2>&1 | grep -E "Time:|fib\($FIB_N\)" | tail -1)
    
    if [ -z "$result" ]; then
        echo -e "${RED}Benchmark failed${NC}"
        return 1
    fi
    
    # Extract time value (assuming format like "Time: 5.23ms")
    local time_ms
    time_ms=$(echo "$result" | grep -oE '[0-9]+\.[0-9]+' | head -1)
    
    if [ -z "$time_ms" ]; then
        echo -e "${RED}Could not extract time${NC}"
        return 1
    fi
    
    echo -e "${GREEN}Performance: ${time_ms}ms${NC}"
    
    # Compare with threshold
    if (( $(echo "$time_ms > $THRESHOLD_MS" | bc -l) )); then
        echo -e "${RED}Performance regression detected (>${THRESHOLD_MS}ms)${NC}"
        return 1  # Bad commit
    else
        echo -e "${GREEN}Performance OK (<=${THRESHOLD_MS}ms)${NC}"
        return 0  # Good commit
    fi
}

# Main bisect function
main() {
    if [ $# -lt 2 ]; then
        echo "Usage: $0 <good_commit> <bad_commit>"
        echo "Example: $0 HEAD~10 HEAD"
        exit 1
    fi
    
    local good_commit="$1"
    local bad_commit="$2"
    
    echo -e "${BLUE}Starting bisect:${NC}"
    echo "  Good commit: $good_commit"
    echo "  Bad commit: $bad_commit"
    echo "  Threshold: ${THRESHOLD_MS}ms"
    echo ""
    
    git bisect start "$bad_commit" "$good_commit"
    
    # Run bisect
    git bisect run bash -c "
        cd '$(pwd)' && \
        mkdir -p '$BUILD_DIR' && \
        cd '$BUILD_DIR' && \
        cmake .. > /dev/null 2>&1 && \
        make tiny-clj-repl -j\$(nproc) > /dev/null 2>&1 && \
        cd .. && \
        result=\$(cd '$BUILD_DIR' && ./tiny-clj-repl -f '../$BENCHMARK_FILE' 2>&1 | grep -E 'Time:|fib\($FIB_N\)' | tail -1) && \
        time_ms=\$(echo \"\$result\" | grep -oE '[0-9]+\.[0-9]+' | head -1) && \
        if [ -z \"\$time_ms\" ]; then exit 125; fi && \
        if (( \$(echo \"\$time_ms > $THRESHOLD_MS\" | bc -l) )); then exit 1; else exit 0; fi
    " || true
    
    echo ""
    echo -e "${GREEN}Bisect complete!${NC}"
    git bisect log
}

# Build and test function (called by bisect run)
if [ "$1" == "--build-and-test" ]; then
    build_project
    run_benchmark
    exit $?
fi

# Run main function
main "$@"
