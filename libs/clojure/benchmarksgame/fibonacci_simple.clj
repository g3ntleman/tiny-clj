^#^{:doc "Computes Fibonacci(n) using the classic naive recursive definition (benchmarksgame helper)."}
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

(fib 20)
