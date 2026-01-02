;; Einfaches Benchmark-System in Clojure (ohne File-I/O)
;; Verwendet nur verfügbare Built-ins: println, time-now, dotimes
;; TODO: time-now muss noch implementiert werden

;; Benchmark-Helper-Funktionen
^#^{:doc "Führt func iterations-mal aus und liefert eine Ergebnis-Map mit Timing-Daten (in Mikrosekunden)."}
(defn run-benchmark [name func iterations]
  (let [start-time (time-now)
        _ (dotimes [i iterations] (func))
        end-time (time-now)
        duration (- end-time start-time)]
    {:name name
     :iterations iterations
     :duration-micros duration
     :avg-per-iteration (/ duration iterations)}))

^#^{:doc "Gibt ein Benchmark-Ergebnis (Map aus run-benchmark) menschenlesbar auf stdout aus."}
(defn print-benchmark-result [result]
  (println (str "📊 " (:name result) ":"))
  (println (str "   Iterations: " (:iterations result)))
  (println (str "   Total time: " (:duration-micros result) "μs"))
  (println (str "   Avg per iteration: " (:avg-per-iteration result) "μs"))
  (println ""))

;; Benchmark-Funktionen (recur-basiert)
^#^{:doc "Berechnet Fibonacci(n) iterativ über tail-recursion (via fib-helper)."}
(defn fib [n] 
  (if (< n 2) 
    n 
    (fib-helper 0 1 n)))

^#^{:doc "Helper für fib: führt i Schritte einer Fibonacci-Iteration aus (tail-recursive)."}
(defn fib-helper [a b i] 
  (if (= i 0) 
    a 
    (recur b (+ a b) (- i 1))))

^#^{:doc "Einfacher Arithmetic-Workload für Benchmarking."}
(defn test-arithmetic [] 
  (+ 1 2 3 4 5 6 7 8 9 10))

^#^{:doc "Komplexerer Arithmetic-Workload (Mix aus +, *, -, /) für Benchmarking."}
(defn test-complex-arithmetic [] 
  (+ (* 1 2) (* 3 4) (* 5 1) (- 2 3) (/ 4 5)))

^#^{:doc "Addiert a und b."}
(defn add [a b] (+ a b))
^#^{:doc "Multipliziert a und b."}
(defn multiply [a b] (* a b))
^#^{:doc "Subtrahiert b von a."}
(defn subtract [a b] (- a b))

^#^{:doc "Kleiner Funktionsaufruf-Workload für Benchmarking."}
(defn test-function-calls [] 
  (add (multiply (subtract 10 5) 3) (add 2 1)))

^#^{:doc "Rekursive Summenfunktion: 1+...+n (naiv rekursiv)."}
(defn sum-rec [n] 
  (if (<= n 0) 
    0 
    (+ n (sum-rec (- n 1)))))

;; Haupt-Benchmark-Funktion
^#^{:doc "Führt alle Benchmarks aus und druckt die Ergebnisse."}
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
