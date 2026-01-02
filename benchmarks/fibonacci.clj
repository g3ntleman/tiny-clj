;; Computer Language Benchmarks Game - Fibonacci
;; Original source: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
;; License: BSD-3-Clause

(ns clojure.benchmarksgame.fibonacci)

;; Original recursive implementation from Benchmarks Game
^#^{:doc "Computes Fibonacci(n) using the classic naive recursive definition."}
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

;; Benchmark execution
;; Run (fib 20) five times and measure the total execution time.
;; Result of fib 20 should be 6765.
^#^{:doc "Runs the fibonacci benchmark (fib 20 multiple times) and prints the final result."}
(defn run-benchmark []
  (time
    (dotimes [_ 5]
      (fib 20)))
  (println (fib 20)))

(run-benchmark)
