^#^{:doc "Computes Fibonacci(n) using the classic naive recursive definition."}
(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

(println "Starting fib(20)...")
(def start-time (System/currentTimeMillis))
(def result (fib 20))
(def end-time (System/currentTimeMillis))
(def elapsed (- end-time start-time))
(println (str "fib(20) = " result))
(println (str "Time: " elapsed " ms"))

