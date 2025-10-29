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
;; Run 1000x more iterations for measurable timing using dotimes
(defn benchmark-fibonacci []
  (println "Running fibonacci benchmark (1000x iterations)...")
  (time
    (dotimes [i 1000]
      (fib 20))))

(benchmark-fibonacci)
