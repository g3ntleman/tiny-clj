;; Simple recursive sum
(defn sum-rec [n]
  (if (<= n 0)
    0
    (+ n (sum-rec (- n 1)))))

;; Benchmark with dotimes for proper JIT warmup and execution time measurement
(defn benchmark-sum-rec []
  (println "Running sum-rec benchmark...")
  (time
    (dotimes [i 1000]
      (sum-rec 100))))

(benchmark-sum-rec)
