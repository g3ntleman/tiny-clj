;; Function Call Performance Benchmark
;; Tests function definition and calling with multiple iterations

(defn add [a b] (+ a b))
(defn multiply [a b] (* a b))
(defn subtract [a b] (- a b))

(defn test-function-calls []
  (add (multiply (subtract 10 5) 3) (add 2 1)))

(defn test-nested-function-calls []
  (add (multiply (subtract 10 5) 3) (add 2 1)))

;; Run 1000x more iterations for measurable timing
(defn benchmark-function-calls []
  (println "Running function call performance benchmark (1000x iterations)...")
  (time
    (dotimes [i 1000]
      (test-function-calls)
      (test-nested-function-calls))))

(benchmark-function-calls)