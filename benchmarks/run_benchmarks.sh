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

# Funktion zum Messen der Execution-Zeit aus den Log-Dateien
measure_execution_time() {
    local command="$1"
    local description="$2"
    local output_file="$3"
    
    echo -e "${YELLOW}⏱️  Messe: $description${NC}"
    
    # Führe den Befehl aus und speichere die Ausgabe
    echo "Führe aus: $command" > "$output_file.log"
    timeout "$TIMEOUT_SECONDS" bash -c "$command" >> "$output_file.log" 2>&1
    
    # Extrahiere die Execution-Zeit aus der Ausgabe
    local execution_time=$(grep "Elapsed time:" "$output_file.log" | sed 's/.*Elapsed time: \([0-9.]*\) msecs.*/\1/' | head -1)
    
    if [ -n "$execution_time" ]; then
        echo "$description,$execution_time,0,0,0" >> "$RESULTS_DIR/timing_results.csv"
        echo -e "${GREEN}✅ $description: ${execution_time}ms (Execution time only)${NC}"
    else
        echo -e "${RED}❌ Konnte Execution-Zeit für $description nicht extrahieren${NC}"
        echo "$description,0,0,0,0" >> "$RESULTS_DIR/timing_results.csv"
    fi
}

# CSV-Header schreiben
echo "Benchmark,Execution_Time_ms,User_Time_ms,Sys_Time_ms,Max_Memory_KB" > "$RESULTS_DIR/timing_results.csv"

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
    measure_execution_time "clojure $BENCHMARK_DIR/fibonacci.clj" "Clojure-Fibonacci" "$RESULTS_DIR/clojure_fibonacci"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_execution_time "../tiny-clj-repl -f ./fibonacci.clj" "tiny-clj-Fibonacci" "$RESULTS_DIR/tiny_clj_fibonacci"
fi

echo ""

# Benchmark 2: Sum Recursive (simple recursive benchmark)
echo -e "${BLUE}📊 Benchmark 2: Sum Recursive (sum-rec 100)${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_execution_time "clojure ./sum_rec.clj" "Clojure-SumRec" "$RESULTS_DIR/clojure_sumrec"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_execution_time "../tiny-clj-repl -f ./sum_rec.clj" "tiny-clj-SumRec" "$RESULTS_DIR/tiny_clj_sumrec"
fi

echo ""

# Benchmark 3: Let Performance (simple let benchmark)
echo -e "${BLUE}📊 Benchmark 3: Let Performance${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_execution_time "clojure ./let_performance.clj" "Clojure-Let" "$RESULTS_DIR/clojure_let"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_execution_time "../tiny-clj-repl -f ./let_performance.clj" "tiny-clj-Let" "$RESULTS_DIR/tiny_clj_let"
fi

echo ""

# Benchmark 4: Arithmetic Performance
echo -e "${BLUE}📊 Benchmark 4: Arithmetic Performance${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_execution_time "clojure ./arithmetic_performance.clj" "Clojure-Arithmetic" "$RESULTS_DIR/clojure_arithmetic"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_execution_time "../tiny-clj-repl -f ./arithmetic_performance.clj" "tiny-clj-Arithmetic" "$RESULTS_DIR/tiny_clj_arithmetic"
fi

echo ""

# Benchmark 5: Function Call Performance
echo -e "${BLUE}📊 Benchmark 5: Function Call Performance${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_execution_time "clojure ./function_call_performance.clj" "Clojure-FunctionCalls" "$RESULTS_DIR/clojure_functioncalls"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_execution_time "../tiny-clj-repl -f ./function_call_performance.clj" "tiny-clj-FunctionCalls" "$RESULTS_DIR/tiny_clj_functioncalls"
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
