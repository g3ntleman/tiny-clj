;; Performance test for let bindings (simplified without let)
^#^{:doc "Benchmark workload for let bindings (simplified here as arithmetic)."}
(defn test-let-performance []
  (+ 1 2 3 4 5 6 7 8 9 10))

;; Test nested let performance (simplified)
^#^{:doc "Benchmark workload for nested let bindings (simplified here as arithmetic)."}
(defn test-nested-let-performance []
  (+ 1 2 3 4 5))

;; Run 1000x more iterations for measurable timing
^#^{:doc "Runs the let performance benchmark using (time ...) and dotimes."}
(defn benchmark-let-performance []
  (println "Running let performance benchmark (1000x iterations)...")
  (time
    (dotimes [i 1000]
      (test-let-performance)
      (test-nested-let-performance))))

(benchmark-let-performance)