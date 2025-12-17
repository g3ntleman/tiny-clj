#!/bin/bash

# Fibonacci Benchmark: tiny-clj vs Python vs Clojure
# Reference: Clojure with JIT-Warmup (best-case JVM performance)

set -e

# Change to project root
cd "$(dirname "$0")/.."

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Configuration
FIB_N=30
WARMUP_ITERATIONS=5

echo -e "${BLUE}${BOLD}════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}${BOLD}  Fibonacci Benchmark: fib($FIB_N)${NC}"
echo -e "${BLUE}${BOLD}  Reference: Clojure with JIT-Warmup${NC}"
echo -e "${BLUE}${BOLD}════════════════════════════════════════════════════════════${NC}"
echo ""

# Check availability
CLOJURE_AVAILABLE=false
PYTHON_AVAILABLE=false
TINY_CLJ_AVAILABLE=false

if command -v clojure &> /dev/null; then
    CLOJURE_AVAILABLE=true
    echo -e "${GREEN}✓${NC} Clojure"
else
    echo -e "${RED}✗${NC} Clojure nicht gefunden"
    echo -e "${RED}  Clojure ist als Referenz erforderlich!${NC}"
    exit 1
fi

if command -v python3 &> /dev/null; then
    PYTHON_AVAILABLE=true
    echo -e "${GREEN}✓${NC} Python3"
else
    echo -e "${YELLOW}○${NC} Python3 nicht gefunden"
fi

# Build tiny-clj Release
echo -e "${YELLOW}○${NC} tiny-clj - baue Release..."
mkdir -p build

# Force clean Release build if needed
if [ -f "./CMakeCache.txt" ] && ! grep -q 'CMAKE_BUILD_TYPE:STRING=Release' ./CMakeCache.txt 2>/dev/null; then
    rm -rf ./CMakeCache.txt ./build/*
fi

BUILD_LOG=$(mktemp)
if (cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)) > "$BUILD_LOG" 2>&1; then
    if [ -f "./build/tiny-clj-repl" ]; then
        TINY_CLJ_AVAILABLE=true
        TINY_CLJ_PATH="./build/tiny-clj-repl"
        echo -e "\033[1A\033[K${GREEN}✓${NC} tiny-clj (Release)"
    else
        echo -e "${RED}✗${NC} Build erfolgreich aber Executable nicht gefunden"
        cat "$BUILD_LOG"
        rm -f "$BUILD_LOG"
        exit 1
    fi
else
    echo -e "${RED}✗${NC} Build fehlgeschlagen:"
    tail -20 "$BUILD_LOG"
    rm -f "$BUILD_LOG"
    exit 1
fi
rm -f "$BUILD_LOG"

echo ""
echo -e "${CYAN}Messe Referenzwert: Clojure mit JIT-Warmup (${WARMUP_ITERATIONS} Iterationen)...${NC}"

# Measure Clojure with JIT-Warmup (Reference)
CLOJURE_JIT_TIME=$(clojure -M -e "
(defn fib [n] (if (<= n 1) n (+ (fib (- n 1)) (fib (- n 2)))))
(dotimes [_ $WARMUP_ITERATIONS] (fib $FIB_N))
(let [start (System/nanoTime)
      _ (fib $FIB_N)
      end (System/nanoTime)]
  (println (/ (- end start) 1000000.0)))
" 2>/dev/null | tail -1)

echo ""
echo -e "${BLUE}${BOLD}════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}${BOLD}  Ergebnisse für fib($FIB_N)${NC}"
echo -e "${BLUE}${BOLD}════════════════════════════════════════════════════════════${NC}"
echo ""

printf "%-25s %12s %12s\n" "Runtime" "Zeit (ms)" "Faktor"
echo "─────────────────────────────────────────────────────"

# Reference: Clojure JIT
printf "${GREEN}%-25s %12.2f %12s${NC}\n" "Clojure (JIT)" "$CLOJURE_JIT_TIME" "1.0x ★"

# Python3
if [ "$PYTHON_AVAILABLE" = true ]; then
    PYTHON_TIME=$(python3 -c "
import time
def fib(n):
    return n if n <= 1 else fib(n-1) + fib(n-2)
# Warmup
for _ in range($WARMUP_ITERATIONS):
    fib($FIB_N)
# Measure
start = time.time()
fib($FIB_N)
print(f'{(time.time()-start)*1000:.2f}')
" 2>/dev/null)
    PYTHON_FACTOR=$(echo "scale=1; $PYTHON_TIME / $CLOJURE_JIT_TIME" | bc)
    printf "%-25s %12.2f %12s\n" "Python3" "$PYTHON_TIME" "${PYTHON_FACTOR}x"
fi

# tiny-clj
if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    TINY_TIME=$($TINY_CLJ_PATH -e "
(time (do (defn fib [n] (if (<= n 1) n (+ (fib (- n 1)) (fib (- n 2))))) (fib $FIB_N)))
" 2>&1 | grep "Elapsed time" | sed 's/.*Elapsed time: \([0-9.]*\) msecs.*/\1/')
    TINY_FACTOR=$(echo "scale=1; $TINY_TIME / $CLOJURE_JIT_TIME" | bc)
    printf "${YELLOW}%-25s %12.2f %12s${NC}\n" "tiny-clj (Release)" "$TINY_TIME" "${TINY_FACTOR}x"
fi

echo ""
echo -e "${BLUE}${BOLD}════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}Legende:${NC}"
echo "  ★  = Referenzwert (Clojure mit JIT-Warmup)"
echo "  Faktor = Wie viel langsamer als Referenz"
echo ""
echo -e "${CYAN}Interpreter-Typen:${NC}"
echo "  Clojure JIT  = JVM mit Just-In-Time Compilation"
echo "  Python3      = CPython Bytecode Interpreter"
echo "  tiny-clj     = C AST-Walking Interpreter"
echo ""
