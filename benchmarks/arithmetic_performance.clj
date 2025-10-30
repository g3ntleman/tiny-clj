;; Arithmetic Performance Benchmark
;; Tests basic arithmetic operations with multiple iterations

(defn test-arithmetic []
  (+ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20))

(defn test-complex-arithmetic []
  (+ (* 1 2) (* 3 4) (* 5 1) (- 2 3) (/ 4 5)))

;; Run 10000x more iterations for measurable timing
(defn benchmark-arithmetic []
  (println "Running arithmetic performance benchmark (10000x iterations)...")
  (time
    (dotimes [i 10000]
      (test-arithmetic)
      (test-complex-arithmetic))))

(benchmark-arithmetic)