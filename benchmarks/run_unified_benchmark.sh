#!/bin/bash

# Unified Benchmark Runner
# Runs benchmarks for both Clojure and tiny-clj
# Loads historical results and appends new ones

set -e

cd "$(dirname "$0")/.."

BENCHMARK_DIR="./benchmarks"
BENCHMARK_FILE="$BENCHMARK_DIR/unified_benchmark.clj"

# Colors
BLUE='\033[0;34m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}=== Unified Benchmark Runner ==="
echo "Runs benchmarks for Clojure and tiny-clj"
echo "Loads historical results and appends new ones"
echo -e "${NC}"

# Check Clojure
if command -v clojure &> /dev/null; then
    echo -e "${GREEN}✅ Clojure found${NC}"
    echo ""
    echo -e "${BLUE}📊 Running Clojure benchmarks (20s JVM warmup)${NC}"
    clojure "$BENCHMARK_FILE" 2>&1 | grep -v "^Loading\|^Picked\|^Downloading" || true
    echo ""
else
    echo -e "${YELLOW}⚠️  Clojure not found${NC}"
fi

# Check tiny-clj
if [ -f "./tiny-clj-repl" ]; then
    TINY_CLJ_REPL="./tiny-clj-repl"
elif [ -f "./build-release/tiny-clj-repl" ]; then
    TINY_CLJ_REPL="./build-release/tiny-clj-repl"
else
    TINY_CLJ_REPL=""
fi

if [ -n "$TINY_CLJ_REPL" ]; then
    echo -e "${GREEN}✅ tiny-clj found${NC}"
    echo ""
    echo -e "${BLUE}📊 Running tiny-clj benchmarks (no warmup needed)${NC}"
    "$TINY_CLJ_REPL" < "$BENCHMARK_FILE" 2>&1 | grep -v "Loading\|Memory\|🔍\|===\|user=>\|clojure.core=>" || true
    echo ""
else
    echo -e "${YELLOW}⚠️  tiny-clj not found${NC}"
fi

# Show results
if [ -f "benchmark_results/unified_results.csv" ]; then
    echo -e "${GREEN}📈 Results saved${NC}"
    echo ""
    echo "Recent results:"
    tail -10 "benchmark_results/unified_results.csv" | column -t -s ','
fi

echo ""
echo -e "${GREEN}✅ Benchmark run completed!${NC}"
