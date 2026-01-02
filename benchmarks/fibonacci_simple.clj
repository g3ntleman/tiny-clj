;; Simple Fibonacci Benchmark for tiny-clj

^#^{:doc "Computes Fibonacci(n) using the classic naive recursive definition."}
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

;; Run benchmark - fib 20 five times
(dotimes [_ 5]
  (fib 20))

;; Print result to verify
(println (fib 20))

