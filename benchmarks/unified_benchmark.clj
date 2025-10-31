;; Unified Benchmark Runner
;; Loads historical results, runs benchmarks, appends new results
;; Uses dotimes for scalable test length
;; JVM warmup: 20 seconds with actual test code (for Clojure only)

(def results-file "benchmark_results/unified_results.csv")

;; Helper: Check if file exists by trying to slurp it
(defn file-exists? [filename]
  (try
    (slurp filename)
    true
    (catch Exception e false)))

;; Helper: Load historical results
(defn load-historical-results []
  (try
    (if (file-exists? results-file)
      (slurp results-file)
      "")
    (catch Exception e "")))

;; Helper: Format timestamp (simple string)
(defn get-timestamp []
  "2024-01-01T00:00:00")

;; Helper: Detect implementation (simple heuristic)
(defn detect-impl []
  (try
    ;; Try to access Java VM property (Clojure only)
    (let [vm-name (System/getProperty "java.vm.name")]
      (if vm-name "Clojure" "tiny-clj"))
    (catch Exception e "tiny-clj")))

;; Benchmark function with warmup for JVM
(defn run-benchmark [name test-fn iterations warmup-seconds impl]
  (println (str "📊 Benchmark: " name))
  
  ;; JVM Warmup for Clojure only - run test code for warmup-seconds
  (when (= impl "Clojure")
    (println (str "🔥 Warming up JVM for " warmup-seconds " seconds..."))
    (let [warmup-iterations (* warmup-seconds 10)]
      (dotimes [i warmup-iterations] (test-fn))))
  
  ;; Measure with time macro - it prints "Elapsed time: X msecs" to stdout
  (println (str "   Running " iterations " iterations..."))
  (time (dotimes [i iterations] (test-fn)))
  (println "")
  
  ;; Return result (time will be parsed from output by script)
  {:timestamp (get-timestamp)
   :impl impl
   :benchmark name
   :time-ms 0  ;; Parsed from time output
   :iterations iterations
   :warmup-s warmup-seconds})

;; Helper: Convert lines list to string (without apply)
(defn lines-to-str [lines]
  (if (empty? lines)
    ""
    (let [first-line (first lines)
          rest-lines (rest lines)]
      (if (empty? rest-lines)
        first-line
        (str first-line (lines-to-str rest-lines))))))

;; Save results to CSV
(defn save-results [results]
  (let [header "Timestamp,Implementation,Benchmark,Execution_Time_ms,Iterations,Warmup_s\n"
        existing (load-historical-results)
        ;; Format results as CSV lines (using fn instead of #)
        format-line (fn [result]
                     (str (:timestamp result) "," (:impl result) "," (:benchmark result) "," 
                          (:time-ms result) "," (:iterations result) "," (:warmup-s result) "\n"))
        lines (map format-line results)
        lines-str (lines-to-str lines)
        content (str (if (empty? existing) header "") lines-str)]
    (try
      ;; For append mode: check if file exists
      (if (file-exists? results-file)
        ;; Append to existing file
        (let [existing-content (slurp results-file)
              combined-content (str existing-content lines-str)]
          (spit results-file combined-content))
        ;; Create new file with header
        (spit results-file (str header lines-str)))
      (catch Exception e
        (println (str "Error saving results: " e))))))

;; Benchmark tests using dotimes for scaling

;; Test 1: Let Performance (tests environment chaining)
(defn test-let-simple []
  (let [a 1 b 2 c 3] (+ a b c)))

(defn test-let-nested []
  (let [a 1]
    (let [b (+ a 1)]
      (let [c (+ b 1)]
        (+ a b c)))))

(defn benchmark-let [iterations impl]
  (run-benchmark "Let" 
                 (fn [] (do (test-let-simple) (test-let-nested)))
                 iterations
                 (if (= impl "Clojure") 20 0)
                 impl))

;; Test 2: SumRec (recursive with environment lookups)
(defn sum-rec [n]
  (if (<= n 0)
    0
    (+ n (sum-rec (- n 1)))))

(defn benchmark-sumrec [iterations impl]
  (run-benchmark "SumRec"
                 (fn [] (sum-rec 100))
                 iterations
                 (if (= impl "Clojure") 20 0)
                 impl))

;; Test 3: Fibonacci (recursive)
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

(defn benchmark-fibonacci [iterations impl]
  (run-benchmark "Fibonacci"
                 (fn [] (fib 20))
                 iterations
                 (if (= impl "Clojure") 20 0)
                 impl))

;; Test 4: Arithmetic
(defn test-arithmetic []
  (let [a 1 b 2 c 3 d 4 e 5]
    (+ (* a b) (- c d) (/ e 2))))

(defn benchmark-arithmetic [iterations impl]
  (run-benchmark "Arithmetic"
                 test-arithmetic
                 iterations
                 (if (= impl "Clojure") 20 0)
                 impl))

;; Test 5: Function Calls
(defn add [a b] (+ a b))
(defn multiply [a b] (* a b))

(defn test-function-calls []
  (add (multiply 2 3) (add 1 2)))

(defn benchmark-function-calls [iterations impl]
  (run-benchmark "FunctionCalls"
                 test-function-calls
                 iterations
                 (if (= impl "Clojure") 20 0)
                 impl))

;; Main benchmark runner
(defn run-all-benchmarks []
  (println "=== Unified Benchmark Runner ===")
  (println "")
  
  ;; Detect implementation
  (let [impl (detect-impl)]
    (println (str "Detected implementation: " impl))
    (println ""))
  
  ;; Load historical results
  (let [historical (load-historical-results)
        impl (detect-impl)]
    (if (not (empty? historical))
      (println (str "📚 Loaded historical results from " results-file))
      (println "📝 Creating new results file"))
    (println "")
    
    ;; Run all benchmarks with scaled iterations using dotimes
    (let [results [(benchmark-let 10000 impl)
                    (benchmark-sumrec 1000 impl)
                    (benchmark-fibonacci 100 impl)
                    (benchmark-arithmetic 10000 impl)
                    (benchmark-function-calls 10000 impl)]]
      
      ;; Save results
      (save-results results)
      (println "✅ Results saved to" results-file)
      
      ;; Show comparison
      (println "")
      (println "=== Performance Comparison ===")
      (doseq [result results]
        (println (str (:benchmark result) ": " (:time-ms result) "ms (" 
                      (:iterations result) " iterations)")))
      
      results)))

;; Execute
(run-all-benchmarks)
