(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

(def iterations 2000)

(time
  (dotimes [i iterations]
    (fib 20)))
