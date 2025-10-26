;; Computer Language Benchmarks Game - Fibonacci
;; Original source: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
;; License: BSD-3-Clause

;; Namespace: clojure.benchmarksgame.fibonacci
;; TODO: Replace with (ns clojure.benchmarksgame.fibonacci) when require/use available

;; === Benchmark Implementation ===
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

;; === Benchmark Execution ===
;; Calculate fibonacci(20) - small enough for reasonable execution time
(println "fibonacci(20):" (fib 20))
