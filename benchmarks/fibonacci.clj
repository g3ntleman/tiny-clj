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
;; Manual repetition for execution time measurement
(defn run-fib-benchmark []
  (fib 20) (fib 20) (fib 20) (fib 20) (fib 20)
  (fib 20) (fib 20) (fib 20) (fib 20) (fib 20)
  (fib 20) (fib 20) (fib 20) (fib 20) (fib 20)
  (fib 20) (fib 20) (fib 20) (fib 20) (fib 20)
  (fib 20) (fib 20) (fib 20) (fib 20) (fib 20)
  (fib 20) (fib 20) (fib 20) (fib 20) (fib 20)
  (fib 20) (fib 20) (fib 20) (fib 20) (fib 20)
  (fib 20) (fib 20) (fib 20) (fib 20) (fib 20)
  (fib 20) (fib 20) (fib 20) (fib 20) (fib 20)
  (fib 20) (fib 20) (fib 20) (fib 20) (fib 20))

(defn benchmark-fibonacci []
  (println "Running fibonacci benchmark...")
  (time (run-fib-benchmark)))

(benchmark-fibonacci)
