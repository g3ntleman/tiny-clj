#!/bin/bash

# Benchmark Runner für tiny-clj vs Standard Clojure
# Misst Startup-Zeit und Execution-Zeit

set -e

# Farben für Output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Konfiguration
BENCHMARK_DIR="."
RESULTS_DIR="../benchmark_results"
TIMEOUT_SECONDS=30

# Erstelle Results-Verzeichnis
mkdir -p "$RESULTS_DIR"

echo -e "${BLUE}🚀 Benchmark Runner für tiny-clj vs Standard Clojure${NC}"
echo "=================================================="

# Funktion zum Messen der Zeit (macOS-kompatibel)
measure_time() {
    local command="$1"
    local description="$2"
    local output_file="$3"
    
    echo -e "${YELLOW}⏱️  Messe: $description${NC}"
    
    # macOS time command hat andere Syntax - verwende gtime wenn verfügbar, sonst fallback
    if command -v gtime &> /dev/null; then
        # GNU time verfügbar
        gtime -f "%e %U %S %M" -o "$output_file.tmp" timeout "$TIMEOUT_SECONDS" bash -c "$command" 2>&1 | tee "$output_file.log"
    else
        # Fallback: verwende einfaches time und parse Output
        echo "Führe aus: $command" > "$output_file.log"
        start_time=$(date +%s)
        timeout "$TIMEOUT_SECONDS" bash -c "$command" >> "$output_file.log" 2>&1
        exit_code=$?
        end_time=$(date +%s)
        
        # Berechne elapsed time (einfache Sekunden)
        elapsed=$(echo "$end_time - $start_time" | bc -l)
        
        # Schreibe einfache Zeit-Daten
        echo "$elapsed 0 0 0" > "$output_file.tmp"
    fi
    
    # Parse die Zeit-Ergebnisse
    if [ -f "$output_file.tmp" ]; then
        local real_time=$(awk '{print $1}' "$output_file.tmp")
        local user_time=$(awk '{print $2}' "$output_file.tmp")
        local sys_time=$(awk '{print $3}' "$output_file.tmp")
        local max_memory=$(awk '{print $4}' "$output_file.tmp")
        
        echo "$description,$real_time,$user_time,$sys_time,$max_memory" >> "$RESULTS_DIR/timing_results.csv"
        echo -e "${GREEN}✅ $description: ${real_time}s (User: ${user_time}s, Sys: ${sys_time}s, Memory: ${max_memory}KB)${NC}"
    else
        echo -e "${RED}❌ Fehler beim Messen von $description${NC}"
    fi
    
    rm -f "$output_file.tmp"
}

# CSV-Header schreiben
echo "Benchmark,Real_Time,User_Time,Sys_Time,Max_Memory_KB" > "$RESULTS_DIR/timing_results.csv"

# Prüfe ob Standard Clojure verfügbar ist
if command -v clojure &> /dev/null; then
    CLOJURE_AVAILABLE=true
    echo -e "${GREEN}✅ Standard Clojure gefunden${NC}"
else
    CLOJURE_AVAILABLE=false
    echo -e "${YELLOW}⚠️  Standard Clojure nicht gefunden - überspringe Clojure-Benchmarks${NC}"
fi

# Prüfe ob tiny-clj verfügbar ist
if [ -f "../tiny-clj-repl" ]; then
    TINY_CLJ_AVAILABLE=true
    echo -e "${GREEN}✅ tiny-clj gefunden${NC}"
else
    TINY_CLJ_AVAILABLE=false
    echo -e "${RED}❌ tiny-clj nicht gefunden - kompiliere zuerst mit 'make'${NC}"
    exit 1
fi

echo ""

# Benchmark 1: Fibonacci (verwende funktionierenden Code)
echo -e "${BLUE}📊 Benchmark 1: Fibonacci (fib 20)${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_time "clojure $BENCHMARK_DIR/fibonacci.clj" "Clojure-Fibonacci" "$RESULTS_DIR/clojure_fibonacci"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    # Verwende den funktionierenden Code ohne Kommentare
    measure_time "../tiny-clj-repl -e \"(defn fib [n] (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))) (fib 20)\"" "tiny-clj-Fibonacci" "$RESULTS_DIR/tiny_clj_fibonacci"
fi

echo ""

# Benchmark 2: Sum Recursive (simple recursive benchmark)
echo -e "${BLUE}📊 Benchmark 2: Sum Recursive (sum-rec 100)${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_time "clojure -e \"(defn sum-rec [n] (if (<= n 0) 0 (+ n (sum-rec (- n 1))))) (sum-rec 100)\"" "Clojure-SumRec" "$RESULTS_DIR/clojure_sumrec"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_time "../tiny-clj-repl -e \"(defn sum-rec [n] (if (<= n 0) 0 (+ n (sum-rec (- n 1))))) (sum-rec 100)\"" "tiny-clj-SumRec" "$RESULTS_DIR/tiny_clj_sumrec"
fi

echo ""

# Benchmark 3: Let Performance (simple let benchmark)
echo -e "${BLUE}📊 Benchmark 3: Let Performance${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_time "clojure -e \"(let [x 1 y 2 z 3 a 4 b 5 c 6 d 7 e 8 f 9 g 10] (+ x y z a b c d e f g))\"" "Clojure-Let" "$RESULTS_DIR/clojure_let"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_time "../tiny-clj-repl -e \"(let [x 1 y 2 z 3 a 4 b 5 c 6 d 7 e 8 f 9 g 10] (+ x y z a b c d e f g))\"" "tiny-clj-Let" "$RESULTS_DIR/tiny_clj_let"
fi

echo ""

# Benchmark 4: Arithmetic Performance
echo -e "${BLUE}📊 Benchmark 4: Arithmetic Performance${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_time "clojure -e \"(reduce + (range 1000))\"" "Clojure-Arithmetic" "$RESULTS_DIR/clojure_arithmetic"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_time "../tiny-clj-repl -e \"(+ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20)\"" "tiny-clj-Arithmetic" "$RESULTS_DIR/tiny_clj_arithmetic"
fi

echo ""

# Benchmark 5: Function Call Performance
echo -e "${BLUE}📊 Benchmark 5: Function Call Performance${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_time "clojure -e \"(defn add [a b] (+ a b)) (reduce add (range 100))\"" "Clojure-FunctionCalls" "$RESULTS_DIR/clojure_functioncalls"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_time "../tiny-clj-repl -e \"(defn add [a b] (+ a b)) (add (add 1 2) (add 3 4))\"" "tiny-clj-FunctionCalls" "$RESULTS_DIR/tiny_clj_functioncalls"
fi

echo ""

# Zusammenfassung
echo -e "${BLUE}📈 Benchmark-Zusammenfassung${NC}"
echo "================================"

if [ -f "$RESULTS_DIR/timing_results.csv" ]; then
    echo -e "${GREEN}Ergebnisse gespeichert in: $RESULTS_DIR/timing_results.csv${NC}"
    echo ""
    echo "Detaillierte Ergebnisse:"
    cat "$RESULTS_DIR/timing_results.csv" | column -t -s ','
fi

echo ""
echo -e "${BLUE}🎯 Nächste Schritte:${NC}"
echo "1. Analysiere die Ergebnisse in $RESULTS_DIR/"
echo "2. Vergleiche Startup-Zeit vs Execution-Zeit"
echo "3. Identifiziere Performance-Bottlenecks"
echo "4. Optimiere tiny-clj basierend auf den Ergebnissen"

echo ""
echo -e "${GREEN}✅ Benchmark-Run abgeschlossen!${NC}"
