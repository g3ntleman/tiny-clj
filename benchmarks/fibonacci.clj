;; Computer Language Benchmarks Game - Fibonacci
;; Original source: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
;; License: BSD-3-Clause

(ns clojure.benchmarksgame.fibonacci)

;; Original recursive implementation from Benchmarks Game
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

;; Benchmark execution
;; Run (fib 20) five times and measure the total execution time.
;; Result of fib 20 should be 6765.
(defn run-benchmark []
  (time
    (dotimes [_ 5]
      (fib 20)))
  (println (fib 20)))

(run-benchmark)
