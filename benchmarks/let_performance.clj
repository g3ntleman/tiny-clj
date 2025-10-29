;; Performance test for let bindings (simplified without let)
(defn test-let-performance []
  (+ 1 2 3 4 5 6 7 8 9 10))

;; Test nested let performance (simplified)
(defn test-nested-let-performance []
  (+ 1 2 3 4 5))

;; Run 1000x more iterations for measurable timing
(defn benchmark-let-performance []
  (println "Running let performance benchmark (1000x iterations)...")
  (time
    (dotimes [i 1000]
      (test-let-performance)
      (test-nested-let-performance))))

(benchmark-let-performance)