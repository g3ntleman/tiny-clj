;; Performance test for let bindings (simplified without let)
(defn test-let-performance []
  (+ 1 2 3 4 5 6 7 8 9 10))

;; Test nested let performance (simplified)
(defn test-nested-let-performance []
  (+ 1 2 3 4 5))

;; Benchmark with dotimes for proper JIT warmup and execution time measurement
(defn benchmark-let-performance []
  (println "Running let performance benchmark...")
  (time
    (dotimes [i 10000]
      (test-let-performance)
      (test-nested-let-performance))))

(benchmark-let-performance)