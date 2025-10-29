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
  (let [start (time-micro)
        _ (dotimes [i 100] (fib 10))
        end (time-micro)]
    (println (str "100x fib(10) took: " (- end start) " microseconds")))
  
  (println "")
  (println "✅ Benchmark completed!"))

;; Ausführung
(simple-benchmark)
