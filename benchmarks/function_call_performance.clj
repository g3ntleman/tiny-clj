;; Function Call Performance Benchmark
;; Tests function definition and calling with multiple iterations

^#^{:doc "Adds a and b."}
(defn add [a b] (+ a b))
^#^{:doc "Multiplies a and b."}
(defn multiply [a b] (* a b))
^#^{:doc "Subtracts b from a."}
(defn subtract [a b] (- a b))

^#^{:doc "Small workload that composes a few arithmetic functions."}
(defn test-function-calls []
  (add (multiply (subtract 10 5) 3) (add 2 1)))

^#^{:doc "Nested call workload (currently same as test-function-calls)."}
(defn test-nested-function-calls []
  (add (multiply (subtract 10 5) 3) (add 2 1)))

;; Run 10000x more iterations for measurable timing
^#^{:doc "Runs a simple function-call benchmark using (time ...) and dotimes."}
(defn benchmark-function-calls []
  (println "Running function call performance benchmark (10000x iterations)...")
  (time
    (dotimes [i 10000]
      (test-function-calls)
      (test-nested-function-calls))))

(benchmark-function-calls)