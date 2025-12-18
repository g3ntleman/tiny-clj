;; Fibonacci benchmark variant for fib(30)
;; Used for profiling/sample captures to identify hot spots

(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

;; Run fib(30) a few times to keep the profiler busy
(defn run-benchmark []
  (time
    (dotimes [_ 3]
      (fib 30)))
  (println (fib 30)))

(run-benchmark)







