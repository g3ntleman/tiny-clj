;; Naive recursive Fibonacci benchmark
;; Uses the same naive recursive algorithm on both Clojure and tiny-clj
;; Uses (time) for consistent measurement on both sides

^#^{:doc "Computes Fibonacci(n) using the classic naive recursive definition."}
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

;; Benchmark: Run fib(20) multiple times
;; This allows fair comparison between Clojure (JIT-optimized) and tiny-clj (interpreter)
(println "Running naive recursive fibonacci benchmark...")
(println "Algorithm: fib(n) = if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))")
(println "Test: fib(20) = 6765")
(println "")

;; Warmup: Run once to ensure function is defined
(def warmup-result (fib 20))
(println (str "Warmup: fib(20) = " warmup-result))
(println "")

;; Actual benchmark: 5 iterations using (time)
;; Both Clojure and tiny-clj use the same (time) function for measurement
(println "Running 5 iterations of fib(20)...")
(time
  (dotimes [i 5]
    (fib 20)))

(println "")
(println "Benchmark completed!")

