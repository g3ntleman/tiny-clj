;; Computer Language Benchmarks Game - Fibonacci
;; Original source: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
;; License: BSD-3-Clause

;; Namespace: clojure.benchmarksgame.fibonacci
;; TODO: Replace with (ns clojure.benchmarksgame.fibonacci) when require/use available

;; === Benchmark Implementation ===
;; Recursive version (causes memory issues in tiny-clj)
(defn fib-recursive [n]
  (if (< n 2)
    n
    (+ (fib-recursive (- n 1)) (fib-recursive (- n 2)))))

;; Tail-recursive version using recur (memory-efficient)
(defn fib-helper [a b i]
  (if (= i 0)
    a
    (recur b (+ a b) (- i 1))))

(defn fib [n]
  (if (< n 2)
    n
    (fib-helper 0 1 n)))

;; === Benchmark Execution ===
;; Run 1000x more iterations for measurable timing using dotimes
(defn benchmark-fibonacci []
  (println "Running fibonacci benchmark (1000x iterations)...")
  (time
    (dotimes [i 1000]
      (fib 20))))

(benchmark-fibonacci)
