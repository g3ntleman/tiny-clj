;; Arithmetic Performance Benchmark
;; Tests basic arithmetic operations with multiple iterations

(defn test-complex-arithmetic []
  (+ (* 1 2) (* 3 4) (* 5 1) (- 2 3) (/ 4 5)))

;; Run 10000x more iterations for measurable timing
(defn benchmark-arithmetic []
  (println "Running arithmetic performance benchmark (10000x iterations)...")
  (time
    (dotimes [i 10000]
      (test-complex-arithmetic))))

(benchmark-arithmetic)