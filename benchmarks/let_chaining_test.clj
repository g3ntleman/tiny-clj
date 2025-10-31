;; Benchmark für Environment-Chaining (ohne user=> Prefix)
(defn test-let [] (let [a 1 b 2 c 3] (+ a b c)))
(defn test-nested-let [] (let [a 1] (let [b (+ a 1)] (let [c (+ b 1)] (+ a b c)))))
(println "Starting let benchmark with environment chaining...")
(dotimes [i 1000] (test-let) (test-nested-let))
(println "Benchmark completed: 2000 let operations (1000 simple + 1000 nested)")

