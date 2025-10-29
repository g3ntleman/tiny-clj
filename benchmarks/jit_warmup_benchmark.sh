#!/bin/bash

# JIT-Warmup Benchmark für fairen Vergleich zwischen tiny-clj und Clojure
# Läuft 20 Sekunden lang, damit JVM-Optimierungen greifen können

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
DURATION_SECONDS=20

# Erstelle Results-Verzeichnis
mkdir -p "$RESULTS_DIR"

echo -e "${BLUE}🔥 JIT-Warmup Benchmark (${DURATION_SECONDS}s) - tiny-clj vs Clojure${NC}"
echo "=================================================================="

# Funktion zum Messen der Performance mit JIT-Warmup
measure_jit_performance() {
    local command="$1"
    local description="$2"
    local output_file="$3"
    
    echo -e "${YELLOW}⏱️  Messe: $description (${DURATION_SECONDS}s mit JIT-Warmup)${NC}"
    
    # Starte den Benchmark-Prozess im Hintergrund
    timeout "${DURATION_SECONDS}" bash -c "$command" > "$output_file.log" 2>&1 &
    local pid=$!
    
    # Warte auf das Ende des Prozesses
    wait $pid
    local exit_code=$?
    
    if [ $exit_code -eq 124 ]; then
        echo -e "${GREEN}✅ $description: ${DURATION_SECONDS}s erfolgreich abgeschlossen${NC}"
        # Extrahiere die letzte gemessene Zeit aus der Ausgabe
        local last_time=$(grep -E "Elapsed time:|Time:" "$output_file.log" | tail -1 | sed 's/.*Elapsed time: \([0-9.]*\) msecs.*/\1/' | sed 's/.*Time: \([0-9.]*\)ms.*/\1/')
        if [ -n "$last_time" ]; then
            echo -e "${GREEN}   Letzte gemessene Zeit: ${last_time}ms${NC}"
            echo "$description,$last_time,$DURATION_SECONDS,JIT_WARMUP" >> "$RESULTS_DIR/jit_warmup_results.csv"
        else
            echo -e "${YELLOW}   Keine Zeitmessung gefunden${NC}"
            echo "$description,0,$DURATION_SECONDS,JIT_WARMUP" >> "$RESULTS_DIR/jit_warmup_results.csv"
        fi
    else
        echo -e "${RED}❌ $description: Fehler oder vorzeitiger Abbruch${NC}"
        echo "$description,ERROR,$DURATION_SECONDS,JIT_WARMUP" >> "$RESULTS_DIR/jit_warmup_results.csv"
    fi
}

# CSV-Header schreiben
echo "Benchmark,Last_Time_ms,Duration_s,Type" > "$RESULTS_DIR/jit_warmup_results.csv"

# Prüfe Verfügbarkeit
if command -v clojure &> /dev/null; then
    CLOJURE_AVAILABLE=true
    echo -e "${GREEN}✅ Standard Clojure gefunden${NC}"
else
    CLOJURE_AVAILABLE=false
    echo -e "${YELLOW}⚠️  Standard Clojure nicht gefunden - überspringe Clojure-Benchmarks${NC}"
fi

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

# Benchmark 1: Fibonacci mit JIT-Warmup
echo -e "${BLUE}📊 Benchmark 1: Fibonacci (fib 20) - ${DURATION_SECONDS}s${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_jit_performance "
        clojure -e \"
        (defn fib [n] 
          (if (< n 2) 
            n 
            (+ (fib (- n 1)) (fib (- n 2)))))
        
        (defn benchmark-fibonacci []
          (let [start (System/currentTimeMillis)]
            (dotimes [i 1000]
              (fib 20))
            (let [end (System/currentTimeMillis)]
              (println (str \\\"Elapsed time: \\\" (- end start) \\\" msecs\\\")))))
        
        (loop []
          (benchmark-fibonacci)
          (Thread/sleep 100)
          (recur))
        \"
    " "Clojure-Fibonacci-JIT" "$RESULTS_DIR/clojure_fibonacci_jit"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_jit_performance "
        echo \"
        (defn fib [n] 
          (if (< n 2) 
            n 
            (+ (fib (- n 1)) (fib (- n 2)))))
        
        (defn benchmark-fibonacci []
          (let [start (time-now)]
            (dotimes [i 1000]
              (fib 20))
            (let [end (time-now)]
              (println (str \\\"Elapsed time: \\\" (- end start) \\\" msecs\\\")))))
        
        (loop []
          (benchmark-fibonacci)
          (sleep 100)
          (recur))
        \" | $TINY_CLJ_PATH
    " "tiny-clj-Fibonacci-JIT" "$RESULTS_DIR/tiny_clj_fibonacci_jit"
fi

echo ""

# Benchmark 2: Arithmetik mit JIT-Warmup
echo -e "${BLUE}📊 Benchmark 2: Arithmetik - ${DURATION_SECONDS}s${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_jit_performance "
        clojure -e \"
        (defn arithmetic-test []
          (let [start (System/currentTimeMillis)]
            (dotimes [i 10000]
              (+ (* 2 3) (- 10 5) (/ 20 4)))
            (let [end (System/currentTimeMillis)]
              (println (str \\\"Elapsed time: \\\" (- end start) \\\" msecs\\\")))))
        
        (loop []
          (arithmetic-test)
          (Thread/sleep 100)
          (recur))
        \"
    " "Clojure-Arithmetic-JIT" "$RESULTS_DIR/clojure_arithmetic_jit"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_jit_performance "
        echo \"
        (defn arithmetic-test []
          (let [start (time-now)]
            (dotimes [i 10000]
              (+ (* 2 3) (- 10 5) (/ 20 4)))
            (let [end (time-now)]
              (println (str \\\"Elapsed time: \\\" (- end start) \\\" msecs\\\")))))
        
        (loop []
          (arithmetic-test)
          (sleep 100)
          (recur))
        \" | $TINY_CLJ_PATH
    " "tiny-clj-Arithmetic-JIT" "$RESULTS_DIR/tiny_clj_arithmetic_jit"
fi

echo ""

# Benchmark 3: Function Calls mit JIT-Warmup
echo -e "${BLUE}📊 Benchmark 3: Function Calls - ${DURATION_SECONDS}s${NC}"

if [ "$CLOJURE_AVAILABLE" = true ]; then
    measure_jit_performance "
        clojure -e \"
        (defn add [a b] (+ a b))
        (defn multiply [a b] (* a b))
        (defn subtract [a b] (- a b))
        
        (defn function-call-test []
          (let [start (System/currentTimeMillis)]
            (dotimes [i 5000]
              (add (multiply 2 3) (subtract 10 5)))
            (let [end (System/currentTimeMillis)]
              (println (str \\\"Elapsed time: \\\" (- end start) \\\" msecs\\\")))))
        
        (loop []
          (function-call-test)
          (Thread/sleep 100)
          (recur))
        \"
    " "Clojure-FunctionCalls-JIT" "$RESULTS_DIR/clojure_functioncalls_jit"
fi

if [ "$TINY_CLJ_AVAILABLE" = true ]; then
    measure_jit_performance "
        echo \"
        (defn add [a b] (+ a b))
        (defn multiply [a b] (* a b))
        (defn subtract [a b] (- a b))
        
        (defn function-call-test []
          (let [start (time-now)]
            (dotimes [i 5000]
              (add (multiply 2 3) (subtract 10 5)))
            (let [end (time-now)]
              (println (str \\\"Elapsed time: \\\" (- end start) \\\" msecs\\\")))))
        
        (loop []
          (function-call-test)
          (sleep 100)
          (recur))
        \" | $TINY_CLJ_PATH
    " "tiny-clj-FunctionCalls-JIT" "$RESULTS_DIR/tiny_clj_functioncalls_jit"
fi

echo ""

# Zusammenfassung
echo -e "${BLUE}📈 JIT-Warmup Benchmark-Zusammenfassung${NC}"
echo "=============================================="

if [ -f "$RESULTS_DIR/jit_warmup_results.csv" ]; then
    echo -e "${GREEN}Ergebnisse gespeichert in: $RESULTS_DIR/jit_warmup_results.csv${NC}"
    echo ""
    echo "Detaillierte Ergebnisse:"
    cat "$RESULTS_DIR/jit_warmup_results.csv" | column -t -s ','
fi

echo ""
echo -e "${BLUE}🎯 Analyse der JIT-Optimierungen:${NC}"
echo "1. Clojure profitiert von JVM JIT-Compilation"
echo "2. tiny-clj läuft bereits optimiert (nativ kompiliert)"
echo "3. Vergleich zeigt 'warm' vs 'cold' Performance"
echo "4. JIT-Warmup dauert typischerweise 1-5 Sekunden"

echo ""
echo -e "${GREEN}✅ JIT-Warmup Benchmark abgeschlossen!${NC}"
