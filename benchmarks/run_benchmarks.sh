#!/bin/bash

# Benchmark Runner für tiny-clj vs Standard Clojure
# Misst Startup-Zeit und Execution-Zeit

set -e

# Wechsle ins Root-Verzeichnis des Projekts
cd "$(dirname "$0")/.."

# Farben für Output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Konfiguration
BENCHMARK_DIR="./benchmarks"
RESULTS_DIR="./benchmark_results"
TIMEOUT_SECONDS=10

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
    local execution_time=$(grep -E "Elapsed time:|println.*Elapsed time:" "$output_file.log" | sed 's/.*Elapsed time: \([0-9.]*\) msecs.*/\1/' | head -1)
    
    # Falls keine "Elapsed time:" gefunden wurde, messe die echte Ausführungszeit
    if [ -z "$execution_time" ]; then
        # Messe Startup-Zeit (leerer Befehl)
        local startup_start=$(date +%s.%3N)
        timeout "$TIMEOUT_SECONDS" bash -c "$TINY_CLJ_PATH -e 'nil'" > /dev/null 2>&1
        local startup_end=$(date +%s.%3N)
        local startup_time=$(echo "scale=3; ($startup_end - $startup_start) * 1000" | bc)
        
        # Messe Gesamtzeit
        local total_start=$(date +%s.%3N)
        timeout "$TIMEOUT_SECONDS" bash -c "$command" > /dev/null 2>&1
        local total_end=$(date +%s.%3N)
        local total_time=$(echo "scale=3; ($total_end - $total_start) * 1000" | bc)
        
        # Berechne reine Ausführungszeit (Gesamtzeit - Startup-Zeit)
        execution_time=$(echo "scale=3; $total_time - $startup_time" | bc)
        
        # Stelle sicher, dass die Zeit nicht negativ ist
        if (( $(echo "$execution_time < 0" | bc -l) )); then
            execution_time="0.1"
        fi
    fi
    
    if [ -n "$execution_time" ]; then
        echo "$description,$execution_time,0,0,0" >> "$RESULTS_DIR/timing_results.csv"
        echo -e "${GREEN}✅ $description: ${execution_time}ms (Execution time only)${NC}"
        
        # Logge tiny-clj Ergebnisse separat
        if [[ "$description" == *"tiny-clj"* ]]; then
            local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
            local unix_timestamp=$(date +%s)
            local commit_hash=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
            local build_type="MinSizeRel"  # Standard Build-Type
            local benchmark_name=$(echo "$description" | sed 's/tiny-clj-//')
            echo "$timestamp,$unix_timestamp,$benchmark_name,$execution_time,$commit_hash,$build_type" >> "$TINY_CLJ_LOG"
        fi
    else
        echo -e "${RED}❌ Konnte Execution-Zeit für $description nicht extrahieren${NC}"
        echo "$description,0,0,0,0" >> "$RESULTS_DIR/timing_results.csv"
        
        # Logge auch fehlgeschlagene tiny-clj Benchmarks
        if [[ "$description" == *"tiny-clj"* ]]; then
            local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
            local unix_timestamp=$(date +%s)
            local commit_hash=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
            local build_type="MinSizeRel"
            local benchmark_name=$(echo "$description" | sed 's/tiny-clj-//')
            echo "$timestamp,$unix_timestamp,$benchmark_name,0,$commit_hash,$build_type" >> "$TINY_CLJ_LOG"
        fi
    fi
}

# CSV-Header schreiben
echo "Benchmark,Execution_Time_ms,User_Time_ms,Sys_Time_ms,Max_Memory_KB" > "$RESULTS_DIR/timing_results.csv"

# CSV-Header für tiny-clj Log schreiben (falls nicht existiert)
TINY_CLJ_LOG="$RESULTS_DIR/tiny_clj_performance_log.csv"
if [ ! -f "$TINY_CLJ_LOG" ]; then
    echo "Timestamp,Unix_Timestamp,Benchmark_Name,Execution_Time_ms,Commit_Hash,Build_Type" > "$TINY_CLJ_LOG"
fi

# Prüfe ob Standard Clojure verfügbar ist
if command -v clojure &> /dev/null; then
    CLOJURE_AVAILABLE=true
    echo -e "${GREEN}✅ Standard Clojure gefunden${NC}"
else
    CLOJURE_AVAILABLE=false
    echo -e "${YELLOW}⚠️  Standard Clojure nicht gefunden - überspringe Clojure-Benchmarks${NC}"
fi

# Prüfe ob tiny-clj verfügbar ist
if [ -f "./build-release/tiny-clj-repl" ]; then
    TINY_CLJ_AVAILABLE=true
    TINY_CLJ_PATH="./build-release/tiny-clj-repl"
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

# tiny-clj Fibonacci benchmark
if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_execution_time "echo '(defn fib [n] (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))) (fib 20)' | timeout 30 $TINY_CLJ_REPL" "tiny-clj-Fibonacci" "$RESULTS_DIR/tiny_clj_fibonacci"
else
    echo -e "${YELLOW}⚠️  tiny-clj Fibonacci übersprungen (tiny-clj nicht verfügbar)${NC}"
fi

echo ""

# Benchmark 2: Sum Recursive (simple recursive benchmark)
echo -e "${BLUE}📊 Benchmark 2: Sum Recursive (sum-rec 100)${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_execution_time "clojure $BENCHMARK_DIR/sum_rec.clj" "Clojure-SumRec" "$RESULTS_DIR/clojure_sumrec"
fi

# tiny-clj SumRec benchmark
if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_execution_time "echo 'user=> (defn sum-rec [n] (if (= n 0) 0 (+ n (sum-rec (- n 1))))) (sum-rec 100)' | $TINY_CLJ_REPL" "tiny-clj-SumRec" "$RESULTS_DIR/tiny_clj_sumrec"
else
    echo -e "${YELLOW}⚠️  tiny-clj SumRec übersprungen (tiny-clj nicht verfügbar)${NC}"
fi

echo ""

# Benchmark 3: Let Performance (simple let benchmark)
echo -e "${BLUE}📊 Benchmark 3: Let Performance${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_execution_time "clojure $BENCHMARK_DIR/let_performance.clj" "Clojure-Let" "$RESULTS_DIR/clojure_let"
fi

# tiny-clj Let benchmark
if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_execution_time "echo 'user=> (defn test-let [] (let [a 1 b 2 c 3] (+ a b c))) (dotimes [i 1000] (test-let))' | $TINY_CLJ_REPL" "tiny-clj-Let" "$RESULTS_DIR/tiny_clj_let"
else
    echo -e "${YELLOW}⚠️  tiny-clj Let übersprungen (tiny-clj nicht verfügbar)${NC}"
fi

echo ""

# Benchmark 4: Arithmetic Performance
echo -e "${BLUE}📊 Benchmark 4: Arithmetic Performance${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_execution_time "clojure $BENCHMARK_DIR/arithmetic_performance.clj" "Clojure-Arithmetic" "$RESULTS_DIR/clojure_arithmetic"
fi

# tiny-clj Arithmetic benchmark
if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_execution_time "echo 'user=> (defn test-arithmetic [] (let [a 1 b 2 c 3 d 4 e 5] (+ (* a b) (- c d) (/ e 2)))) (dotimes [i 1000] (test-arithmetic))' | $TINY_CLJ_REPL" "tiny-clj-Arithmetic" "$RESULTS_DIR/tiny_clj_arithmetic"
else
    echo -e "${YELLOW}⚠️  tiny-clj Arithmetic übersprungen (tiny-clj nicht verfügbar)${NC}"
fi

echo ""

# Benchmark 5: Function Call Performance
echo -e "${BLUE}📊 Benchmark 5: Function Call Performance${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_execution_time "clojure $BENCHMARK_DIR/function_call_performance.clj" "Clojure-FunctionCalls" "$RESULTS_DIR/clojure_functioncalls"
fi

# tiny-clj FunctionCalls benchmark
if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_execution_time "echo 'user=> (defn add [a b] (+ a b)) (defn multiply [a b] (* a b)) (defn test-calls [] (add (multiply 2 3) (add 1 2))) (dotimes [i 1000] (test-calls))' | $TINY_CLJ_REPL" "tiny-clj-FunctionCalls" "$RESULTS_DIR/tiny_clj_functioncalls"
else
    echo -e "${YELLOW}⚠️  tiny-clj FunctionCalls übersprungen (tiny-clj nicht verfügbar)${NC}"
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

# Zeige tiny-clj Performance Log
if [ -f "$TINY_CLJ_LOG" ]; then
    echo ""
    echo -e "${BLUE}📊 tiny-clj Performance Log${NC}"
    echo "================================"
    echo -e "${GREEN}Log gespeichert in: $TINY_CLJ_LOG${NC}"
    echo ""
    echo "Letzte 5 Einträge:"
    tail -5 "$TINY_CLJ_LOG" | column -t -s ','
fi

echo ""
echo -e "${BLUE}🎯 Nächste Schritte:${NC}"
echo "1. Analysiere die Ergebnisse in $RESULTS_DIR/"
echo "2. Vergleiche Startup-Zeit vs Execution-Zeit"
echo "3. Identifiziere Performance-Bottlenecks"
echo "4. Optimiere tiny-clj basierend auf den Ergebnissen"

echo ""
echo -e "${GREEN}✅ Benchmark-Run abgeschlossen!${NC}"
