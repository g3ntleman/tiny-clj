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
;; Using fib(47) to match other language benchmarks (e.g., drujensen/fib)
;; Note: This is exponential and will take a long time (~27 seconds in Clojure)
;; Result should be: 2971215073
(println (fib 47))
