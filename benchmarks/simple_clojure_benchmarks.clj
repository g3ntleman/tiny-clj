;; Einfaches Benchmark-System in Clojure (ohne File-I/O)
;; Verwendet nur verfügbare Built-ins: println, time-micro, dotimes

;; Benchmark-Helper-Funktionen
(defn run-benchmark [name func iterations]
  (let [start-time (time-micro)
        _ (dotimes [i iterations] (func))
        end-time (time-micro)
        duration (- end-time start-time)]
    {:name name
     :iterations iterations
     :duration-micros duration
     :avg-per-iteration (/ duration iterations)}))

(defn print-benchmark-result [result]
  (println (str "📊 " (:name result) ":"))
  (println (str "   Iterations: " (:iterations result)))
  (println (str "   Total time: " (:duration-micros result) "μs"))
  (println (str "   Avg per iteration: " (:avg-per-iteration result) "μs"))
  (println ""))

;; Benchmark-Funktionen (recur-basiert)
(defn fib [n] 
  (if (< n 2) 
    n 
    (fib-helper 0 1 n)))

(defn fib-helper [a b i] 
  (if (= i 0) 
    a 
    (recur b (+ a b) (- i 1))))

(defn test-arithmetic [] 
  (+ 1 2 3 4 5 6 7 8 9 10))

(defn test-complex-arithmetic [] 
  (+ (* 1 2) (* 3 4) (* 5 1) (- 2 3) (/ 4 5)))

(defn add [a b] (+ a b))
(defn multiply [a b] (* a b))
(defn subtract [a b] (- a b))

(defn test-function-calls [] 
  (add (multiply (subtract 10 5) 3) (add 2 1)))

(defn sum-rec [n] 
  (if (<= n 0) 
    0 
    (+ n (sum-rec (- n 1)))))

;; Haupt-Benchmark-Funktion
(defn run-all-benchmarks []
  (println "🚀 Clojure Benchmark System")
  (println "============================")
  (println "")
  
  (println "📊 Benchmark Results:")
  (println "====================")
  (println "")
  
  ;; Führe Benchmarks einzeln aus
  (print-benchmark-result (run-benchmark "Fibonacci (recur)" #(fib 20) 100))
  (print-benchmark-result (run-benchmark "Arithmetic" #(test-arithmetic) 1000))
  (print-benchmark-result (run-benchmark "Complex Arithmetic" #(test-complex-arithmetic) 1000))
  (print-benchmark-result (run-benchmark "Function Calls" #(test-function-calls) 1000))
  (print-benchmark-result (run-benchmark "Sum Recursive" #(sum-rec 10) 100))
  
  (println "✅ All benchmarks completed!"))

;; Ausführung
(run-all-benchmarks)
