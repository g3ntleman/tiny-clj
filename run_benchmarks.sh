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
BENCHMARK_DIR="libs/clojure/benchmarksgame"
RESULTS_DIR="benchmark_results"
TIMEOUT_SECONDS=30

# Erstelle Results-Verzeichnis
mkdir -p "$RESULTS_DIR"

echo -e "${BLUE}🚀 Benchmark Runner für tiny-clj vs Standard Clojure${NC}"
echo "=================================================="

# Funktion zum Messen der Zeit
measure_time() {
    local command="$1"
    local description="$2"
    local output_file="$3"
    
    echo -e "${YELLOW}⏱️  Messe: $description${NC}"
    
    # Verwende 'time' mit Format-String für präzise Messung
    /usr/bin/time -f "%e %U %S %M" -o "$output_file.tmp" timeout "$TIMEOUT_SECONDS" bash -c "$command" 2>&1 | tee "$output_file.log"
    
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
if [ -f "./tiny-clj-repl" ]; then
    TINY_CLJ_AVAILABLE=true
    echo -e "${GREEN}✅ tiny-clj gefunden${NC}"
else
    TINY_CLJ_AVAILABLE=false
    echo -e "${RED}❌ tiny-clj nicht gefunden - kompiliere zuerst mit 'make'${NC}"
    exit 1
fi

echo ""

# Benchmark 1: Fibonacci
if [ -f "$BENCHMARK_DIR/fibonacci.clj" ]; then
    echo -e "${BLUE}📊 Benchmark 1: Fibonacci (fib 20)${NC}"
    
    if [ "$CLOJURE_AVAILABLE" = true ]; then
        measure_time "clojure $BENCHMARK_DIR/fibonacci.clj" "Clojure-Fibonacci" "$RESULTS_DIR/clojure_fibonacci"
    fi
    
    if [ "$TINY_CLJ_AVAILABLE" = true ]; then
        measure_time "./tiny-clj-repl -f $BENCHMARK_DIR/fibonacci.clj" "tiny-clj-Fibonacci" "$RESULTS_DIR/tiny_clj_fibonacci"
    fi
    
    echo ""
fi

# Benchmark 2: Let Performance
if [ -f "$BENCHMARK_DIR/let_performance.clj" ]; then
    echo -e "${BLUE}📊 Benchmark 2: Let Performance${NC}"
    
    if [ "$CLOJURE_AVAILABLE" = true ]; then
        measure_time "clojure $BENCHMARK_DIR/let_performance.clj" "Clojure-Let" "$RESULTS_DIR/clojure_let"
    fi
    
    if [ "$TINY_CLJ_AVAILABLE" = true ]; then
        measure_time "./tiny-clj-repl -f $BENCHMARK_DIR/let_performance.clj" "tiny-clj-Let" "$RESULTS_DIR/tiny_clj_let"
    fi
    
    echo ""
fi

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
