;; Einfachstes Benchmark-System in Clojure
;; Testet nur die grundlegenden Funktionen

;; Benchmark-Funktionen
(defn fib [n] 
  (if (< n 2) 
    n 
    (fib-helper 0 1 n)))

(defn fib-helper [a b i] 
  (if (= i 0) 
    a 
    (recur b (+ a b) (- i 1))))

(defn test-arithmetic [] 
  (+ 1 2 3 4 5))

;; Einfacher Benchmark-Test
(defn simple-benchmark []
  (println "🚀 Simple Benchmark Test")
  (println "========================")
  (println "")
  
  (println "Testing Fibonacci:")
  (println (str "fib(10) = " (fib 10)))
  (println "")
  
  (println "Testing Arithmetic:")
  (println (str "test-arithmetic() = " (test-arithmetic)))
  (println "")
  
  (println "Running timing test...")
  ;; TODO: time-now muss noch implementiert werden
  ;; (let [start (time-now)
  ;;       _ (dotimes [i 100] (fib 10))
  ;;       end (time-now)]
  ;;   (println (str "100x fib(10) took: " (- end start) " microseconds")))
  (time (dotimes [i 100] (fib 10)))
  
  (println "")
  (println "✅ Benchmark completed!"))

;; Ausführung
(simple-benchmark)
