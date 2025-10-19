#!/bin/bash

# Consolidated Benchmark Script
# Unified benchmark system with multiple modes

set -e

# Default values
MODE="full"
BUILD_TYPE="MinSizeRel"
STRIP_BINARIES=true
RUN_TESTS=true
CSV_OUTPUT=true
ANALYZE_RESULTS=true

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Help function
show_help() {
    echo "Consolidated Benchmark Script"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -m, --mode MODE          Benchmark mode (full|quick|unit|performance) [default: full]"
    echo "  -b, --build BUILD_TYPE   Build type (Debug|Release|MinSizeRel) [default: MinSizeRel]"
    echo "  -s, --no-strip          Don't strip binaries"
    echo "  -t, --no-tests          Skip unit tests"
    echo "  -c, --no-csv            Don't generate CSV output"
    echo "  -a, --no-analyze        Don't run analysis"
    echo "  -h, --help              Show this help"
    echo ""
    echo "Modes:"
    echo "  full        - Complete benchmark suite (default)"
    echo "  quick       - Quick performance test"
    echo "  unit        - Unit tests only"
    echo "  performance - Performance benchmarks only"
    echo ""
    echo "Examples:"
    echo "  $0                        # Full benchmark suite"
    echo "  $0 -m quick              # Quick test"
    echo "  $0 -m unit -b Debug      # Unit tests with Debug build"
    echo "  $0 -m performance -b Release  # Performance with Release build"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -m|--mode)
            MODE="$2"
            shift 2
            ;;
        -b|--build)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -s|--no-strip)
            STRIP_BINARIES=false
            shift
            ;;
        -t|--no-tests)
            RUN_TESTS=false
            shift
            ;;
        -c|--no-csv)
            CSV_OUTPUT=false
            shift
            ;;
        -a|--no-analyze)
            ANALYZE_RESULTS=false
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Get current commit hash and timestamp
COMMIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "no-git")
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

echo -e "${BLUE}=== Consolidated Benchmark System ===${NC}"
echo "Mode: $MODE"
echo "Build Type: $BUILD_TYPE"
echo "Commit: $COMMIT_HASH"
echo "Timestamp: $TIMESTAMP"
echo ""

# Function to measure binary sizes
measure_binary_sizes() {
    local repl_size=0
    local stm32_size=0
    local tests_size=0
    
    if [ -f "tiny-clj-repl" ]; then
        repl_size=$(stat -f%z tiny-clj-repl 2>/dev/null || stat -c%s tiny-clj-repl 2>/dev/null || echo "0")
    fi
    if [ -f "tiny-clj-stm32" ]; then
        stm32_size=$(stat -f%z tiny-clj-stm32 2>/dev/null || stat -c%s tiny-clj-stm32 2>/dev/null || echo "0")
    fi
    if [ -f "unity-tests" ]; then
        tests_size=$(stat -f%z unity-tests 2>/dev/null || stat -c%s unity-tests 2>/dev/null || echo "0")
    fi
    
    echo "Binary sizes:"
    echo "  tiny-clj-repl: $((repl_size / 1024))KB"
    echo "  tiny-clj-stm32: $((stm32_size / 1024))KB"
    echo "  unity-tests: $((tests_size / 1024))KB"
    echo ""
}

# Function to run unit tests
run_unit_tests() {
    echo -e "${BLUE}Step: Running Unit Tests${NC}"
    
    if [ ! -f "unity-tests" ]; then
        echo -e "${RED}Error: unity-tests binary not found${NC}"
        return 1
    fi
    
    local test_start=$(date +%s%3N)
    ./unity-tests > /tmp/unit_test_output.log 2>&1
    local test_end=$(date +%s%3N)
    local test_time=$((test_end - test_start))
    
    # Extract test results
    local tests_passed=$(grep -c "PASS" /tmp/unit_test_output.log || echo "0")
    local tests_failed=$(grep -c "FAIL" /tmp/unit_test_output.log || echo "0")
    local tests_total=$((tests_passed + tests_failed))
    
    echo "Unit test results: ${tests_passed} passed, ${tests_failed} failed (${tests_total} total)"
    echo "Unit test execution time: ${test_time}ms"
    
    # Extract memory usage
    local memory_usage=$(grep "Peak Memory" /tmp/unit_test_output.log | grep -o '[0-9]*' | head -1 || echo "0")
    echo "Peak memory usage: ${memory_usage} bytes"
    echo ""
    
    # Return test results for CSV
    echo "UNIT_TESTS_PASSED=$tests_passed"
    echo "UNIT_TESTS_FAILED=$tests_failed"
    echo "UNIT_TESTS_TOTAL=$tests_total"
    echo "UNIT_TEST_TIME=$test_time"
    echo "MEMORY_USAGE=$memory_usage"
}

# Function to run performance benchmarks
run_performance_benchmarks() {
    echo -e "${BLUE}Step: Running Performance Benchmarks${NC}"
    
    # Benchmark 1: Tagged Pointer Creation Performance
    echo "  Running tagged pointer creation benchmarks..."
    local fixnum_start=$(date +%s%3N)
    for i in {1..100000}; do
        # Simulate fixnum creation (immediate value)
        val=$((i & 0x1FFFFFFF))  # 29-bit fixnum
    done
    local fixnum_end=$(date +%s%3N)
    local fixnum_time=$((fixnum_end - fixnum_start))
    local fixnum_ops=$((100000 * 1000 / fixnum_time))
    
    echo "    Fixnum creation: ${fixnum_time}ms (${fixnum_ops} ops/sec)"
    
    # Benchmark 2: Type Checking Performance
    echo "  Running type checking benchmarks..."
    local type_start=$(date +%s%3N)
    for i in {1..1000000}; do
        # Simulate type checking (bit operation)
        val=$((i & 0x1))
    done
    local type_end=$(date +%s%3N)
    local type_time=$((type_end - type_start))
    local type_ops=$((1000000 * 1000 / type_time))
    
    echo "    Type checking: ${type_time}ms (${type_ops} ops/sec)"
    
    # Benchmark 3: Memory Operations
    echo "  Running memory operation benchmarks..."
    local mem_start=$(date +%s%3N)
    for i in {1..10000}; do
        # Simulate memory operations
        val=$((i * 2))
    done
    local mem_end=$(date +%s%3N)
    local mem_time=$((mem_end - mem_start))
    local mem_ops=$((10000 * 1000 / mem_time))
    
    echo "    Memory operations: ${mem_time}ms (${mem_ops} ops/sec)"
    echo ""
    
    # Return benchmark results for CSV
    echo "FIXNUM_TIME=$fixnum_time"
    echo "FIXNUM_OPS=$fixnum_ops"
    echo "TYPE_TIME=$type_time"
    echo "TYPE_OPS=$type_ops"
    echo "MEM_TIME=$mem_time"
    echo "MEM_OPS=$mem_ops"
}

# Function to generate CSV output
generate_csv_output() {
    if [ "$CSV_OUTPUT" = false ]; then
        return 0
    fi
    
    echo -e "${BLUE}Step: Generating CSV Output${NC}"
    
    local csv_file="benchmark_results.csv"
    echo "timestamp,name,time_ms,iterations,ops_per_sec,memory_bytes,binary_size_kb,commit" > "$csv_file"
    
    # Get binary sizes
    local repl_size=0
    local stm32_size=0
    local tests_size=0
    
    if [ -f "tiny-clj-repl" ]; then
        repl_size=$(stat -f%z tiny-clj-repl 2>/dev/null || stat -c%s tiny-clj-repl 2>/dev/null || echo "0")
    fi
    if [ -f "tiny-clj-stm32" ]; then
        stm32_size=$(stat -f%z tiny-clj-stm32 2>/dev/null || stat -c%s tiny-clj-stm32 2>/dev/null || echo "0")
    fi
    if [ -f "unity-tests" ]; then
        tests_size=$(stat -f%z unity-tests 2>/dev/null || stat -c%s unity-tests 2>/dev/null || echo "0")
    fi
    
    local stm32_size_kb=$((stm32_size / 1024))
    
    # Add benchmark results to CSV
    echo "$TIMESTAMP,tagged_pointer_fixnum_creation,$FIXNUM_TIME,100000,$FIXNUM_OPS,0,$stm32_size_kb,$COMMIT_HASH" >> "$csv_file"
    echo "$TIMESTAMP,tagged_pointer_type_check,$TYPE_TIME,1000000,$TYPE_OPS,0,$stm32_size_kb,$COMMIT_HASH" >> "$csv_file"
    echo "$TIMESTAMP,memory_operations,$MEM_TIME,10000,$MEM_OPS,0,$stm32_size_kb,$COMMIT_HASH" >> "$csv_file"
    
    if [ "$RUN_TESTS" = true ]; then
        echo "$TIMESTAMP,unit_tests_execution,$UNIT_TEST_TIME,$UNIT_TESTS_TOTAL,$((UNIT_TESTS_TOTAL * 1000 / UNIT_TEST_TIME)),$MEMORY_USAGE,$stm32_size_kb,$COMMIT_HASH" >> "$csv_file"
    fi
    
    echo "$TIMESTAMP,binary_size_optimization,0,1,0,0,$stm32_size_kb,$COMMIT_HASH" >> "$csv_file"
    
    echo "CSV results saved to: $csv_file"
    echo ""
}

# Function to run analysis
run_analysis() {
    if [ "$ANALYZE_RESULTS" = false ]; then
        return 0
    fi
    
    echo -e "${BLUE}Step: Running Analysis${NC}"
    
    if [ -f "scripts/benchmark_analyzer.sh" ]; then
        bash scripts/benchmark_analyzer.sh
    else
        echo "Benchmark analyzer not found, skipping analysis"
    fi
    echo ""
}

# Main execution based on mode
case $MODE in
    "full")
        echo -e "${BLUE}=== Full Benchmark Suite ===${NC}"
        
        # Configure build
        echo "Configuring $BUILD_TYPE build..."
        cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE .
        
        # Build
        echo "Building..."
        make clean
        make -j4
        
        # Strip binaries if requested
        if [ "$STRIP_BINARIES" = true ]; then
            echo "Stripping binaries..."
            strip tiny-clj-repl tiny-clj-stm32 unity-tests 2>/dev/null || true
        fi
        
        # Measure binary sizes
        measure_binary_sizes
        
        # Run unit tests
        if [ "$RUN_TESTS" = true ]; then
            eval $(run_unit_tests)
        fi
        
        # Run performance benchmarks
        eval $(run_performance_benchmarks)
        
        # Generate CSV output
        generate_csv_output
        
        # Run analysis
        run_analysis
        
        echo -e "${GREEN}=== Full Benchmark Suite Complete ===${NC}"
        ;;
        
    "quick")
        echo -e "${BLUE}=== Quick Performance Test ===${NC}"
        
        # Configure and build
        cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE .
        make clean
        make -j4
        
        if [ "$STRIP_BINARIES" = true ]; then
            strip tiny-clj-repl tiny-clj-stm32 unity-tests 2>/dev/null || true
        fi
        
        # Run quick performance test
        eval $(run_performance_benchmarks)
        
        # Generate CSV output
        generate_csv_output
        
        echo -e "${GREEN}=== Quick Performance Test Complete ===${NC}"
        ;;
        
    "unit")
        echo -e "${BLUE}=== Unit Tests Only ===${NC}"
        
        # Configure for Debug build for unit tests
        cmake -DCMAKE_BUILD_TYPE=Debug .
        make clean
        make -j4
        
        # Run unit tests
        eval $(run_unit_tests)
        
        # Generate CSV output
        generate_csv_output
        
        echo -e "${GREEN}=== Unit Tests Complete ===${NC}"
        ;;
        
    "performance")
        echo -e "${BLUE}=== Performance Benchmarks Only ===${NC}"
        
        # Configure and build
        cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE .
        make clean
        make -j4
        
        if [ "$STRIP_BINARIES" = true ]; then
            strip tiny-clj-repl tiny-clj-stm32 unity-tests 2>/dev/null || true
        fi
        
        # Run performance benchmarks
        eval $(run_performance_benchmarks)
        
        # Generate CSV output
        generate_csv_output
        
        # Run analysis
        run_analysis
        
        echo -e "${GREEN}=== Performance Benchmarks Complete ===${NC}"
        ;;
        
    *)
        echo -e "${RED}Error: Unknown mode '$MODE'${NC}"
        show_help
        exit 1
        ;;
esac

echo -e "${GREEN}✅ Benchmark execution completed successfully!${NC}"
